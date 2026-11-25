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
  connect(&c_, &C::runAgainRequested, this, [this] {
    newSeed();
    prepare();
  });
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
  connect(feed, &StatusFeed::stopExerciseRequested, this, [this] { if (radio_.stop) radio_.stop(); });
  if (cfg_.check) return checked(cfg_.check(cfg_.scenarioFile));
  check_ = std::make_unique<ExerciseCheck>(cfg_.data, cfg_.voices, [this](const CheckResult& r) { ui([this, r] { checked(r); }); });
  check_->select(cfg_.scenarioFile);
}

void LiveExercise::checked(const CheckResult& r) {
  using S = CheckResult::State;
  ready_ = r.state == S::Covered ? r.ready : PreparedExercise{};
  QStringList lines;
  for (const auto& m : r.missing) lines << q(m);
  for (const auto& e : r.errors) lines << q(toString(e));
  if (ready_) newSeed();
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

// The Start dialog.
void LiveExercise::prepare() {
  const auto p = c_.phase();
  if (!ready_ || (p != ExerciseController::Idle && p != ExerciseController::Result)) return;
  view_.prepare({.exercise = ready_.exercise, .seed = seed_,
                 .model = stt_ ? stt_->nameOf(cfg_.model) : q(cfg_.model), .consent = access_ && access_->consent()});
  emit c_.screenRequested("ex");
}

void LiveExercise::start(bool assessed) {
  if (starting_ || !ready_ || !radio_.start) return;
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
  newSeed();
}

void LiveExercise::leave() {
  c_.begin({}, false, false);
  emit c_.screenRequested("dash");
}
