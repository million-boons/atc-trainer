#include "app/PlannerController.h"

#include <QMetaObject>
#include <algorithm>
#include <utility>

using L = LabController;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString presetName(PlanPreset p) {
  switch (p) {
    case PlanPreset::Easy: return PlannerController::tr("Easy");
    case PlanPreset::Normal: return PlannerController::tr("Normal");
    case PlanPreset::Hard: return PlannerController::tr("Hard");
    case PlanPreset::Custom: break;
  }
  return PlannerController::tr("Custom");
}
QString seconds(int ms) { return PlannerController::tr("%1 s").arg(ms / 1000.0, 0, 'f', 1); }
QString runsText(const Plan& p) {
  const QString n = p.runs == 1 ? PlannerController::tr("1 run") : PlannerController::tr("%1 runs").arg(p.runs);
  return p.lastRun.empty() ? n : n + ", " + q(p.lastRun);
}
QString valuesText(const Plan& p) { return p.fixed ? PlannerController::tr("Values: fixed") : PlannerController::tr("Values: random"); }
QString traineeText(const Plan& p) { return p.trainee.empty() ? PlannerController::tr("Anyone") : q(p.trainee); }
QString modeText(const Plan& p) { return p.assessed ? PlannerController::tr("Assessed") : PlannerController::tr("Practice"); }
QString planLine(const Plan& p) {
  return QStringList{valuesText(p), p.trainee.empty() ? traineeText(p) : PlannerController::tr("Trainee %1").arg(traineeText(p)), modeText(p),
                     presetName(p.difficulty.preset)}
      .join("   ");
}
// Text of one line: the note is never parsed, a line break in it would only cost a line on the card.
QString oneLine(QString t, int limit) { return t.replace('\n', ' ').replace('\r', ' ').left(limit); }
void stepDelay(PlanDifficulty& d, bool from, int steps) {
  int& v = from ? d.replyFromMs : d.replyToMs;
  v = std::clamp(v + steps * 100, from ? PlannerController::kDelayMin : d.replyFromMs, from ? d.replyToMs : PlannerController::kDelayMax);
}
}  // namespace

PlannerController::PlannerController(AccessController* access, QObject* parent) : QObject(parent) { setAccess(access); }

void PlannerController::setAccess(AccessController* a) {
  if (access_) access_->disconnect(this);
  access_ = a;
  if (a) connect(a, &AccessController::changed, this, &PlannerController::accessChanged);
  accessChanged();
}

void PlannerController::setBackend(PlannerBackend b) {
  b_ = std::move(b);
  if (!b_.setEvents) return;
  PlannerEvents e;
  e.plansChanged = [this] { queued([this] { reload(); }); };
  e.putAway = [this](const std::string& name, const std::string& person) {
    queued([this, name, person] {
      say(L::Caution, tr("%1 logged out: their plan \"%2\" was taken off the Dashboard.").arg(q(person), q(name)));
      emit planChanged();
    });
  };
  b_.setEvents(std::move(e));
}

void PlannerController::queued(std::function<void()> f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }

bool PlannerController::say(int level, const QString& text) {
  messageLevel_ = text.isEmpty() ? int(L::None) : level;
  message_ = text;
  emit messageChanged();
  return false;
}

PlannerBackend::Done PlannerController::answer(std::function<void()> ok, const QString& doneText) {
  ++waiting_;
  emit planChanged();
  return [this, ok = std::move(ok), doneText](const std::string& why) {
    queued([this, ok, doneText, why] {
      --waiting_;
      if (!why.empty()) {
        say(L::Caution, q(why));
      } else {
        say(L::Ok, doneText);
        if (ok) ok();
      }
      emit planChanged();
    });
  };
}

bool PlannerController::allowed() {
  if (!connected()) return say(L::Caution, tr("This build has no Planner backend."));
  if (!instructor()) return say(L::Caution, tr("Only the instructor may do this."));
  if (trainerBusy_) return say(L::Caution, tr("Not during an exercise."));
  if (busy()) return say(L::Caution, tr("Wait for the last request."));
  return true;
}

bool PlannerController::edit() { return allowed() && open_; }

bool PlannerController::leaveEdit() {
  if (open_ && dirty()) return say(L::Caution, tr("Save or undo this plan first."));
  return true;
}

