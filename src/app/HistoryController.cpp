#include "app/HistoryController.h"

#include <QDateTime>
#include <QLocale>
#include <QMetaObject>
#include <algorithm>
#include <utility>

using L = LabController;
using Kind = HistoryEntry::Kind;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QStringList list(const std::vector<std::string>& v) {
  QStringList l;
  for (const auto& s : v) l << q(s);
  return l;
}
QString date(std::int64_t ms) { return QLocale::c().toString(QDateTime::fromMSecsSinceEpoch(ms), "d MMM yyyy  HH:mm"); }
QString clock(std::int64_t ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm"); }
QString length(std::int64_t ms) { return QString("%1:%2").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')); }
QString seconds(int ms) { return ms < 0 ? QStringLiteral("-") : HistoryController::tr("%1 s").arg(ms / 1000.0, 0, 'f', 1); }
QString modeText(bool assessed) { return assessed ? HistoryController::tr("ASSESSED") : HistoryController::tr("PRACTICE"); }
QString endText(HistoryRow::End e) {
  return e == HistoryRow::End::Completed ? HistoryController::tr("completed") : e == HistoryRow::End::Stopped ? HistoryController::tr("stopped") : HistoryController::tr("fault");
}
int endLevel(HistoryRow::End e) { return e == HistoryRow::End::Completed ? L::Ok : e == HistoryRow::End::Stopped ? L::Caution : L::Fault; }
// What the instructor may say about an entry. The engine's verdict stays beside it.
// The backend stores the key; the screen shows the words.
struct Verdict { const char* key; const char* words; };
const Verdict kVerdicts[] = {{"trainee", QT_TRANSLATE_NOOP("HistoryController", "Trainee error")},
                             {"stt", QT_TRANSLATE_NOOP("HistoryController", "Speech recognition error")},
                             {"none", QT_TRANSLATE_NOOP("HistoryController", "Not an error")}};
}  // namespace

QString HistoryController::verdictWords(const std::string& key) {
  for (const Verdict& v : kVerdicts)
    if (key == v.key) return tr(v.words);
  return QString::fromStdString(key);  // unknown: as stored
}

HistoryController::HistoryController(AccessController* access, std::function<std::int64_t()> nowMs, QObject* parent)
    : QObject(parent), nowMs_(nowMs ? std::move(nowMs) : [] { return std::int64_t(QDateTime::currentMSecsSinceEpoch()); }) {
  setAccess(access);
}

void HistoryController::setAccess(AccessController* a) {
  if (access_) access_->disconnect(this);
  access_ = a;
  if (a) connect(a, &AccessController::changed, this, &HistoryController::accessChanged);
  accessChanged();
}

void HistoryController::queued(std::function<void()> f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }

bool HistoryController::say(int level, const QString& text) {
  messageLevel_ = text.isEmpty() ? int(L::None) : level;
  message_ = text;
  emit messageChanged();
  return false;
}

HistoryBackend::Done HistoryController::answer(std::function<void()> ok, const QString& doneText) {
  ++waiting_;
  emit detailChanged();
  return [this, ok = std::move(ok), doneText](const std::string& why) {
    queued([this, ok, doneText, why] {
      --waiting_;
      if (!why.empty()) say(L::Caution, q(why));
      else {
        say(L::Ok, doneText);
        if (ok) ok();
      }
      emit detailChanged();
    });
  };
}

// Somebody else is logged in: nothing of the one before stays on the screen.
void HistoryController::accessChanged() {
  const QString now = !allowed() ? QString() : instructor() ? QStringLiteral("instructor") : "t:" + access_->personId();
  if (now == who_) return;
  who_ = now;
  drop();
  if (allowed()) reload();
}

void HistoryController::drop() {
  ++loadNo_;
  ++openNo_;
  stopPlay();
  all_.clear();
  detail_.reset();
  selected_.clear();
  trainee_.clear();
  scenario_.clear();
  entry_ = -1;
  loaded_ = false;
  say(L::None, {});
  emit listChanged();
  emit detailChanged();
}

bool HistoryController::mine(const HistoryRow& r) const {
  if (r.trainee.empty()) return false;
  return instructor() || (access_ && access_->trainee() && q(r.trainee) == access_->personId());
}

void HistoryController::enter() {
  say(L::None, {});
  if (allowed()) reload();
}

void HistoryController::leave() { stopPlay(); }

void HistoryController::reload() {
  if (!b_.list) return;
  const unsigned n = ++loadNo_;
  b_.list(instructor() ? std::string() : access_->personId().toStdString(), [this, n](std::vector<HistoryRow> all, const std::string& why) {
    queued([this, n, all = std::move(all), why] {
      if (n != loadNo_ || !allowed()) return;
      all_.clear();
      for (const HistoryRow& r : all)
        if (mine(r)) all_.push_back(r);  // checked here too
      std::stable_sort(all_.begin(), all_.end(), [](const HistoryRow& a, const HistoryRow& b) { return a.startedMs > b.startedMs; });
      loaded_ = why.empty();
      if (!why.empty()) say(L::Fault, q(why));
      if (std::none_of(all_.begin(), all_.end(), [this](const HistoryRow& r) { return r.id == selected_; })) selected_.clear();
      emit listChanged();
    });
  });
}

// ---- list

std::vector<const HistoryRow*> HistoryController::shown() const {
  std::vector<const HistoryRow*> l;
  const std::int64_t from = days_ > 0 ? nowMs_() - std::int64_t(days_) * 86'400'000 : 0;
  for (const HistoryRow& r : all_) {
    if (!trainee_.isEmpty() && q(r.trainee) != trainee_) continue;
    if (mode_ != "all" && (mode_ == "assessed") != r.assessed) continue;
    if (!scenario_.isEmpty() && q(r.scenario) != scenario_) continue;
    if (r.startedMs < from) continue;
    l.push_back(&r);
  }
  return l;
}

QVariantList HistoryController::rows() const {
  QVariantList l;
  for (const HistoryRow* r : shown())
    l.append(QVariantMap{{"id", q(r->id)}, {"date", date(r->startedMs)}, {"trainee", q(r->trainee)},
                         {"scenario", r->run > 0 ? tr("%1  run %2").arg(q(r->plan.empty() ? r->scenario : r->plan)).arg(r->run) : q(r->scenario)},
                         {"mode", modeText(r->assessed)}, {"end", endText(r->end)}, {"level", endLevel(r->end)}, {"steps", tr("%1 / %2").arg(r->stepsDone).arg(r->steps)},
                         {"errors", r->errors}, {"missed", r->missed}, {"response", seconds(r->responseMs)}, {"suspected", r->suspected}, {"ptt", r->ptt},
                         {"reviewed", r->reclassified}, {"selected", r->id == selected_}});
  return l;
}

QString HistoryController::countText() const {
  const auto n = shown().size();
  return n == 1 ? tr("1 exercise") : tr("%1 exercises").arg(n);
}

QStringList HistoryController::trainees() const {
  QStringList l;
  if (!instructor()) return l;
  for (const HistoryRow& r : all_)
    if (!l.contains(q(r.trainee))) l << q(r.trainee);
  l.sort();
  return l;
}

QStringList HistoryController::scenarios() const {
  QStringList l;
  for (const HistoryRow& r : all_)
    if (!l.contains(q(r.scenario))) l << q(r.scenario);
  l.sort();
  return l;
}

QString HistoryController::trendNote() const {
  if (instructor() && trainee_.isEmpty()) return tr("Choose one trainee and one scenario.");  // H-5: trainees are not compared
  if (scenario_.isEmpty()) return tr("Choose one scenario.");
  return shown().empty() ? tr("No exercise.") : QString();
}

// Oldest first, at most the last 20. Suspected recognition errors have a row of their own.
QVariantList HistoryController::trend() const {
  if (!trendNote().isEmpty()) return {};
  auto l = shown();
  if (l.size() > std::size_t(kTrend)) l.resize(std::size_t(kTrend));
  std::reverse(l.begin(), l.end());
  QStringList errors, missed, response, stt;
  for (const HistoryRow* r : l) {
    errors << QString::number(r->errors);
    missed << QString::number(r->missed);
    response << (r->responseMs < 0 ? QStringLiteral("-") : QString::number(r->responseMs / 1000.0, 'f', 1));
    stt << QString::number(r->suspected);
  }
  const auto row = [](const QString& name, const QStringList& v) { return QVariantMap{{"name", name}, {"values", v}}; };
  return {row(tr("Trainee errors per exercise"), errors), row(tr("Missed responses"), missed), row(tr("Response, median, s"), response),
          row(tr("Suspected STT (not errors)"), stt)};
}

void HistoryController::setTrainee(const QString& id) {
  if (!instructor()) return;  // a trainee has only their own
  trainee_ = id;
  emit listChanged();
}

void HistoryController::setMode(const QString& m) {
  mode_ = m;
  emit listChanged();
}

void HistoryController::setScenario(const QString& name) {
  scenario_ = name;
  emit listChanged();
}

void HistoryController::setDays(int d) {
  days_ = d;
  emit listChanged();
}

void HistoryController::select(const QString& id) {
  selected_ = id.toStdString();
  emit listChanged();
}

// ---- one exercise

void HistoryController::openSelected() {
  if (busy() || std::none_of(all_.begin(), all_.end(), [this](const HistoryRow& r) { return r.id == selected_; })) return;
  load(selected_);
}

void HistoryController::load(const std::string& id) {
  if (!b_.open) return;
  const unsigned n = ++openNo_;
  b_.open(id, [this, n](HistoryDetail d, const std::string& why) {
    queued([this, n, d = std::move(d), why] {
      if (n != openNo_ || !allowed()) return;
      if (!why.empty()) return void(say(L::Fault, q(why)));
      if (!mine(d.row)) return void(say(L::Caution, tr("This exercise is somebody else's.")));
      const bool again = detail_ && detail_->row.id == d.row.id;
      detail_ = d;
      if (!again) {
        entry_ = d.entries.empty() ? -1 : 0;
        entryFilter_ = QStringLiteral("all");
      }
      emit detailChanged();
    });
  });
}

void HistoryController::close() {
  ++openNo_;
  stopPlay();
  detail_.reset();
  entry_ = -1;
  say(L::None, {});
  emit detailChanged();
  reload();
}

QVariantMap HistoryController::head() const {
  if (!detail_) return {};
  const HistoryRow& r = detail_->row;
  return {{"trainee", q(r.trainee)}, {"scenario", r.run > 0 ? tr("%1  run %2").arg(q(r.plan.empty() ? r.scenario : r.plan)).arg(r.run) : q(r.scenario)},
          {"callsign", q(detail_->callsign)}, {"mode", modeText(r.assessed)},
          {"when", tr("%1 to %2").arg(date(r.startedMs), clock(r.endedMs))}, {"end", endText(r.end).toUpper()}, {"level", endLevel(r.end)},
          {"sub", tr("%1 of %2 steps   %3   fallback PTT used: %4   Model %5").arg(r.stepsDone).arg(r.steps).arg(length(r.endedMs - r.startedMs), detail_->fallbackPtt ? tr("yes") : tr("no"), q(detail_->model))},
          {"placeholder", r.placeholder},
          {"changed", r.scenarioChanged ? tr("Scenario changed since this run") : QString()}};
}

// Facts. The readback row is hidden (H-4); there is no score.
QVariantList HistoryController::cards() const {
  if (!detail_) return {};
  const HistoryRow& r = detail_->row;
  const auto row = [](const QString& n, const QString& v) { return QVariantMap{{"name", n}, {"value", v}}; };
  return {row(tr("Completion"), tr("%1 of %2").arg(r.stepsDone).arg(r.steps)),
          row(tr("Instructions"), r.errors == 0 ? tr("no error") : tr("%1 incorrect").arg(r.errors)),
          row(tr("Missed steps"), detail_->missedSteps.empty() ? tr("none") : list(detail_->missedSteps).join(", ")),
          row(tr("Missed responses"), QString::number(r.missed)),
          row(tr("Response"), r.responseMs < 0 ? QStringLiteral("-") : tr("median %1").arg(seconds(r.responseMs))),
          row(tr("PTT discipline"), r.ptt == 0 ? tr("no note") : tr("%1 notes").arg(r.ptt)),
          row(tr("Reclassified"), QString::number(r.reclassified))};
}

QStringList HistoryController::suspected() const { return detail_ ? list(detail_->suspectedNotes) : QStringList(); }
QStringList HistoryController::pttNotes() const { return detail_ ? list(detail_->pttNotes) : QStringList(); }

QVariantList HistoryController::entries() const {
  QVariantList l;
  if (!detail_) return l;
  for (std::size_t i = 0; i < detail_->entries.size(); ++i) {
    const HistoryEntry& e = detail_->entries[i];
    if ((entryFilter_ == "errors" && !e.error) || (entryFilter_ == "ptt" && e.kind != Kind::Ptt) || (entryFilter_ == "stt" && !e.suspected)) continue;
    const QString who = e.kind == Kind::Pilot ? tr("PILOT") : e.kind == Kind::Atc ? tr("ATC") : e.kind == Kind::Ptt ? tr("PTT") : tr("NOTE");
    QString out = q(e.outcome);
    if (e.suspected) out = out.isEmpty() ? tr("not counted") : tr("%1, not counted").arg(out);
    if (!e.reviews.empty()) out = tr("%1 (reclassified)").arg(out);
    l.append(QVariantMap{{"no", int(i)}, {"time", q(e.time)}, {"who", who}, {"text", q(e.text)}, {"outcome", out}, {"level", e.level}, {"selected", int(i) == entry_}});
  }
  return l;
}

QVariantMap HistoryController::entry() const {
  if (!detail_ || entry_ < 0 || entry_ >= int(detail_->entries.size())) return {};
  const HistoryEntry& e = detail_->entries[std::size_t(entry_)];
  const bool atc = e.kind == Kind::Atc;
  return {{"title", e.step.empty() ? q(e.time) : tr("%1   %2").arg(q(e.time), q(e.step))}, {"atc", atc}, {"heard", q(e.heard)}, {"parsed", q(e.parsed)}, {"expected", q(e.expected)},
          {"outcome", q(e.outcome)}, {"cls", q(e.cls)}, {"delay", q(e.delay)}, {"level", e.level},
          {"audio", e.audio}, {"audioNote", e.audio ? QString() : tr("no audio saved")},
          {"mayReclassify", atc && instructor()}, {"text", q(e.text)}};
}

QVariantList HistoryController::reviews() const {
  QVariantList l;
  if (!detail_ || entry_ < 0 || entry_ >= int(detail_->entries.size())) return l;
  for (const HistoryReview& r : detail_->entries[std::size_t(entry_)].reviews)
    l.append(QVariantMap{{"verdict", verdictWords(r.verdict)}, {"note", q(r.note)}, {"by", tr("%1, %2").arg(q(r.role), q(r.at))}});
  return l;
}

void HistoryController::setEntryFilter(const QString& f) {
  entryFilter_ = f;
  emit detailChanged();
}

void HistoryController::selectEntry(int no) {
  if (!detail_ || no < 0 || no >= int(detail_->entries.size())) return;
  stopPlay();
  entry_ = no;
  emit detailChanged();
}

void HistoryController::play(bool keyedOnly) {
  if (!entry()["audio"].toBool() || busy() || !b_.play) return;  // only if it was saved with consent
  b_.play(detail_->row.id, entry_, keyedOnly, answer({}));
}

void HistoryController::stopPlay() {
  if (b_.stopPlay) b_.stopPlay();
}

void HistoryController::reclassify(const QString& verdict, const QString& note) {
  if (!instructor()) return void(say(L::Caution, tr("Only the instructor may reclassify.")));
  if (busy() || !b_.reclassify || !entry()["mayReclassify"].toBool()) return;
  const auto v = std::find_if(std::begin(kVerdicts), std::end(kVerdicts), [&](const Verdict& x) { return tr(x.words) == verdict; });
  if (v == std::end(kVerdicts)) return void(say(L::Caution, tr("Choose a class.")));
  const std::string id = detail_->row.id;
  b_.reclassify(id, entry_, v->key, note.trimmed().left(kNote).toStdString(), answer([this, id] {
                  if (detail_ && detail_->row.id == id) load(id);
                  emit reclassified();
                }, tr("Stored beside the original.")));
}

void HistoryController::removeSelected() {
  if (!instructor()) return void(say(L::Caution, tr("Only the instructor may delete results.")));
  if (selected_.empty() || busy() || !b_.remove) return;
  b_.remove(selected_, answer([this] { reload(); }, tr("Deleted. The deletion is in the log.")));
}
