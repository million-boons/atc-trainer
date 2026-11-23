#include "app/MockBackend.h"

#include <QTime>
#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>


using namespace atc;

namespace {
// PLACEHOLDER, from data/bench/placeholder_phrases.txt.
const QStringList kWords = QString("eagle four two one taxi via alpha three hold short runway zero nine").split(' ');
const QList<double> kConf{.93, .9, .88, .91, .97, .86, .9, .58, .95, .96, .98, .83, .61};
constexpr int kIdOffset = 13;  // seeded recordings use TX 8..13
constexpr int kRate = 16000;
const TxConfig kCfg;

class FakeEngine : public ISpeechRecognizer {
 public:
  std::string name() const override { return "mock"; }
  void begin() override {}
  void accept(std::span<const std::int16_t>) override {}
  SttResult finish() override {
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    SttResult r;
    r.text = kWords.join(' ').toStdString();
    for (int i = 0; i < kWords.size(); ++i) r.words.push_back({kWords[i].toStdString(), kConf[i]});
    return r;
  }
};
const char* const kScriptModel = "whisper-tiny-prompt";
}  // namespace

std::unique_ptr<ISpeechRecognizer> MockBackend::fakeEngine(const SttModel&, std::string&) {
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  return std::make_unique<FakeEngine>();
}

MockBackend::MockBackend(LabController* lab, SttController* stt, int pilotCutMs, QObject* parent)
    : QObject(parent), lab_(lab), stt_(stt), pilotCutMs_(pilotCutMs) {
  connect(lab, &LabController::pressRequested, this, [this](const QString& s) { userEdge(s, true); });
  connect(lab, &LabController::releaseRequested, this, [this](const QString& s) { userEdge(s, false); });
  connect(lab, &LabController::playRequested, this,
          [this] { lab_->setNotice(LabController::Caution, tr("Mock backend: no audio playback.")); });
  connect(lab, &LabController::exportRequested, this,
          [this] { lab_->setNotice(LabController::Caution, tr("Mock backend: export is not available.")); });
  const auto chip = [this] { lab_->setChip(LabController::Stt, stt_->stateText(), LabController::Level(stt_->level())); };
  connect(stt, &SttController::changed, this, chip);
  connect(stt, &SttController::result, this,
          [this](quint64 tx, const QString& model, const QString& config, const QString& text, const QStringList& words,
                 const QList<double>& conf, double sec) {
            if (tx >> 32 != epoch_) return;
            showFinal(tx & 0xffffffffu, text, words, conf, tr("%1  %2 s").arg(model).arg(sec, 0, 'f', 2), config);
          });
  timer_.setTimerType(Qt::PreciseTimer);
  timer_.setInterval(20);
  connect(&timer_, &QTimer::timeout, this, [this] {
    if (!frozen_) advanceTo(wallBase_ + wall_.nsecsElapsed());
  });
  run("idle");
  goLive();
  timer_.start();
  chip();
}

QVariantList MockBackend::scripts() const {
  const std::pair<const char*, const char*> all[] = {
      {"idle", "Idle"}, {"tx", "Transmitting"}, {"tail", "Tail"}, {"recog", "Recognising"}, {"good", "Result, good"},
      {"clipped", "Too loud"}, {"early", "Released early"}, {"before", "Spoke before keying"}, {"empty", "No speech"},
      {"tap", "Short tap"}, {"other", "Second source"}, {"stuck", "Stuck (30 s)"}, {"lost", "PTT unplugged"},
      {"pulse", "Pulse-type pedal"}, {"muted", "Mic muted"}, {"rx", "Pilot speaking"},
      {"cut", "PILOT CUT (interrupt)"}, {"blocked", "BLOCKED (rule block)"},
      {"queued", "WAIT (rule queue)"}, {"queueopen", "Queued, then TX"}, {"queuedrop", "Queued, dropped"}, {"queuetalk", "Queued, talked too soon"}, {"ptimeout", "Pilot voice timeout"},
      {"mloading", "Model LOADING"}, {"mfailed", "Model FAILED"}, {"mother", "Other model selected"}, {"mlocked", "Model locked"},
      {"vloaded", "Bank loaded"}, {"vok", "Bank coverage OK"}, {"vmissing", "Bank missing clips"}, {"vfailed", "Bank failed"}};
  QVariantList l;
  for (auto [id, name] : all) l.append(QVariantMap{{"id", id}, {"name", name}});
  return l;
}