void PlannerController::allChanged() {
  emit listChanged();
  emit planChanged();
}

void PlannerController::accessChanged() {
  const bool now = instructor();
  if (std::exchange(wasInstructor_, now) != now) {
    if (now) {
      reload(true);
    } else {
      open_ = false;
      draft_ = {};
      editPresets_.clear();
      clearMessage();
    }
  }
  allChanged();  // the plans somebody may choose, and the records
}

void PlannerController::setTrainerBusy(bool on) {
  if (trainerBusy_ == on) return;
  trainerBusy_ = on;
  allChanged();
}

// ---- loading

void PlannerController::enter() {
  clearMessage();
  reload();
}

void PlannerController::reload(bool reopen) {
  if (!b_.load) return;
  const unsigned n = ++loadNo_;
  b_.load([this, n, reopen](PlannerContent c, const std::string& why) {
    queued([this, n, reopen, c = std::move(c), why] {
      if (n != loadNo_) return;
      if (!why.empty()) {
        loaded_ = false;
        say(L::Fault, q(why));
        return allChanged();
      }
      const bool keep = loaded_ && open_ && !reopen && dirty();  // an edit stays, with the revision it started from
      content_ = c;
      loaded_ = true;
      if (!keep && instructor()) {
        const Plan* p = find(want_.empty() ? draft_.id : want_);
        if (!p && !content_.plans.empty()) p = &content_.plans.front();
        open_ = p != nullptr;
        if (p) open(*p);
      }
      want_.clear();
      allChanged();
    });
  });
}

void PlannerController::open(const Plan& p) {
  draft_ = p;
  draftRevision_ = content_.revision;
  open_ = true;
  last_ = Field::None;
  check();
  askFits();
  allChanged();
  emit planLoaded();  // after planChanged: the fields read the new values
}

// The voice bank, for the scenario on the screen.
void PlannerController::check() {
  coverage_ = {};
  const unsigned n = ++coverageNo_;
  const PlanScenario* s = scenarioOf(draft_.scenario);
  if (!s || !s->errors.empty() || !b_.coverage) return;
  b_.coverage(draft_.scenario, [this, n](PlanCoverage c) {
    queued([this, n, c] {
      if (n != coverageNo_) return;
      coverage_ = c;
      emit planChanged();
    });
  });
}

// ---- questions

const PlanScenario* PlannerController::scenarioOf(const std::string& id) const {
  const auto at = std::find_if(content_.scenarios.begin(), content_.scenarios.end(), [&](const PlanScenario& s) { return s.id == id; });
  return at == content_.scenarios.end() ? nullptr : &*at;
}

const Plan* PlannerController::find(const std::string& id) const {
  const auto at = std::find_if(content_.plans.begin(), content_.plans.end(), [&](const Plan& p) { return p.id == id; });
  return id.empty() || at == content_.plans.end() ? nullptr : &*at;
}

const PlanDifficulty* PlannerController::presetOf(PlanPreset p) const {
  const auto at = std::find_if(content_.presets.begin(), content_.presets.end(), [p](const PlanDifficulty& d) { return d.preset == p; });
  return at == content_.presets.end() ? nullptr : &*at;
}

bool PlannerController::known(const std::string& trainee) const { return people().contains(q(trainee)); }

QStringList PlannerController::people() const {
  QStringList l;
  if (access_)
    for (const QVariant& v : access_->people()) l << v.toMap()["id"].toString();
  return l;
}

bool PlannerController::fits(const PlanValues& v, const PlanScenario& s) const {
  return std::find(s.combinations.begin(), s.combinations.end(), v) != s.combinations.end();
}

PlannerController::Fit PlannerController::draftFit() const {
  const PlanScenario* s = scenarioOf(draft_.scenario);
  const PlanValues& v = draft_.values;
  if (!s || v.callsign.empty() || v.runway.empty() || v.taxiway.empty()) return Fit::Incomplete;
  if (!s->tooMany) return fits(v, *s) ? Fit::Yes : Fit::No;
  const bool same = fit_.answered && fit_.scenario == draft_.scenario && fit_.values == v;
  return !same ? Fit::Checking : fit_.ok ? Fit::Yes : Fit::No;
}

