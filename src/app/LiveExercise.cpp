#include "app/LiveExercise.h"

#include <QFileInfo>
#include <random>
#include <utility>


using namespace atc;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Below 2^53, so that the seed survives any JSON reader.
std::uint64_t randomBits() {
  std::random_device d;
  return (std::uint64_t(d()) << 21) ^ d();
}
}  // namespace

LiveExercise::LiveExercise(LiveExerciseConfig cfg, SttController* stt, LabController* lab, AccessController* access, SettingsController* settings, QObject* parent)
    : QObject(parent), cfg_(std::move(cfg)), stt_(stt), lab_(lab), access_(access), settings_(settings) {
  using C = ExerciseController;
  connect(&c_, &C::startRequested, this, &LiveExercise::start);
  connect(&c_, &C::cancelStartRequested, this, [this] { if (!starting_) leave(); });
  connect(&c_, &C::stopRequested, this, [this] { if (radio_.stop) radio_.stop(); });
  connect(&c_, &C::continueRequested, this, [this] { if (radio_.resume) radio_.resume(); });
  connect(&c_, &C::screenPttRequested, this, [this] { if (radio_.useFallback) radio_.useFallback(); });
  connect(&c_, &C::resultRequested, this, [this] { view_.showResult(); });
  connect(&c_, &C::runAgainRequested, this, &LiveExercise::prepare);  // with the values drawn at the end, which the card shows (D-321)
  connect(&c_, &C::leaveRequested, this, &LiveExercise::leave);
  second_.setInterval(1000);
  connect(&second_, &QTimer::timeout, this, [this] { view_.tick(monotonicNow()); });
}

LiveExercise::~LiveExercise() = default;

ExerciseRuntime::Callbacks LiveExercise::callbacks() {
  ExerciseRuntime::Callbacks cb;
  cb.started = [this](bool ok, const std::string& why) {
    ui([this, ok, why] {
      starting_ = false;
      if (!ok) return view_.refused(q(why));
      if (plan_ && planner_) planner_->started(*plan_, session_);  // counted from the start (PB-1)
      if (status_.running) status_.running(true);
      view_.started(assessed_, startedAt_);
      second_.start();
    });
  };
  cb.input = [this](Ns t, const EngineInput& i) { ui([this, t, i] { view_.input(t, i); }); };
  cb.output = [this](Ns t, const EngineOutput& o) { ui([this, t, o] { view_.output(t, o); }); };
  cb.ended = [this](const ExerciseResult& r) { ui([this, r] { ended(r); }); };
  cb.fault = [this](const std::string& why) { ui([this, why] { view_.fault(q(why)); }); };
  return cb;
}

void LiveExercise::connectRadio(ExerciseRadio radio, ExerciseStatus status, StatusFeed* feed) {
  radio_ = std::move(radio);
  status_ = std::move(status);
  connect(feed, &StatusFeed::startExerciseRequested, this, &LiveExercise::prepare);
  connect(feed, &StatusFeed::stopExerciseRequested, &c_, &ExerciseController::stop);
  if (access_) connect(access_.data(), &AccessController::confirmed, &c_, &ExerciseController::endConfirmed);
  connect(&c_, &ExerciseController::phaseChanged, this, [this] {
    if (recheck_) ui([this] { if (recheck_) recheck(); });  // later: while an exercise begins the phase is idle for a moment
  });
  if (cfg_.check) return checked(cfg_.check(scenarioFile()));
  check_ = std::make_unique<ExerciseCheck>(cfg_.data, cfg_.voices, [this](const CheckResult& r) { ui([this, r] { checked(r); }); }, cfg_.mayUse, cfg_.phraseology);
  check_->select(scenarioFile());
}

bool LiveExercise::active() const {
  const auto p = c_.phase();
  return starting_ || (p != ExerciseController::Idle && p != ExerciseController::Result);
}

void LiveExercise::recheck() {
  if (!cfg_.check && !check_) return;
  recheck_ = active();
  if (recheck_) return;
  if (cfg_.check) checked(cfg_.check(scenarioFile()));
  else check_->select(scenarioFile());
}

void LiveExercise::checked(const CheckResult& r) {
  using S = CheckResult::State;
  ready_ = r.state == S::Covered ? r.ready : PreparedExercise{};
  QStringList lines;
  for (const auto& m : r.missing) lines << q(m);
  for (const auto& e : r.errors) lines << q(toString(e));
  if (ready_ && plan_) showPlan();
  else if (ready_) newSeed();
  else if (status_.scenario) status_.scenario({{"name", QFileInfo(q(r.scenarioFile)).completeBaseName()}, {"placeholder", true}});
  if (!status_.bank) return;
  status_.bank(r.state == S::Checking ? BankReady::Checking : r.state == S::Covered ? BankReady::CoverageOk
               : r.state == S::MissingClips ? BankReady::MissingClips : BankReady::Failed,
               lines);
}

