#include "app/ScenarioController.h"

#include <QMetaObject>
#include <algorithm>
#include <utility>

using L = LabController;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QStringList list(const std::vector<std::string>& v) {
  QStringList l;
  for (const auto& s : v) l << q(s);
  return l;
}
// The eight categories of the schema, with a short name for the filter.
const std::pair<const char*, const char*> kCategories[] = {{"departure", QT_TRANSLATE_NOOP("ScenarioController", "Dep")}, {"arrival", QT_TRANSLATE_NOOP("ScenarioController", "Arr")},
                                                           {"ground", QT_TRANSLATE_NOOP("ScenarioController", "Gnd")}, {"landing", QT_TRANSLATE_NOOP("ScenarioController", "Ldg")},
                                                           {"go-around", QT_TRANSLATE_NOOP("ScenarioController", "GA")}, {"emergency", QT_TRANSLATE_NOOP("ScenarioController", "Emg")},
                                                           {"rcf", QT_TRANSLATE_NOOP("ScenarioController", "RCF")}, {"abnormal", QT_TRANSLATE_NOOP("ScenarioController", "Abn")}};
}  // namespace

void ScenarioController::queued(std::function<void()> f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }

void ScenarioController::say(int level, const QString& text) {
  messageLevel_ = text.isEmpty() ? int(L::None) : level;
  message_ = text;
  emit messageChanged();
}

void ScenarioController::setTrainerBusy(bool on) {
  if (trainerBusy_ == on) return;
  trainerBusy_ = on;
  emit detailChanged();
}

void ScenarioController::enter() {
  say(L::None, {});
  if (!b_.load) return;
  const unsigned n = ++loadNo_;
  b_.load([this, n](std::vector<ScenarioInfo> all, const std::string& why) {
    queued([this, n, all = std::move(all), why] {
      if (n != loadNo_) return;
      loaded_ = why.empty();
      all_ = loaded_ ? all : std::vector<ScenarioInfo>{};
      coverage_.clear();  // the files may have changed
      if (!why.empty()) say(L::Fault, q(why));
      if (!find(open_)) open_ = all_.empty() ? std::string() : all_.front().id;
      if (!open_.empty()) check(open_);
      emit listChanged();
      emit detailChanged();
    });
  });
}

const ScenarioInfo* ScenarioController::find(const std::string& id) const {
  const auto at = std::find_if(all_.begin(), all_.end(), [&](const ScenarioInfo& s) { return s.id == id; });
  return id.empty() || at == all_.end() ? nullptr : &*at;
}

bool ScenarioController::shown(const ScenarioInfo& s) const {
  if (category_ != "all" && q(s.category) != category_) return false;
  if (difficulty_ != "all" && q(s.difficulty) != difficulty_) return false;
  return search_.isEmpty() || q(s.name).contains(search_, Qt::CaseInsensitive) || q(s.id).contains(search_, Qt::CaseInsensitive);
}

QString ScenarioController::countText() const {
  const auto n = std::count_if(all_.begin(), all_.end(), [this](const ScenarioInfo& s) { return shown(s); });
  return tr("%1 of %2").arg(n).arg(all_.size());
}

QVariantList ScenarioController::categories() const {
  QVariantList l{QVariantMap{{"id", "all"}, {"name", tr("All")}, {"off", false}, {"selected", category_ == "all"}}};
  for (const auto& [id, name] : kCategories) {
    const bool any = std::any_of(all_.begin(), all_.end(), [c = id](const ScenarioInfo& s) { return s.category == c; });
    l.append(QVariantMap{{"id", id}, {"name", tr(name)}, {"off", !any}, {"selected", category_ == id}});
  }
  return l;
}

QVariantList ScenarioController::rows() const {
  QVariantList l;
  for (const ScenarioInfo& s : all_) {
    if (!shown(s)) continue;
    int level = L::None;
    QString state = tr("Clips not checked");
    const auto c = coverage_.find(s.id);
    if (!s.errors.empty()) {
      level = L::Fault;
      state = tr("Cannot be loaded: %1 errors").arg(s.errors.size());
    } else if (c != coverage_.end()) {
      using S = ScenarioCoverage::State;
      const S st = c->second.state;
      level = st == S::Ok ? L::Ok : st == S::Checking ? L::None : L::Fault;
      state = st == S::Ok ? tr("Clips complete") : st == S::Checking ? tr("Checking the clips") : st == S::Missing ? tr("%1 clips missing").arg(c->second.missing.size()) : tr("Clip check failed");
    }
    l.append(QVariantMap{{"id", q(s.id)}, {"name", q(s.name)}, {"sub", tr("%1   %2   %3 steps").arg(q(s.category), q(s.difficulty)).arg(s.steps.size())},
                         {"tag", s.placeholder ? tr("PLACEHOLDER") : tr("VALIDATED")}, {"placeholder", s.placeholder},
                         {"level", level}, {"state", state}, {"selected", s.id == open_}});
  }
  return l;
}