QString PlannerController::fitWhy() const {
  const bool same = fit_.answered && fit_.scenario == draft_.scenario && fit_.values == draft_.values;
  return same && !fit_.ok ? q(fit_.why) : QString();
}

// The backend says whether fixed values fit, with the reason (10 section 8). Needed with tooMany; otherwise the
// list decides at once and the answer only adds the reason.
void PlannerController::askFits() {
  if (!open_ || !draft_.fixed || !b_.fits) return;
  const PlanValues& v = draft_.values;
  if (v.callsign.empty() || v.runway.empty() || v.taxiway.empty()) return;
  if (fit_.scenario == draft_.scenario && fit_.values == v) return;  // asked already
  fit_ = {draft_.scenario, v, false, false, {}};
  const unsigned n = ++fitNo_;
  b_.fits(draft_.scenario, v, [this, n](bool ok, const std::string& why) {
    queued([this, n, ok, why] {
      if (n != fitNo_) return;
      fit_.answered = true;
      fit_.ok = ok;
      fit_.why = why;
      emit planChanged();
    });
  });
}

namespace {
// Do all values that are chosen (but those left out) occur together?
bool agree(const PlanValues& v, const PlanScenario& s, bool aircraft, bool runway, bool taxiway) {
  return std::any_of(s.combinations.begin(), s.combinations.end(), [&](const PlanValues& c) {
    return (!aircraft || v.callsign.empty() || (c.callsign == v.callsign && c.type == v.type)) &&
           (!runway || v.runway.empty() || c.runway == v.runway) && (!taxiway || v.taxiway.empty() || c.taxiway == v.taxiway);
  });
}
}  // namespace

// Nothing is corrected silently: a choice that no longer fits is marked. The value chosen last is not the one at fault.
bool PlannerController::misfit(Field f) const {
  const PlanScenario* s = scenarioOf(draft_.scenario);
  const auto& v = draft_.values;
  const bool empty = f == Field::Aircraft ? v.callsign.empty() : f == Field::Runway ? v.runway.empty() : v.taxiway.empty();
  if (!draft_.fixed || !s || s->tooMany || empty || f == last_ || agree(v, *s, true, true, true)) return false;
  const auto without = [&](Field x) { return x != last_ && agree(v, *s, x != Field::Aircraft, x != Field::Runway, x != Field::Taxiway); };
  const bool any = without(Field::Aircraft) || without(Field::Runway) || without(Field::Taxiway);
  return !any || without(f);
}

// The values keep the numbers of the preset they were saved with (P-6, D-144).
bool PlannerController::presetChanged(const Plan& p) const {
  const PlanDifficulty* now = presetOf(p.difficulty.preset);
  return now && !(*now == p.difficulty);
}

PlannerController::State PlannerController::stateOf(const Plan& p) const {
  const PlanScenario* s = scenarioOf(p.scenario);
  if (!s) return {L::Fault, tr("Cannot start: no such scenario"), false};
  if (!s->errors.empty()) return {L::Fault, tr("Cannot start: scenario has errors"), false};
  if (s->hash != p.scenarioHash) return {L::Caution, tr("Scenario changed"), false};
  if (p.fixed && !p.valuesError.empty()) return {L::Fault, tr("Cannot start: %1").arg(q(p.valuesError)), false};
  if (p.fixed && !s->tooMany && !fits(p.values, *s)) return {L::Fault, tr("Cannot start: values do not fit"), false};
  if (!p.fixed && s->combinations.empty()) return {L::Fault, tr("Cannot start: no values"), false};
  if (p.assessed && s->placeholder) return {L::Fault, tr("Cannot start: not Validated"), false};
  if (p.assessed && p.trainee.empty()) return {L::Fault, tr("Cannot start: no trainee"), false};
  if (!p.trainee.empty() && !known(p.trainee)) return {L::Fault, tr("Cannot start: no record %1").arg(q(p.trainee)), false};
  if (presetChanged(p)) return {L::Caution, tr("Warns: preset changed"), true};
  return {L::Ok, tr("Ready"), true};
}

// D-142: their own and Anyone plans; a guest Anyone plans; the instructor all.
bool PlannerController::mayChoose(const Plan& p) const {
  if (instructor() || p.trainee.empty()) return true;
  return access_ && access_->trainee() && access_->personId() == q(p.trainee);
}