void LiveExercise::newSeed() {
  if (!ready_) return;
  seed_ = randomBits();
  if (status_.scenario) status_.scenario(ExerciseView::card(*ready_.exercise, seed_));
}

void LiveExercise::showPlan() {
  if (!ready_ || !plan_ || !status_.scenario) return;
  QVariantMap m = ExerciseView::card(*ready_.exercise, plan_->values);
  if (cfg_.planCard) {
    const QVariantMap keys = cfg_.planCard(*plan_);
    for (auto it = keys.begin(); it != keys.end(); ++it)
      if (it.key() != "callsign" && it.key() != "type" && it.key() != "runway" && it.key() != "taxiway") m[it.key()] = it.value();  // the values that run
  }
  status_.scenario(m);
}

void LiveExercise::usePlan(std::optional<PlanSelection> p) {
  if (active()) return;  // never under an exercise or its dialog
  const std::string before = scenarioFile();
  plan_ = std::move(p);
  if (scenarioFile() != before) return recheck();
  if (plan_) showPlan();
  else newSeed();
}

// The Start dialog.
void LiveExercise::prepare() {
  const auto p = c_.phase();
  if (!ready_ || (p != ExerciseController::Idle && p != ExerciseController::Result)) return;
  std::string why;
  std::optional<ExercisePlan> shown;
  if (plan_ && planner_) {
    why = planner_->beforeStart(*plan_);  // the person of now: run number, previous values (D-154)
    showPlan();
    shown = ExercisePlan{q(plan_->plan.name), q(plan_->plan.trainee), plan_->plan.assessed, plan_->run};
  }
  view_.prepare({.exercise = ready_.exercise, .seed = seed_,
                 .model = stt_ ? stt_->nameOf(cfg_.model) : q(cfg_.model), .trainee = access_ && access_->trainee() ? access_->personId() : QString(),
                 .consent = access_ && access_->consent(),
                 .values = plan_ ? std::optional<ParamValues>(plan_->values) : std::nullopt, .plan = shown});
  emit c_.screenRequested("ex");
  if (!why.empty()) view_.refused(q(why));
}

void LiveExercise::start(bool assessed) {
  if (starting_ || !ready_ || !radio_.start) return;
  if (plan_ && assessed != plan_->plan.assessed)
    return view_.refused(plan_->plan.assessed ? tr("This plan is Assessed.") : tr("This plan is Practice."));
  startedAt_ = QDateTime::currentDateTime();
  ExerciseOptions o;
  o.seed = seed_;
  o.mode = assessed ? ExerciseMode::Assessed : ExerciseMode::Practice;
  o.sessionId = startedAt_.toUTC().toString("yyyyMMdd'T'HHmmss'Z'").toStdString() + "-" + QString::number(randomBits() & 0xffff, 16).toStdString();
  o.startedMs = startedAt_.toMSecsSinceEpoch();
  if (access_ && access_->trainee()) o.traineeId = access_->personId().toStdString();
  o.model = cfg_.model;
  o.pttFallback = !cfg_.pttMissingBlocks || (settings_ && settings_->pttFallback());
  o.radio = cfg_.radio;
  if (plan_) {  // D-334: exactly what the card and the dialog show
    const PlanDifficulty& d = plan_->plan.difficulty;
    o.values = plan_->values;
    o.replyDelay = std::pair{ms(d.replyFromMs), ms(d.replyToMs)};
    if (const auto r = radioIntensityFromString(d.radio)) o.radio = *r;
    static const char* presets[] = {"easy", "normal", "hard", "custom"};
    o.plan = {plan_->plan.id, plan_->plan.name, presets[int(d.preset)], plan_->run};
  }
  session_ = o.sessionId;
  starting_ = true;  // before start(): started() may come at once
  assessed_ = assessed;
  const std::string why = radio_.start(ready_, o, cfg_.makeLog ? cfg_.makeLog() : std::make_unique<JsonlEventLog>(cfg_.logs));
  if (why.empty()) return;
  starting_ = false;
  view_.refused(q(why));
}

void LiveExercise::ended(const ExerciseResult& r) {
  second_.stop();
  if (status_.running) status_.running(false);
  view_.ended(r, QDateTime::currentDateTime());
  // No Lab result comes for a transmission of the exercise.
  if (lab_) lab_->setText({}, tr("Waiting"), false, {}, false);
  if (plan_ && planner_) {
    planner_->ended(*plan_);  // Random: new values, shown before they are used (PB-4)
    showPlan();
  } else {
    newSeed();
  }
}

void LiveExercise::leave() {
  c_.begin({}, false, false);
  emit c_.screenRequested("dash");
}