QVariantMap ScenarioController::detail() const {
  const ScenarioInfo* s = find(open_);
  if (!s) return {};
  using S = ScenarioCoverage::State;
  const auto c = coverage_.find(open_);
  int level = L::None;
  QString bank = tr("Voice bank: not checked, the scenario cannot be loaded");
  if (s->errors.empty()) {
    const S st = c == coverage_.end() ? S::Checking : c->second.state;
    level = st == S::Ok ? L::Ok : st == S::Checking ? L::None : L::Fault;
    bank = st == S::Checking ? tr("Voice bank: CHECKING. Start waits for the check.")
           : st == S::Ok ? tr("Voice bank: coverage checked, %1 clips").arg(c->second.clips)
           : st == S::Missing ? tr("Voice bank: %1 clips missing. They block Start.").arg(c->second.missing.size()) : tr("Voice bank: the check failed");
  }
  return {{"id", q(s->id)}, {"name", q(s->name)}, {"placeholder", s->placeholder}, {"tag", s->placeholder ? tr("PLACEHOLDER") : tr("VALIDATED")},
          {"info", tr("%1   %2   %3   SOP reference: %4").arg(q(s->category), q(s->difficulty), q(s->airfield), s->sopRef.empty() ? tr("none") : q(s->sopRef))},
          {"stepsTitle", tr("STEPS   %1").arg(s->steps.size())}, {"rule", tr("Half-duplex: %1").arg(s->rule.empty() ? QStringLiteral("-") : q(s->rule))},
          {"values", tr("Callsign %1   Type %2   Runway %3   Taxiway %4").arg(s->callsigns).arg(s->types).arg(s->runways).arg(s->taxiways)},
          {"bankLevel", level}, {"bank", bank}, {"hasMissing", !missing().isEmpty()}, {"hasErrors", !s->errors.empty()},
          {"assessedLevel", int(s->placeholder || !s->errors.empty() ? L::Fault : L::Ok)},
          {"assessed", !s->errors.empty() ? tr("Assessed: not available, the scenario cannot be loaded") : s->placeholder ? tr("Assessed: not available, text is PLACEHOLDER") : tr("Assessed: available, needs a trainee")}};
}

// Practice shows all steps to everybody; those of a scenario that may be Assessed only to the instructor (C-1).
bool ScenarioController::stepsHidden() const {
  const ScenarioInfo* s = find(open_);
  return s && !s->placeholder && !instructor_;
}

QVariantList ScenarioController::steps() const {
  QVariantList l;
  if (stepsHidden()) return l;
  if (const ScenarioInfo* s = find(open_))
    for (const ScenarioStepInfo& x : s->steps)
      l.append(QVariantMap{{"no", l.size() + 1}, {"name", q(x.name)}, {"intent", q(x.intent)},
                           {"wait", x.waitSeconds > 0 ? tr("%1 s, %2 calls").arg(x.waitSeconds).arg(x.pilotCalls) : tr("no limit")}});
  return l;
}

QStringList ScenarioController::objectives() const {
  const ScenarioInfo* s = find(open_);
  return s ? list(s->objectives) : QStringList();
}

QStringList ScenarioController::errors() const {
  const ScenarioInfo* s = find(open_);
  return s ? list(s->errors) : QStringList();
}

QStringList ScenarioController::missing() const {
  const auto c = coverage_.find(open_);
  return c == coverage_.end() ? QStringList() : list(c->second.missing);
}

// Allowed while the clips are checked and if some are missing: they block Start, not the choice (5d).
bool ScenarioController::canSelect() const {
  const ScenarioInfo* s = find(open_);
  return s && s->errors.empty() && !trainerBusy_ && !selecting_ && b_.select;
}

void ScenarioController::setCategory(const QString& id) {
  category_ = id;
  emit listChanged();
}

void ScenarioController::setDifficulty(const QString& d) {
  difficulty_ = d;
  emit listChanged();
}

void ScenarioController::search(const QString& words) {
  if (search_ == words.trimmed()) return;
  search_ = words.trimmed();
  emit listChanged();
}

void ScenarioController::open(const QString& id) {
  const ScenarioInfo* s = find(id.toStdString());
  if (!s || s->id == open_) return;
  open_ = s->id;
  say(L::None, {});
  check(open_);
  emit listChanged();
  emit detailChanged();
}

void ScenarioController::check(const std::string& id) {
  const ScenarioInfo* s = find(id);
  if (!s || !s->errors.empty() || !b_.coverage) return;
  const auto known = coverage_.find(id);
  if (known != coverage_.end() && known->second.state != ScenarioCoverage::State::Failed) return;
  coverage_[id] = {};
  const unsigned n = loadNo_;
  b_.coverage(id, [this, n, id](ScenarioCoverage c) {
    queued([this, n, id, c = std::move(c)] {
      if (n != loadNo_) return;  // the files were read again meanwhile
      coverage_[id] = c;
      emit listChanged();
      emit detailChanged();
    });
  });
}

void ScenarioController::select() {
  if (!canSelect()) return;
  selecting_ = true;
  emit detailChanged();
  b_.select(open_, [this](const std::string& why) {
    queued([this, why] {
      selecting_ = false;
      say(L::Caution, q(why));
      emit detailChanged();
      if (why.empty()) emit selected();
    });
  });
}