bool PlannerController::dirty() const {
  const Plan* s = find(draft_.id);
  if (!open_) return false;
  if (!s) return true;
  const Plan &a = draft_, &b = *s;
  return a.name != b.name || a.scenario != b.scenario || a.trainee != b.trainee || a.assessed != b.assessed || a.fixed != b.fixed ||
         a.briefing != b.briefing || (a.fixed && !(a.values == b.values)) || !(a.difficulty == b.difficulty);
}

bool PlannerController::changes() const {
  const PlanScenario* s = scenarioOf(draft_.scenario);
  return dirty() || presetChanged(draft_) || (s && s->hash != draft_.scenarioHash);
}

QString PlannerController::saveRefusal() const {
  const QString name = q(draft_.name).trimmed();
  if (name.isEmpty()) return tr("Enter a name.");
  for (const Plan& p : content_.plans)
    if (p.id != draft_.id && q(p.name).compare(name, Qt::CaseInsensitive) == 0) return tr("Another plan has this name.");
  const PlanScenario* s = scenarioOf(draft_.scenario);
  if (!s) return tr("Choose a scenario.");
  if (!s->errors.empty()) return tr("The scenario cannot be loaded.");
  if (draft_.fixed) {
    const auto& v = draft_.values;
    if (v.callsign.empty() || v.runway.empty() || v.taxiway.empty()) return tr("Choose callsign, runway and taxiway.");
    const Fit f = draftFit();
    if (f == Fit::Checking) return tr("Checking the values...");
    if (f == Fit::No) return fitWhy().isEmpty() ? tr("The values do not fit together.") : tr("The values do not fit together: %1.").arg(fitWhy());
  } else if (s->combinations.empty()) {
    return tr("The scenario has no values.");
  }
  if (draft_.assessed && s->placeholder) return tr("Assessed: the scenario is not Validated.");
  if (draft_.assessed && draft_.trainee.empty()) return tr("Assessed needs a trainee.");
  if (!draft_.trainee.empty() && !known(draft_.trainee)) return tr("There is no record %1.").arg(q(draft_.trainee));
  return {};
}

QString PlannerController::saveNote() const {
  if (!open_) return {};
  const QString why = saveRefusal();
  if (!why.isEmpty()) return why;
  if (presetChanged(draft_)) return tr("Preset changed: Save keeps the old values as Custom.");
  const PlanScenario* s = scenarioOf(draft_.scenario);
  if (!draft_.id.empty() && s && s->hash != draft_.scenarioHash) return tr("Scenario changed: check the plan, then save it.");
  return dirty() ? tr("Not saved") : draft_.savedAt.empty() ? tr("Saved") : tr("Saved %1").arg(q(draft_.savedAt));
}

// ---- what the screens show

QVariantList PlannerController::plans() const {
  QVariantList l;
  if (!ready()) return l;
  for (const Plan& p : content_.plans) {
    if (filter_ == "anyone" ? !p.trainee.empty() : filter_ != "all" && filter_ != q(p.trainee)) continue;
    if (!search_.isEmpty() && !q(p.name).contains(search_, Qt::CaseInsensitive) && !q(p.trainee).contains(search_, Qt::CaseInsensitive)) continue;
    const State s = stateOf(p);
    l.append(QVariantMap{{"id", q(p.id)}, {"name", q(p.name)}, {"who", traineeText(p) + "  " + modeText(p)}, {"runs", runsText(p)},
                         {"state", s.text}, {"level", s.level}, {"selected", open_ && p.id == draft_.id}});
  }
  return l;
}

QVariantList PlannerController::scenarios() const {
  QVariantList l;
  for (const PlanScenario& s : content_.scenarios) {
    const QString note = !s.errors.empty() ? tr("%1 errors").arg(s.errors.size()) : s.placeholder ? tr("PLACEHOLDER") : tr("Validated");
    l.append(QVariantMap{{"id", q(s.id)}, {"text", q(s.name)}, {"note", note}, {"off", !s.errors.empty()}});
  }
  return l;
}

