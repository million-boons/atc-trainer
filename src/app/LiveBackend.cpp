#include "app/LiveBackend.h"

#include <QTime>
#include <algorithm>
#include <chrono>
#include <utility>

#include "app/LabChecks.h"

using namespace atc;
using L = LabController;
using S = TransmissionController::State;

namespace {
constexpr int kRate = 16000;
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

LiveBackend::LiveBackend(LabController* lab, SttController* stt, LiveConfig cfg, QObject* parent)
    : QObject(parent), lab_(lab), stt_(stt), cfg_(std::move(cfg)), micLow_(lab->micLow), micHigh_(lab->micHigh),
      hasPtt_(!cfg_.pttDevice.isEmpty()), t0_(monotonicNow()), ring_(kRate, std::size_t(kRate) * 60) {
  lab_->setPttSource(hasPtt_ ? cfg_.pttName : tr("no PTT device set"));
  lab_->setChip(L::Speaker, tr("NOT CHECKED"), L::Caution);
  lab_->setChip(L::Ptt, hasPtt_ ? tr("OPENING") : tr("NOT SET"), L::Caution);
  lab_->setVoiceBank(L::Bank::NotLoaded);
  lab_->setText({}, tr("Waiting"), false, {}, false);

  connect(lab, &L::pressRequested, this, [this](const QString& s) {
    post([this, t = monotonicNow(), src = s.toStdString()] { ptt_->rawEdge(t, src, true); });
  });
  connect(lab, &L::releaseRequested, this, [this](const QString& s) {
    post([this, t = monotonicNow(), src = s.toStdString()] { ptt_->rawEdge(t, src, false); });
  });
  connect(lab, &L::playRequested, this, [this] { lab_->setNotice(L::Caution, tr("Playback is not built yet.")); });
  connect(lab, &L::exportRequested, this, [this] { lab_->setNotice(L::Caution, tr("Export is not built yet. Recordings stay in memory.")); });

  const auto chip = [this] { lab_->setChip(L::Stt, stt_->stateText(), L::Level(stt_->level())); };
  connect(stt, &SttController::changed, this, chip);
  chip();
  connect(stt, &SttController::result, this,
          [this](quint64 tx, const QString& model, const QString& config, const QString& text, const QStringList& words,
                 const QList<double>& conf, double sec) {
            const QString info = tr("%1  %2 s").arg(model).arg(sec, 0, 'f', 2);
            if (tx == guiTx_ && !guiOpen_) {  // never overwrite a newer transmission
              lab_->setText(text.isEmpty() ? tr("(nothing recognised)") : text, tr("Final"), text.isEmpty(), info, !text.isEmpty());
              lab_->setWords(words, conf);
            }
            lab_->setResult(int(tx), text, info, config);
            post([this, tx] { tx_->onProcessed(tx, std::nullopt, monotonicNow()); });
          });
}

LiveBackend::~LiveBackend() {
  right_.reset();
  leftSrc_.reset();
  cap_.reset();
  stop_ = true;
  cv_.notify_one();
  if (thread_.joinable()) thread_.join();
}

void LiveBackend::post(std::function<void()> f) {
  {
    std::lock_guard l(m_);
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
}

void LiveBackend::event(Ns t, const QString& src, const QString& edge, const QString& detail, bool warn) {
  ui([this, s = double(t - t0_) / 1e9, src, edge, detail, warn] { lab_->addEvent(s, src, edge, detail, warn); });
}

void LiveBackend::notice(L::Level l, const QString& n, L::Level al, const QString& a) {
  ui([=, this] {
    lab_->setNotice(l, n);
    lab_->setAlert(al, a);
  });
}

void LiveBackend::pttState(bool present, const QString& chip, L::Level l, const QString& why) {
  if (present == present_ && chip == pttChip_) return;
  present_ = present;
  pttChip_ = chip;
  ui([=, this] {
    lab_->setChip(L::Ptt, chip, l);
    if (!why.isEmpty()) lab_->setAlert(l, why);
  });
}

bool LiveBackend::start() {
  build();
  thread_ = std::thread([this] { run(); });

  CaptureConfig cc;
  cc.source = cfg_.micSource;
  cc.server = cfg_.pulseServer;
  cap_ = std::make_unique<PulseCapture>(cc, ring_, meter_, [this](const CaptureStatus& s) { post([this, s] { mic_ = s; }); });
  const bool ok = cap_->start();
  if (!ok) notice(L::Fault, tr("The microphone could not be started."), L::Fault, tr("Mic: not started"));

  if (hasPtt_) {
    EvdevConfig ec;
    ec.path = cfg_.pttDevice.toStdString();
    ec.grab = cfg_.pttGrab;
    right_ = std::make_unique<EvdevPttSource>(ec, EvdevPttSource::Callbacks{
        [this](Ns t, bool d) { post([=, this] { ptt_->rawEdge(t, "RIGHT", d); }); },
        [this](bool d) {
          post([=, this] {
            ptt_->setInitialState("RIGHT", d);
            event(monotonicNow(), "RIGHT", tr("ready"), d ? tr("held: release") : tr("device open"), d);
            pttState(true, tr("OK"), L::Ok, {});
            ui([this] { lab_->setAlert(L::Ok, tr("Ready")); });
          });
        },
        [this](Ns t) {
          post([=, this] {
            ptt_->deviceLost(t, "RIGHT");
            pttState(false, tr("LOST"), L::Fault, tr("PTT device lost"));
            notice(L::Fault, tr("PTT disconnected. Reconnect it, or use Screen PTT."), L::Fault, tr("PTT device lost"));
          });
        },
        [this](const std::string& e) { post([=, this] { pttState(false, pttChip_ == tr("LOST") ? pttChip_ : tr("NOT OPEN"), L::Fault, tr("PTT: %1").arg(q(e))); }); }});
    right_->start();
    if (!cfg_.pttGrab) {  // a grabbed device has only one reader
      EvdevConfig lc = ec;
      lc.code = 0x110;  // BTN_LEFT, reported only (D-01)
      leftSrc_ = std::make_unique<EvdevPttSource>(lc, EvdevPttSource::Callbacks{
          [this](Ns t, bool d) {
            post([=, this] {
              left_ = d;
              event(t, "LEFT", d ? tr("down") : tr("up"), tr("reported only"), false);
            });
          },
          [this](bool d) { post([=, this] { left_ = d; }); }, [this](Ns) { post([this] { left_ = false; }); }, {}});
      leftSrc_->start();
    }
  }
  return ok;
}

void LiveBackend::build() {
  ptt_.emplace(PttConfig{}, PttInputManager::Callbacks{
      [this](Ns t, const std::string& src) {
        held_ = true;
        stuck_ = false;
        owner_ = src;
        pressT_ = t;
        notice(L::None, {}, L::Ok, tr("Ready"));
        tx_->onPress(t);
      },
      [this](Ns t, const std::string& src, ReleaseReason why) {
        const Ns held = t - pressT_;
        held_ = false;
        if (why == ReleaseReason::DeviceLost) event(t, q(src), tr("lost"), tr("unplugged"), true);
        else event(t, q(src), tr("up"), held < ms(50) ? tr("held %1 ms").arg(double(held) / 1e6, 0, 'f', 1) : tr("held %1 s").arg(held / 1e9, 0, 'f', 2), false);
        if (why == ReleaseReason::Normal && held >= ms(50) && src == "RIGHT") pulse_ = false;
        releaseT_ = t;
        tx_->onRelease(t, why);
      },
      [this](const std::string& src) {
        event(monotonicNow(), q(src), tr("ignored"), tr("%1 owns TX").arg(q(owner_)), true);
        ui([this, n = tr("%1 ignored: %2 is already transmitting.").arg(q(src), q(owner_))] { lab_->setNotice(L::Caution, n); });
      },
      [this](Ns held, const std::string& src) {
        if (src == "RIGHT") pulse_ = true;  // a short tap on the screen says nothing about the pedal
        event(pressT_ + held, q(src), tr("pulse"), tr("held %1 ms").arg(double(held) / 1e6, 0, 'f', 1), true);
      }});

  tx_.emplace(txCfg_, TransmissionController::Callbacks{
      [this](std::uint64_t id, Ns from) {
        closeNow();  // an earlier transmission still waiting for its last audio
        curId_ = id;
        openT_ = from + txCfg_.preRoll;
        event(openT_, q(owner_), tr("down"), tr("TX %1 open").arg(id), false);
        cursor_ = ring_.indexAt(from);
        lost_ = false;
        streaming_ = stt_->service() && stt_->service()->begin(id);
        ui([this, id, on = streaming_] {
          guiTx_ = id;
          guiOpen_ = true;
          lab_->showRecording(-1);
          lab_->setWords({}, {});
          lab_->setText(on ? tr("Text appears after release") : tr("No speech model loaded: recording only"), tr("Listening"), true, {}, false);
        });
      },
      [this](std::uint64_t id, Ns from, Ns to, TxEnd end) {
        if (end == TxEnd::Stuck) {
          stuck_ = true;
          event(to, tr("TX %1").arg(id), tr("stuck"), tr("at 30 s limit"), true);
          notice(L::Fault, tr("Transmission closed at 30 s. Lift your foot to transmit again."), L::Fault, tr("PTT stuck: closed at maximum duration"));
        }
        closing_ = Closing{id, from, to, openT_, end, ring_.indexAt(to), end == TxEnd::Stuck ? to : releaseT_, monotonicNow() + ms(1000)};
      },
      [this](std::uint64_t id) {
        if (std::exchange(streaming_, false)) stt_->service()->cancel();
        event(monotonicNow(), tr("TX %1").arg(id), tr("cancel"), tr("short tap"), true);
        if (pulse_) notice(L::Fault, tr("Press was too short for a foot press: pulse-type pedal suspected. Hold-to-talk needs a hold-type pedal."), L::Fault, tr("Pulse-type device suspected"));
        else notice(L::Caution, tr("Press was shorter than 250 ms. Not recorded."), L::Caution, tr("Short tap cancelled"));
        ui([this] {
          guiOpen_ = false;
          lab_->setText({}, tr("Waiting"), false, {}, false);
        });
      },
      {}, [this](const Reply&) { cutUntil_ = monotonicNow() + ms(2000); }, {}, {},
      [this](Ns) { ui([this] { emit sideTone(); }); }, {}});
}

void LiveBackend::run() {
  while (!stop_) {
    std::vector<std::function<void()>> todo;
    {
      std::unique_lock l(m_);
      cv_.wait_for(l, std::chrono::milliseconds(5), [this] { return stop_ || !q_.empty(); });
      todo.swap(q_);
    }
    for (auto& f : todo) f();
    const Ns now = monotonicNow();
    ptt_->tick(now);
    tx_->tick(now);
    if (closing_ && (ring_.newestIndex() >= closing_->endIndex || now > closing_->deadline)) closeNow();
    else if (streaming_) feed(closing_ ? std::min(closing_->endIndex, ring_.newestIndex()) : ring_.newestIndex());
    if (now - lastPublish_ >= ms(40) || !todo.empty()) publish(now);
  }
}

void LiveBackend::feed(std::int64_t to) {
  if (to <= cursor_) return;
  auto c = ring_.extractIndex(cursor_, to);
  if (c.missingHead) lost_ = true;
  cursor_ += std::int64_t(c.missingHead + c.samples.size());
  if (!c.samples.empty()) stt_->service()->accept(std::move(c.samples));
}

void LiveBackend::closeNow() {
  if (!closing_) return;
  const Closing c = *std::exchange(closing_, std::nullopt);
  auto cut = ring_.extract(c.from, c.to);
  const bool gap = lost_ || cut.gap || cut.missingHead || cut.missingTail;
  // A missing head shortens the pre-roll; the press stays where it was.
  const int pre = std::max(0, int(txCfg_.preRoll * kRate / 1'000'000'000) - int(cut.missingHead));
  const LabChecks checks = labChecks(cut.samples, pre, c.end, gap, micLow_, micHigh_);
  const bool recognise = streaming_ && c.end == TxEnd::Normal && !checks.noSpeech;
  if (recognise) {
    feed(c.endIndex);
    stt_->service()->finish(c.release);
  } else if (streaming_) {
    stt_->service()->cancel();
  }
  const bool hadModel = std::exchange(streaming_, false);

  auto rec = std::make_shared<LabRecording>();
  const double n = std::max<double>(1, double(cut.samples.size()));
  const double keyed = double(c.release - c.press) / 1e9 * kRate;
  rec->id = int(c.id);
  rec->checks = checks.list;
  rec->pressFrac = std::clamp(pre / n, 0.0, 1.0);
  rec->endFrac = std::clamp((pre + keyed) / n, 0.0, 1.0);
  rec->endLabel = c.end == TxEnd::Stuck ? tr("30 s LIMIT") : c.end == TxEnd::DeviceLost ? tr("LOST") : tr("RELEASE");
  rec->tailLabel = c.end == TxEnd::Stuck ? tr("no tail") : tr("tail");
  rec->audio = std::move(cut.samples);
  ui([this, rec, recognise, hadModel, issue = checks.issue, noSpeech = checks.noSpeech, normal = c.end == TxEnd::Normal, id = c.id] {
    if (id == guiTx_) guiOpen_ = false;
    lab_->addRecording(std::move(*rec), QTime::currentTime().toString("HH:mm:ss"));
    if (issue) {
      lab_->setNotice(issue->level, issue->notice);
      lab_->setAlert(L::Caution, tr("TX %1: %2").arg(id).arg(issue->title.toLower()));
    } else if (!hadModel && normal && !noSpeech) {
      lab_->setNotice(L::Caution, tr("No speech model was loaded: recorded, not recognised."));
    }
    if (recognise) lab_->setText(tr("Processing"), tr("Processing"), true, {}, false);
    else lab_->setText({}, tr("Not run"), false, {}, false);
  });
  if (!recognise) tx_->onProcessed(c.id, std::nullopt, monotonicNow());
}

void LiveBackend::publish(Ns now) {
  lastPublish_ = now;
  if (cutUntil_ && now >= *cutUntil_) cutUntil_.reset();
  const S s = tx_->state();
  const TxDisplay d = txDisplay({.core = s, .pttHeld = held_, .closedAsStuck = stuck_, .blocked = tx_->pressBlocked(),
                                 .queued = tx_->pressQueued(), .pttPresent = !hasPtt_ || present_, .pulseSuspected = pulse_});
  const double sec = s == S::TxOpen ? double(now - openT_) / 1e9 : held_ && stuck_ ? double(txCfg_.maxDuration) / 1e9 : -1;
  const bool active = s == S::TxOpen || s == S::TxTail || s == S::Recognising || closing_;
  const double db = meter_.takePeakDbfs();

  QString mic = tr("STARTING");
  L::Level ml = L::Caution;
  switch (mic_.state) {
    case CaptureState::Lost: mic = tr("LOST"); ml = L::Fault; break;
    case CaptureState::Running:
      if (mic_.muted) { mic = tr("MUTED"); ml = L::Fault; }
      else if (meter_.zeroRun() > std::uint64_t(kRate)) { mic = tr("NO SIGNAL"); ml = L::Fault; }
      else { mic = tr("OK"); ml = L::Ok; }
      break;
    default: break;
  }
  ui([=, this, cut = cutUntil_.has_value(), right = held_ && owner_ == "RIGHT", left = left_, id = int(curId_), pulse = pulse_] {
    lab_->setTx(d, id);
    lab_->setTxSeconds(sec);
    lab_->setRx(s == S::PilotSpeaking);
    lab_->setPilotCut(cut);
    lab_->setButtons(right, left);
    lab_->setMic(db);
    lab_->setChip(L::Mic, mic, ml);
    if (pulse) lab_->setChip(L::Ptt, tr("PULSE"), L::Fault);
    stt_->setTransmissionActive(active);
  });
  // Leave PULSE as soon as a proper hold has been seen.
  if (!pulse_ && present_ && hasPtt_ && pttChip_ == tr("OK")) ui([this] { lab_->setChip(L::Ptt, tr("OK"), L::Ok); });
}