void MockBackend::restart() {
  if (sttOpen_ && stt_->service()) stt_->service()->cancel();
  sttOpen_ = false;
  ++epoch_;
  now_ = lastMeter_ = 0;
  wallBase_ = 0;
  wall_.restart();
  script_.clear();
  speech_ = {};
  owner_.clear();
  held_ = stuck_ = pulse_ = muted_ = pilotHangs_ = false;
  present_ = true;
  curId_ = 0;
  finalAt_.reset();
  pilotEnd_.reset();
  cutUntil_.reset();

  ptt_.emplace(PttConfig{}, PttInputManager::Callbacks{
      [this](Ns t, const std::string& src) {
        held_ = true;
        stuck_ = false;
        owner_ = src;
        pressT_ = t;
        lab_->setNotice(LabController::None, {});
        lab_->setAlert(LabController::Ok, tr("Ready"));
        tx_->onPress(t);
      },
      [this](Ns t, const std::string& src, ReleaseReason why) {
        const QString s = QString::fromStdString(src);
        const Ns held = t - pressT_;
        held_ = false;
        if (why == ReleaseReason::DeviceLost)
          lab_->addEvent(t / 1e9, s, tr("lost"), tr("unplugged"), true);
        else
          lab_->addEvent(t / 1e9, s, tr("up"),
                         held < ms(50) ? tr("held %1 ms").arg(held / ms(1)) : tr("held %1 s").arg(held / 1e9, 0, 'f', 2), false);
        if (why == ReleaseReason::Normal && held >= ms(50) && src == "RIGHT") pulse_ = false;
        releaseT_ = t;
        tx_->onRelease(t, why);
      },
      [this](const std::string& src) {
        lab_->addEvent(sec(), QString::fromStdString(src), tr("ignored"), tr("%1 owns TX").arg(QString::fromStdString(owner_)), true);
        lab_->setNotice(LabController::Caution, tr("%1 ignored: %2 is already transmitting.")
                            .arg(QString::fromStdString(src), QString::fromStdString(owner_)));
      },
      [this](Ns held, const std::string& src) {
        pulse_ = true;
        lab_->addEvent(double(pressT_ + held) / 1e9, QString::fromStdString(src), tr("pulse"), tr("held %1 ms").arg(held / ms(1)), true);
      }});

  TxConfig cfg = kCfg;
  cfg.pressDuringPilot = rule_;
  tx_.emplace(cfg, TransmissionController::Callbacks{
      [this](std::uint64_t id, Ns) {
        curId_ = id;
        openT_ = tx_->openedAt();  // later than the press if it was queued
        lab_->addEvent(openT_ / 1e9, QString::fromStdString(owner_), openT_ == pressT_ ? tr("down") : tr("open"), tr("TX %1 open").arg(id + kIdOffset), false);
        if (openT_ != pressT_) lab_->setNotice(LabController::None, {});  // was queued
        lab_->showRecording(-1);
        lab_->setWords({}, {});
        sttOpen_ = !frozen_ && stt_->service() && stt_->service()->begin(wire(id));
        lab_->setText(frozen_ || sttOpen_ ? tr("Text appears after release") : tr("No speech model loaded: recording only"),
                      tr("Listening"), true, {}, false);
      },
      [this](std::uint64_t id, Ns from, Ns to, TxEnd end) { finish(id, from, to, end); },
      [this](std::uint64_t id) {
        if (sttOpen_) stt_->service()->cancel();
        sttOpen_ = false;
        lab_->addEvent(sec(), tr("TX %1").arg(id + kIdOffset), tr("cancel"), tr("short tap"), true);
        lab_->setText({}, tr("Waiting"), false, {}, false);
        if (pulse_) {
          lab_->setNotice(LabController::Fault, tr("Press was too short for a foot press: pulse-type pedal suspected. Hold-to-talk needs a hold-type pedal."));
          lab_->setAlert(LabController::Fault, tr("Pulse-type device suspected"));
        } else {
          lab_->setNotice(LabController::Caution, tr("Press was shorter than 250 ms. Not recorded."));
          lab_->setAlert(LabController::Caution, tr("Short tap cancelled"));
        }
      },
      [this](const Reply&, PlaybackId id) {
        playback_ = id;
        if (!pilotHangs_) pilotEnd_ = now_ + ms(6000);
        lab_->addEvent(sec(), tr("PILOT"), tr("start"), tr("mock speech"), false);
      },
      [this](const Reply&, PlaybackId, PilotStop why) {
        pilotEnd_.reset();
        if (why == PilotStop::Timeout) {
          lab_->addEvent(sec(), tr("PILOT"), tr("timeout"), tr("no end reported"), true);
          lab_->setNotice(LabController::Fault, tr("Pilot voice stopped: playback did not finish in time."));
          lab_->setAlert(LabController::Fault, tr("Pilot voice fault: stopped by the time limit"));
          return;
        }
        if (why == PilotStop::PressCameFirst) {
          lab_->addEvent(sec(), tr("PILOT"), tr("withdrawn"), tr("press came first"), false);
          return;
        }
        cutUntil_ = now_ + ms(pilotCutMs_);
        lab_->addEvent(sec(), tr("PILOT"), tr("cut"), tr("interrupted"), true);
      },
      [this](Ns t) {
        lab_->addEvent(t / 1e9, QString::fromStdString(owner_), tr("blocked"), tr("discarded"), true);
        lab_->setNotice(LabController::Caution, tr("Pilot is speaking: press discarded. Release, wait, then press again."));
        lab_->setAlert(LabController::Caution, tr("Press discarded: scenario rule is block"));
      },
      [this](Ns t) {
        lab_->addEvent(t / 1e9, QString::fromStdString(owner_), tr("queued"), tr("pilot talking"), true);
        lab_->setNotice(LabController::Caution, tr("Keep holding. Speak only when TX lights, after the pilot finishes."));
      },
      [this](Ns) {
        if (!frozen_) emit sideTone();
      },
      [this](Ns t) {
        lab_->addEvent(t / 1e9, QString::fromStdString(owner_), tr("dropped"), tr("queue ended"), true);
        lab_->setNotice(LabController::Caution, tr("Released before the pilot finished. Nothing was transmitted."));
      }});

  lab_->clear();
  const std::tuple<int, const char*, const char*, double, double, double, double> seeds[] = {
      {8, "10:38:20", "falcon seven three two climb flight level one zero zero", 2.5, .32, .35, 4},
      {9, "10:39:02", "the runway lights look fine today", 2.91, .32, .35, .292},
      {10, "10:39:40", "falcon three one eight contact tower one two one decimal nine", 4.7, .32, -.3, .292},
      {11, "10:40:12", "cobra eight nine zero taxi via alpha nine runway one eight", 3.82, .32, .35, .292},
      {12, "10:40:58", "", .4, .32, .35, 0},
      {13, "10:41:31", "hawk six seven four runway two seven cleared to land", 3.6, .32, .35, .292}};
  for (const auto& [id, time, text, held, start, endBefore, gain] : seeds) {
    speech_ = {start * 1000, endBefore * 1000, gain};
    const Ns p = ms(1000), r = p + Ns(held * 1e9);
    LabRecording rec;
    rec.id = id;
    rec.text = text;
    if (*text) rec.resultInfo = tr("Whisper tiny + prompt  0.34 s");
    rec.audio = synth(p - kCfg.preRoll, r + kCfg.tail, p, r);
    rec.checks = checksFor(rec.audio, int(kCfg.preRoll * kRate / 1'000'000'000), TxEnd::Normal).list;
    const double total = held + .5;
    rec.pressFrac = .3 / total;
    rec.endFrac = (.3 + held) / total;
    rec.endLabel = tr("RELEASE");
    rec.tailLabel = tr("tail");
    lab_->addRecording(std::move(rec), time);
  }
  speech_ = {};
  lab_->showRecording(-1);
  lab_->setChip(LabController::Speaker, tr("OK"), LabController::Ok);
  lab_->setVoiceBank(LabController::Bank::NotLoaded);
}

void MockBackend::at(double tMs, std::function<void()> f) { script_.emplace_back(Ns(tMs * 1e6), std::move(f)); }

void MockBackend::run(const QString& id) {
  rule_ = id == "blocked" ? PressRule::Block : id.startsWith("queue") ? PressRule::Queue : PressRule::Interrupt;
  frozen_ = true;  // scripted: never touches the speech service
  restart();
  SttStatus st;
  st.state = SttState::Ready;
  st.modelId = kScriptModel;
  if (id == "idle") st = SttStatus{};
  if (id == "recog") st.state = SttState::Busy;
  if (id == "mloading") st.state = SttState::Loading;
  if (id == "mfailed") { st.state = SttState::Failed; st.message = "not enough memory: 210 MB free, 325 MB needed"; }
  if (id == "mother") st.modelId = "vosk-grammar";
  if (id == "mlocked") st.locked = true;
  stt_->showScripted(st);
  if (id == "vloaded") lab_->setVoiceBank(LabController::Bank::Loaded);
  if (id == "vok") lab_->setVoiceBank(LabController::Bank::CoverageOk);
  if (id == "vmissing") lab_->setVoiceBank(LabController::Bank::MissingClips, 12);
  if (id == "vfailed") lab_->setVoiceBank(LabController::Bank::Failed);
  double freeze = 200;
  const auto talk = [&](double release) {
    at(1000, [this] { edge(1000, "RIGHT", true); });
    if (release > 0) at(release, [this, release] { edge(release, "RIGHT", false); });
  };
  const auto pilot = [&] { at(500, [this] { tx_->requestPilot({"mock", 0}, now_); }); };

  if (id == "tx") { talk(0); freeze = 4400; }
  else if (id == "tail") { talk(5100); freeze = 5200; }
  else if (id == "recog") { talk(5100); freeze = 5360; }
  else if (id == "good") { talk(5100); freeze = 5700; }
  else if (id == "clipped") { speech_.gain = 4; talk(5100); freeze = 5700; }
  else if (id == "early") { speech_.endBeforeReleaseMs = -400; talk(5100); freeze = 5700; }
  else if (id == "before") { speech_.startMs = -400; talk(5100); freeze = 5700; }
  else if (id == "empty") { speech_.gain = 0; talk(5100); freeze = 5700; }
  else if (id == "tap") { talk(1140); freeze = 1600; }
  else if (id == "other") { talk(0); at(2000, [this] { edge(2000, "SCREEN", true); }); freeze = 3100; }
  else if (id == "stuck") { talk(0); freeze = 31500; }
  else if (id == "lost") {
    talk(0);
    at(4000, [this] {
      present_ = false;
      ptt_->deviceLost(now_, "RIGHT");
      lab_->setNotice(LabController::Fault, tr("PTT disconnected. Reconnect it, or use Screen PTT."));
      lab_->setAlert(LabController::Fault, tr("PTT device lost during a transmission"));
    });
    freeze = 4700;
  }
  else if (id == "pulse") { talk(1001); freeze = 1400; }
  else if (id == "muted") {
    muted_ = true;
    lab_->setNotice(LabController::Fault, tr("Microphone sends pure silence. Check the mute key on the headset."));
    lab_->setAlert(LabController::Fault, tr("Mic: no signal (headset mute?)"));
  }
  else if (id == "rx") { pilot(); freeze = 1500; }
  else if (id == "ptimeout") { pilotHangs_ = true; pilot(); freeze = 21000; }
  else if (id == "cut") {
    pilot();
    at(2000, [this] { edge(2000, "RIGHT", true); });
    freeze = 3000;
  }
  else if (id == "blocked") {
    pilot();
    at(2000, [this] { edge(2000, "RIGHT", true); });
    freeze = 2800;
  }
  else if (id.startsWith("queue")) {
    pilot();
    at(2000, [this] { edge(2000, "RIGHT", true); });
    if (id == "queuetalk") {
      speech_.startMs = -400;
      at(9000, [this] { edge(9000, "RIGHT", false); });
    }
    if (id == "queuedrop") at(3000, [this] { edge(3000, "RIGHT", false); });
    freeze = id == "queuetalk" ? 9700 : id == "queueopen" ? 8500 : id == "queuedrop" ? 3500 : 2800;
  }
  advanceTo(Ns(freeze * 1e6));
  current_ = id;
  emit currentChanged();
}

void MockBackend::goLive() {
  frozen_ = false;
  stt_->showScripted(std::nullopt);
  wallBase_ = now_;
  wall_.restart();
}

void MockBackend::livePilot(const QString& rule) {
  run("idle");
  rule_ = pressRuleFromString(rule.toStdString()).value_or(PressRule::Interrupt);
  restart();
  goLive();
  tx_->requestPilot({"mock", 0}, now_);
  publish();
}

void MockBackend::userEdge(const QString& src, bool down) {
  if (frozen_) {
    if (!down) return;
    run("idle");
    goLive();
  }
  advanceTo(wallBase_ + wall_.nsecsElapsed());
  ptt_->rawEdge(now_, src.toStdString(), down);
  publish();
}

void MockBackend::advanceTo(Ns t) {
  while (now_ < t) {
    now_ = std::min(now_ + ms(10), t);
    for (std::size_t i = 0; i < script_.size();) {
      if (script_[i].first > now_) { ++i; continue; }
      auto f = std::move(script_[i].second);
      script_.erase(script_.begin() + std::ptrdiff_t(i));
      f();
    }
    ptt_->tick(now_);
    tx_->tick(now_);
    if (pilotEnd_ && now_ >= *pilotEnd_) {
      pilotEnd_.reset();
      lab_->addEvent(sec(), tr("PILOT"), tr("end"), tr("mock speech"), false);
      tx_->onPlaybackDone(playback_, now_);
    }
    if (cutUntil_ && now_ >= *cutUntil_) cutUntil_.reset();
    if (finalAt_ && now_ >= finalAt_->second) {
      const auto id = finalAt_->first;
      finalAt_.reset();
      showFinal(id, kWords.join(' '), kWords, kConf,
                tr("%1  %2 s").arg(stt_->nameOf(kScriptModel)).arg(double(now_ - releaseT_) / 1e9, 0, 'f', 2), "{\"mock\":\"scripted\"}");
    }
  }
  publish();
}

void MockBackend::finish(std::uint64_t id, Ns from, Ns to, TxEnd end) {
  LabRecording rec;
  rec.id = int(id) + kIdOffset;
  const Ns keyEnd = end == TxEnd::Stuck ? to : releaseT_;
  rec.audio = synth(from, to, openT_, end == TxEnd::Stuck ? std::nullopt : std::optional<Ns>(releaseT_));
  const LabChecks checks = checksFor(rec.audio, int((openT_ - from) * kRate / 1'000'000'000), end);
  rec.checks = checks.list;
  rec.pressFrac = double(openT_ - from) / double(to - from);
  rec.endFrac = double(keyEnd - from) / double(to - from);
  if (openT_ == from) rec.startLabel = tr("TX OPEN");  // queued: no pre-roll
  rec.endLabel = end == TxEnd::Stuck ? tr("30 s LIMIT") : end == TxEnd::DeviceLost ? tr("LOST") : tr("RELEASE");
  rec.tailLabel = end == TxEnd::Stuck ? tr("no tail") : tr("tail");
  lab_->addRecording(std::move(rec), QTime::currentTime().toString("HH:mm:ss"));

  if (end == TxEnd::Stuck) {
    stuck_ = true;
    lab_->addEvent(sec(), tr("TX %1").arg(id + kIdOffset), tr("stuck"), tr("at 30 s limit"), true);
    lab_->setNotice(LabController::Fault, tr("Transmission closed at 30 s. Lift your foot to transmit again."));
    lab_->setAlert(LabController::Fault, tr("PTT stuck: closed at maximum duration"));
  }
  if (checks.issue) {
    lab_->setNotice(checks.issue->level, checks.issue->notice);
    lab_->setAlert(LabController::Caution, tr("TX %1: %2").arg(id + kIdOffset).arg(checks.issue->title.toLower()));
  }
  const bool open = std::exchange(sttOpen_, false);
  if (checks.noSpeech || end != TxEnd::Normal) {  // nothing scored or recognised
    if (open) stt_->service()->cancel();
    lab_->setText({}, tr("Not run"), false, {}, false);
    tx_->onProcessed(id, std::nullopt, now_);
  } else {
    lab_->setText(tr("Processing"), tr("Processing"), true, {}, false);
    if (frozen_) {
      finalAt_ = {id, now_ + ms(140)};
    } else if (open) {
      stt_->service()->accept(synth(from, to, openT_, releaseT_));
      stt_->service()->finish(monotonicNow() - (now_ - releaseT_));  // virtual to real clock
    } else {
      lab_->setText({}, tr("Not run"), false, {}, false);
      lab_->setNotice(LabController::Caution, tr("No speech model was loaded: recorded, not recognised."));
      tx_->onProcessed(id, std::nullopt, now_);
    }
  }
}

void MockBackend::showFinal(std::uint64_t id, const QString& text, const QStringList& words, const QList<double>& conf,
                            const QString& info, const QString& config) {
  // A newer transmission may already be open: do not overwrite its text.
  if (id == curId_ && tx_->state() != TransmissionController::State::TxOpen) {
    lab_->setText(text, tr("Final"), false, info, true);
    lab_->setWords(words, conf);
  }
  lab_->setResult(int(id) + kIdOffset, text, info, config);
  tx_->onProcessed(id, std::nullopt, now_);
}

std::vector<std::int16_t> MockBackend::synth(Ns from, Ns to, Ns press, std::optional<Ns> release) const {
  std::vector<std::int16_t> a(std::size_t((to - from) * kRate / 1'000'000'000));
  const double t0 = double(from - press) / 1e9;
  const double s0 = speech_.startMs / 1e3;
  const double s1 = release ? double(*release - press) / 1e9 - speech_.endBeforeReleaseMs / 1e3 : 1e9;
  std::uint32_t rng = 12345;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double t = t0 + double(i) / kRate;
    rng = rng * 1664525u + 1013904223u;
    const double noise = (double(rng >> 16) / 32768.0 - 1.0);
    double v = muted_ ? 0 : 0.0025 * noise;
    if (!muted_ && t >= s0 && t < s1) {
      const double env = .25 + .75 * std::abs(std::sin(t * 5.3) * std::cos(t * 2.1 + 1));
      v += speech_.gain * env * (std::sin(2 * M_PI * 140 * t) + .45 * std::sin(2 * M_PI * 420 * t) + .15 * noise) / 1.35;
    }
    a[i] = std::int16_t(std::clamp(v, -1.0, 32767.0 / 32768.0) * 32768);
  }
  return a;
}

LabChecks MockBackend::checksFor(const std::vector<std::int16_t>& audio, int preRollSamples, TxEnd end) const {
  return labChecks(audio, preRollSamples, end, false, lab_->micLow, lab_->micHigh);
}

void MockBackend::publish() {
  using S = TransmissionController::State;
  const S s = tx_->state();
  lab_->setTx(txDisplay({.core = s, .pttHeld = held_, .closedAsStuck = stuck_, .blocked = tx_->pressBlocked(), .queued = tx_->pressQueued(),
                         .pttPresent = present_, .pulseSuspected = pulse_}),
              int(curId_) + kIdOffset);
  stt_->setTransmissionActive(s == S::TxOpen || s == S::TxTail || s == S::Recognising);
  lab_->setTxSeconds(s == S::TxOpen ? double(now_ - openT_) / 1e9 : held_ && stuck_ ? double(kCfg.maxDuration) / 1e9 : -1);
  lab_->setRx(s == S::PilotSpeaking);
  lab_->setPilotCut(cutUntil_.has_value());
  lab_->setButtons(held_ && owner_ == "RIGHT", false);
  lab_->setPttSource(!present_ ? tr("no device") : pulse_ ? tr("pulse device (mock)") : tr("mock stand-in"));
  lab_->setChip(LabController::Ptt, !present_ ? tr("LOST") : pulse_ ? tr("PULSE") : tr("STAND-IN"),
                !present_ || pulse_ ? LabController::Fault : LabController::Caution);
  lab_->setChip(LabController::Mic, muted_ ? tr("NO SIGNAL") : tr("OK"), muted_ ? LabController::Fault : LabController::Ok);

  if (now_ - lastMeter_ < ms(40)) return;  // 25 Hz
  lastMeter_ = now_;
  const bool open = s == S::TxOpen;
  const auto a = synth(now_ - ms(40), now_, open ? openT_ : now_ + ms(60'000), std::nullopt);
  int peak = 0;
  for (auto v : a) peak = std::max(peak, std::abs(int(v)));
  lab_->setMic(peak == 0 ? -120 : 20 * std::log10(peak / 32768.0));
}