QVariantList PlannerController::options(Field f) const {
  QVariantList l;
  const PlanScenario* s = scenarioOf(draft_.scenario);
  if (!s) return l;
  QStringList seen;
  if (s->tooMany) {  // the list is cut: every value of the pools, unmarked; fits() judges the whole (10 section 8)
    if (f == Field::Aircraft)
      for (const PlanValues& c : s->aircraft) l.append(QVariantMap{{"id", q(c.callsign) + "  " + q(c.type)}, {"text", q(c.callsign) + "  " + q(c.type)}, {"note", QString()}});
    else
      for (const std::string& x : f == Field::Runway ? s->runways : s->taxiways) l.append(QVariantMap{{"id", q(x)}, {"text", q(x)}, {"note", QString()}});
    return l;
  }
  for (const PlanValues& c : s->combinations) {
    const QString id = f == Field::Aircraft ? q(c.callsign) + "  " + q(c.type) : q(f == Field::Runway ? c.runway : c.taxiway);
    if (seen.contains(id)) continue;
    seen << id;
    PlanValues v = draft_.values;
    if (f == Field::Aircraft) v.callsign = c.callsign, v.type = c.type;
    else (f == Field::Runway ? v.runway : v.taxiway) = f == Field::Runway ? c.runway : c.taxiway;
    l.append(QVariantMap{{"id", id}, {"text", id}, {"note", agree(v, *s, true, true, true) ? QString() : tr("does not fit the others")}});
  }
  return l;
}
QVariantList PlannerController::aircraft() const { return options(Field::Aircraft); }
QVariantList PlannerController::runways() const { return options(Field::Runway); }
QVariantList PlannerController::taxiways() const { return options(Field::Taxiway); }

QVariantList PlannerController::presets() const {
  QVariantList l;
  for (const PlanDifficulty& d : editPresets_.empty() ? content_.presets : editPresets_)
    l.append(QVariantMap{{"name", presetName(d.preset)}, {"from", seconds(d.replyFromMs)}, {"to", seconds(d.replyToMs)}, {"radio", q(d.radio)}});
  return l;
}

QVariantMap PlannerController::plan() const {
  if (!open_) return {};
  const PlanScenario* s = scenarioOf(draft_.scenario);
  const PlanDifficulty& d = draft_.difficulty;
  const bool may = s && s->errors.empty() && !s->placeholder && !draft_.trainee.empty();
  return {{"id", q(draft_.id)}, {"name", q(draft_.name)}, {"scenarioId", q(draft_.scenario)},
          {"scenario", s ? q(s->name) : q(draft_.scenario)},
          {"scenarioNote", !s ? tr("no such scenario") : tr("%1 steps   %2").arg(s->steps).arg(q(s->difficulty))},
          {"placeholder", s && s->placeholder}, {"trainee", traineeText(draft_)}, {"traineeId", q(draft_.trainee)},
          {"assessed", draft_.assessed}, {"assessedOn", may},
          {"assessedNote", may ? QString() : s && !s->placeholder ? tr("Assessed needs a trainee") : tr("Assessed: scenario not Validated")},
          {"briefing", q(draft_.briefing)}, {"briefingLeft", tr("%1 of %2").arg(q(draft_.briefing).size()).arg(kBriefing)},
          {"fixed", draft_.fixed},
          {"aircraft", draft_.values.callsign.empty() ? QString() : q(draft_.values.callsign) + "  " + q(draft_.values.type)},
          {"runway", q(draft_.values.runway)}, {"taxiway", q(draft_.values.taxiway)},
          {"aircraftBad", misfit(Field::Aircraft)}, {"runwayBad", misfit(Field::Runway)}, {"taxiwayBad", misfit(Field::Taxiway)},
          {"preset", int(d.preset)}, {"presetChanged", presetChanged(draft_)},
          {"from", seconds(d.replyFromMs)}, {"to", seconds(d.replyToMs)}, {"radio", q(d.radio)},
          {"events", d.preset == PlanPreset::Normal ? tr("Occasional \"say again\": not built. Events: not built") : tr("Events: not built")},
          {"isNew", draft_.id.empty()}};
}

QVariantMap PlannerController::card(const PlanChoice& c) {
  return {{"plan", q(c.plan.id)}, {"name", q(c.plan.name)}, {"planNote", tr("plan, %1").arg(c.scenarioName.empty() ? q(c.plan.scenario) : q(c.scenarioName))},
          {"planLine", planLine(c.plan)}, {"briefing", q(c.plan.briefing)},
          {"callsign", q(c.values.callsign)}, {"type", q(c.values.type)}, {"runway", q(c.values.runway)}, {"taxiway", q(c.values.taxiway)}};
}

// The card as the Dashboard would show it: always the SAVED plan, never values of an edit (D-152).
QVariantMap PlannerController::preview() const {
  if (!open_) return {};
  const Plan* saved = find(draft_.id);
  if (!saved) return {{"saved", false}, {"unsaved", true}};
  const PlanScenario* s = scenarioOf(saved->scenario);
  PlanChoice c;
  c.plan = *saved;
  if (saved->fixed) c.values = saved->values;
  c.scenarioName = s ? s->name : saved->scenario;
  QVariantMap m = card(c);
  m["saved"] = true;
  m["unsaved"] = dirty();
  m["drawn"] = !saved->fixed;
  m["placeholder"] = s && s->placeholder;
  return m;
}

QVariantList PlannerController::checks() const {
  QVariantList l;
  if (!open_) return l;
  const auto add = [&](int level, const QString& text) { l.append(QVariantMap{{"level", level}, {"text", text}}); };
  const PlanScenario* s = scenarioOf(draft_.scenario);
  if (!s) add(L::Fault, tr("No such scenario"));
  else if (!s->errors.empty()) add(L::Fault, tr("Scenario: %1 errors").arg(s->errors.size()));
  else if (!draft_.id.empty() && s->hash != draft_.scenarioHash) add(L::Caution, tr("Scenario changed"));
  else add(L::Ok, tr("Scenario loads"));
  if (s && s->errors.empty()) {
    using C = PlanCoverage::State;
    if (coverage_.state == C::Checking) add(L::None, tr("Voice bank: checking"));
    else if (coverage_.state == C::Ok) add(L::Ok, tr("Voice bank: %1 clips").arg(coverage_.clips));
    else if (coverage_.state == C::Missing) add(L::Fault, tr("Voice bank: %1 clips missing").arg(coverage_.missing));
    else add(L::Fault, coverage_.error.empty() ? tr("Voice bank: check failed") : tr("Voice bank: check failed: %1").arg(q(coverage_.error)));
    const auto n = s->combinations.size();
    const Fit f = draftFit();
    if (draft_.fixed && f == Fit::Yes) add(L::Ok, tr("Values fit the pools"));
    else if (draft_.fixed && f == Fit::No) add(L::Fault, fitWhy().isEmpty() ? tr("Values do not fit") : tr("Values do not fit: %1").arg(fitWhy()));
    else if (draft_.fixed && f == Fit::Checking) add(L::None, tr("Values: checking"));
    else if (draft_.fixed) add(L::Caution, tr("Values not chosen"));
    else if (s->tooMany) add(L::Ok, tr("Random of more than %1 combinations").arg(n));
    else add(n > 1 ? L::Ok : n == 1 ? L::Caution : L::Fault, n == 1 ? tr("Only one combination: no change") : tr("Random of %1 combinations").arg(n));
    add(s->placeholder ? L::Caution : L::Ok, s->placeholder ? tr("Text is PLACEHOLDER") : tr("Text is Validated"));
  }
  if (draft_.trainee.empty()) add(draft_.assessed ? L::Fault : L::Ok, draft_.assessed ? tr("Assessed needs a trainee") : tr("Anyone may run it"));
  else add(known(draft_.trainee) ? L::Ok : L::Fault, known(draft_.trainee) ? tr("Trainee record %1").arg(q(draft_.trainee)) : tr("No record %1").arg(q(draft_.trainee)));
  if (presetChanged(draft_)) add(L::Caution, tr("Preset changed"));
  return l;
}

bool PlannerController::canPut() const {
  const Plan* p = find(draft_.id);
  return ready() && open_ && p && !dirty() && stateOf(*p).start && !busy() && !choosing_ && !trainerBusy_;
}

QVariantList PlannerController::choices() const {
  QVariantList l;
  if (!loaded_) return l;
  for (const Plan& p : content_.plans) {
    if (!mayChoose(p)) continue;
    const PlanScenario* sc = scenarioOf(p.scenario);
    const State s = stateOf(p);
    l.append(QVariantMap{{"id", q(p.id)}, {"text", q(p.name)}, {"note", QStringList{sc ? q(sc->name) : q(p.scenario), traineeText(p), modeText(p), s.text}.join("   ")},
                         {"off", !s.start}});
  }
  return l;
}

// ---- requests

void PlannerController::search(const QString& words) {
  search_ = words.trimmed();
  emit listChanged();
}

void PlannerController::setFilter(const QString& f) {
  filter_ = f;
  emit listChanged();
}

void PlannerController::select(const QString& id) {
  if (!allowed() || (open_ && q(draft_.id) == id) || !leaveEdit()) return;
  const Plan* p = find(id.toStdString());
  if (!p) return;
  clearMessage();
  open(*p);
}

void PlannerController::newPlan() {
  if (!allowed() || !leaveEdit()) return;
  Plan p;
  p.fixed = false;
  const auto s = std::find_if(content_.scenarios.begin(), content_.scenarios.end(), [](const PlanScenario& x) { return x.errors.empty(); });
  if (s != content_.scenarios.end()) {
    p.scenario = s->id;
    p.scenarioHash = s->hash;
  }
  if (const PlanDifficulty* d = presetOf(PlanPreset::Normal)) p.difficulty = *d;
  clearMessage();
  open(p);
}

void PlannerController::copyPlan() {
  if (!edit() || !leaveEdit()) return;
  Plan p = draft_;
  p.id.clear();
  p.savedAt.clear();
  p.lastRun.clear();
  p.runs = 0;
  p.name = oneLine(tr("%1 (copy)").arg(q(p.name)), kName).toStdString();
  clearMessage();
  open(p);
}

void PlannerController::touched() {
  if (messageLevel_ != L::None) clearMessage();
  askFits();
  emit planChanged();
}

void PlannerController::setName(const QString& t) {
  if (!edit()) return;
  draft_.name = oneLine(t, kName).toStdString();
  touched();
}

void PlannerController::setScenario(const QString& id) {
  if (!edit()) return;
  const PlanScenario* s = scenarioOf(id.toStdString());
  if (!s || !s->errors.empty() || s->id == draft_.scenario) return;
  draft_.scenario = s->id;
  draft_.scenarioHash = s->hash;
  last_ = Field::None;
  check();
  touched();
}

void PlannerController::setTrainee(const QString& id) {
  if (!edit()) return;
  draft_.trainee = id.toStdString();
  touched();
}

void PlannerController::setAssessed(bool on) {
  if (!edit()) return;
  const PlanScenario* s = scenarioOf(draft_.scenario);
  if (on && (!s || s->placeholder || draft_.trainee.empty())) return;  // the screen shows the reason
  draft_.assessed = on;
  touched();
}

void PlannerController::setBriefing(const QString& t) {
  if (!edit()) return;
  draft_.briefing = oneLine(t, kBriefing).toStdString();
  touched();
}

void PlannerController::setFixed(bool on) {
  if (!edit()) return;
  draft_.fixed = on;
  touched();
}

void PlannerController::setAircraft(const QString& id) {
  const PlanScenario* s = scenarioOf(draft_.scenario);
  if (!edit() || !s) return;
  for (const PlanValues& c : s->tooMany ? s->aircraft : s->combinations)
    if (q(c.callsign) + "  " + q(c.type) == id) {
      draft_.values.callsign = c.callsign;
      draft_.values.type = c.type;
      last_ = Field::Aircraft;
      return touched();
    }
}

void PlannerController::setRunway(const QString& id) {
  if (!edit()) return;
  draft_.values.runway = id.toStdString();
  last_ = Field::Runway;
  touched();
}

void PlannerController::setTaxiway(const QString& id) {
  if (!edit()) return;
  draft_.values.taxiway = id.toStdString();
  last_ = Field::Taxiway;
  touched();
}

void PlannerController::setPreset(int preset) {
  if (!edit() || preset < int(PlanPreset::Easy) || preset > int(PlanPreset::Custom)) return;
  const PlanDifficulty* d = presetOf(PlanPreset(preset));
  if (d) draft_.difficulty = *d;
  else draft_.difficulty.preset = PlanPreset(preset);  // Custom keeps the values
  touched();
}

void PlannerController::useNewPreset() {
  if (presetChanged(draft_)) setPreset(int(draft_.difficulty.preset));
}

void PlannerController::stepReply(bool from, int steps) {
  if (!edit()) return;
  const PlanDifficulty was = draft_.difficulty;
  stepDelay(draft_.difficulty, from, steps);
  if (draft_.difficulty == was) return;
  draft_.difficulty.preset = PlanPreset::Custom;
  touched();
}

void PlannerController::setRadio(const QString& r) {
  if (!edit() || q(draft_.difficulty.radio) == r) return;
  draft_.difficulty.radio = r.toStdString();
  draft_.difficulty.preset = PlanPreset::Custom;
  touched();
}

void PlannerController::save() {
  if (!edit()) return;
  if (!canSave()) return (void)say(L::Caution, saveRefusal());
  Plan p = draft_;
  p.name = q(p.name).trimmed().toStdString();
  p.scenarioHash = scenarioOf(p.scenario)->hash;
  if (presetChanged(p)) p.difficulty.preset = PlanPreset::Custom;  // its values are not the preset's any more
  if (!p.fixed) p.values = {};
  if (!b_.save) return (void)say(L::Caution, tr("This build cannot save plans."));
  ++waiting_;
  emit planChanged();
  b_.save(p, draftRevision_, [this](const std::string& id, const std::string& why) {
    queued([this, id, why] {
      --waiting_;
      if (!why.empty()) {
        say(L::Caution, q(why));
      } else {
        say(L::Ok, tr("Saved"));
        want_ = id;
        reload(true);
      }
      emit planChanged();
    });
  });
}

void PlannerController::undo() {
  if (!edit()) return;
  clearMessage();
  reload(true);  // also reads a file that changed on disk
}

void PlannerController::deletePlan() {
  if (!edit()) return;
  if (draft_.id.empty()) return undo();
  if (b_.remove) b_.remove(draft_.id, content_.revision, answer([this] { reload(true); }, tr("Plan deleted. Its results are kept.")));
}

void PlannerController::editPresets() {
  editPresets_ = content_.presets;
  emit planChanged();
}

void PlannerController::cancelPresets() {
  editPresets_.clear();
  emit planChanged();
}

void PlannerController::stepPreset(int preset, bool from, int steps) {
  if (!allowed() || preset < 0 || preset >= int(editPresets_.size())) return;
  stepDelay(editPresets_[std::size_t(preset)], from, steps);
  emit planChanged();
}

void PlannerController::setPresetRadio(int preset, const QString& r) {
  if (!allowed() || preset < 0 || preset >= int(editPresets_.size())) return;
  editPresets_[std::size_t(preset)].radio = r.toStdString();
  emit planChanged();
}

void PlannerController::startingPresets() {
  if (!allowed()) return;
  editPresets_ = content_.startingPresets;
  emit planChanged();
}

void PlannerController::savePresets() {
  if (!allowed() || !leaveEdit() || editPresets_.empty() || !b_.savePresets) return;
  b_.savePresets(editPresets_, content_.revision, answer([this] {
    editPresets_.clear();
    reload(true);
    emit presetsSaved();
  }, tr("Presets saved. Plans keep the values they were saved with.")));
}

void PlannerController::put() {
  if (allowed() && canPut()) ask(draft_.id);
}

void PlannerController::choose(const QString& id) {
  const Plan* p = find(id.toStdString());
  if (!p || trainerBusy_ || choosing_) return;
  if (!mayChoose(*p)) return (void)say(L::Caution, tr("This plan is for somebody else."));
  if (const State s = stateOf(*p); !s.start) return (void)say(L::Caution, s.text);
  ask(p->id);
}

void PlannerController::ask(const std::string& id) {
  if (!b_.choose) return (void)say(L::Caution, tr("This build cannot choose a plan."));
  const unsigned n = ++chooseNo_;
  choosing_ = true;
  emit planChanged();
  b_.choose(id, [this, n](PlanChoice c, const std::string& why) {  // the backend reads who is logged in itself
    queued([this, n, c = std::move(c), why] {
      if (n != chooseNo_) return;
      choosing_ = false;
      if (!why.empty()) say(L::Caution, q(why));
      else say(L::Ok, c.onlyCombination ? tr("On the Dashboard: %1. The pools allow only one combination.").arg(q(c.plan.name)) : tr("On the Dashboard: %1").arg(q(c.plan.name)));
      emit planChanged();
      if (why.empty()) emit chosen();
    });
  });
}
