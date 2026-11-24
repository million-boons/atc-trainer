#include "runtime/RadioCore.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "audio/UtteranceChecks.h"

namespace atc {

using S = TransmissionController::State;
using K = PttNote::Kind;

RadioCore::RadioCore(RadioConfig cfg, RadioDevices dev, Events ev, std::function<Ns()> clock)
    : cfg_(std::move(cfg)), dev_(std::move(dev)), ev_(std::move(ev)), clock_(std::move(clock)),
      ring_(cfg_.rate, std::size_t(cfg_.rate) * std::size_t(cfg_.ringSeconds)) {
  build();
}

RadioCore::~RadioCore() { stop(); }

void RadioCore::stop() {
  pttDev_.reset();
  otherDev_.reset();
  cap_.reset();
  stop_ = true;
  cv_.notify_one();
  if (thread_.joinable()) thread_.join();
  if (player_) player_->stop();
  player_.reset();
}

void RadioCore::post(std::function<void()> f) {
  {
    std::lock_guard l(m_);
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
}

bool RadioCore::start() {
  // The thread first: nothing a device reports may wait for another device to open.
  thread_ = std::thread([this] { run(); });
  return open();
}

bool RadioCore::open() {
  if (dev_.player) {
    player_ = dev_.player({[this](PlayId id, bool ok) { post([=, this] { playerFinished(id, ok); }); },
                           [this](const OutputStatus& s) { post([=, this] { if (ev_.output) ev_.output(s); }); }});
    player_->start();
  }
  bool ok = false;
  if (dev_.mic) {
    cap_ = dev_.mic(ring_, meter_, [this](const CaptureStatus& s) { post([=, this] { mic_ = s; }); });
    ok = cap_->start();
  }
  if (dev_.ptt) {
    post([this] { link_ = PttLink::Opening; });
    const std::string src = cfg_.pttSource;
    pttDev_ = dev_.ptt({[this, src](Ns t, bool d) { post([=, this] { ptt_->rawEdge(t, src, d); }); },
                        [this, src](bool d) {
                          post([=, this] {
                            ptt_->setInitialState(src, d);
                            link_ = PttLink::Ok;
                            pttError_.clear();
                            note({.kind = d ? K::HeldAtOpen : K::Ready, .t = clock_(), .source = src});
                          });
                        },
                        [this, src](Ns t) {
                          post([=, this] {
                            link_ = PttLink::Lost;
                            ptt_->deviceLost(t, src);
                            note({.kind = K::Lost, .t = t, .source = src});
                          });
                        },
                        [this](const std::string& e) {
                          post([=, this] {
                            if (link_ != PttLink::Lost) link_ = PttLink::NotOpen;
                            pttError_ = e;
                          });
                        }});
    pttDev_->start();
  }
  if (dev_.otherButton) {
    const std::string src = cfg_.otherSource;
    otherDev_ = dev_.otherButton({[this, src](Ns t, bool d) {
                                    post([=, this] {
                                      other_ = d;
                                      note({.kind = d ? K::OtherDown : K::OtherUp, .t = t, .source = src});
                                    });
                                  },
                                  [this](bool d) { post([=, this] { other_ = d; }); },
                                  [this](Ns) { post([this] { other_ = false; }); },
                                  [](const std::string&) {}});
    otherDev_->start();
  }
  return ok;
}

void RadioCore::press(const std::string& s) {
  post([this, s, t = clock_()] { ptt_->rawEdge(t, s, true); });
}

void RadioCore::release(const std::string& s) {
  post([this, s, t = clock_()] { ptt_->rawEdge(t, s, false); });
}

void RadioCore::processed(std::uint64_t tx, std::optional<Reply> reply) {
  post([this, tx, reply = std::move(reply)]() mutable { tx_->onProcessed(tx, std::move(reply), clock_()); });
}

void RadioCore::requestPilot(Reply r) {
  post([this, r = std::move(r)]() mutable {
    if (testing_ || swallowed_) deferred_.push_back(std::move(r));
    else tx_->requestPilot(std::move(r), clock_());
  });
}

void RadioCore::flushPilot() {
  if (testing_ || swallowed_) return;
  for (auto& r : std::exchange(deferred_, {})) tx_->requestPilot(std::move(r), clock_());
}

bool RadioCore::idle() const { return tx_->state() == S::Idle && !held_ && !closing_; }

void RadioCore::playLocal(LocalSound what, std::vector<std::int16_t> samples, int rate) {
  post([this, what, rate, pcm = std::move(samples)]() mutable {
    if (!player_ || !idle() || testing_) {
      if (ev_.local) ev_.local(what, LocalEnd::Refused);
      return;
    }
    endLocal(LocalEnd::Interrupted);
    if (!player_->play(++play_, std::move(pcm), rate)) {
      if (ev_.local) ev_.local(what, LocalEnd::Failed);
      return;
    }
    sound_ = Sound::Local;
    local_ = what;
    if (ev_.local) ev_.local(what, LocalEnd::Started);
  });
}

void RadioCore::stopLocal() {
  post([this] { endLocal(LocalEnd::Interrupted); });
}

void RadioCore::silence() {
  sound_ = Sound::None;
  if (player_ && !player_->stop() && ev_.outputFault) ev_.outputFault("the speaker did not confirm that it is silent");
}

void RadioCore::endLocal(LocalEnd why) {
  if (sound_ != Sound::Local) return;
  silence();
  if (ev_.local) ev_.local(local_, why);
}

void RadioCore::startPilot(const Reply& r, PlaybackId id) {
  endLocal(LocalEnd::Interrupted);
  pilot_ = r;
  pilotPlayback_ = id;
  PilotSound s;
  if (ev_.pilotSound) s = ev_.pilotSound(r);
  if (!s.samples.empty() && player_ && player_->play(++play_, std::move(s.samples), s.rate)) {
    sound_ = Sound::Pilot;
    if (ev_.pilotStarted) ev_.pilotStarted(r);
    return;
  }
  if (ev_.outputFault) ev_.outputFault("the pilot reply could not be played");
  if (ev_.pilotEnded) ev_.pilotEnded(r, false);
  tx_->onPlaybackDone(id, clock_());  // last: it may start the next reply
}

void RadioCore::playerFinished(PlayId id, bool completed) {
  if (id != play_ || sound_ == Sound::None) return;  // stopped or replaced meanwhile
  const Sound was = std::exchange(sound_, Sound::None);
  if (was == Sound::Local) {
    if (ev_.local) ev_.local(local_, completed ? LocalEnd::Completed : LocalEnd::Failed);
    return;
  }
  const Reply r = pilot_;
  if (!completed && ev_.outputFault) ev_.outputFault("the pilot reply was cut off by the audio output");
  if (ev_.pilotEnded) ev_.pilotEnded(r, completed);
  tx_->onPlaybackDone(pilotPlayback_, clock_());  // last: it may start the next reply
}

void RadioCore::setPttTest(bool on) {
  post([this, on] {
    if (on == testing_) return;
    if (on && !idle()) {
      if (ev_.pttTest) ev_.pttTest({.kind = PttTest::Kind::Refused});
      return;
    }
    testing_ = on;
    if (ev_.pttTest) ev_.pttTest({.kind = on ? PttTest::Kind::Started : PttTest::Kind::Ended});
    flushPilot();
  });
}

void RadioCore::build() {
  ptt_.emplace(cfg_.ptt, PttInputManager::Callbacks{
      [this](Ns t, const std::string& src) {
        held_ = true;
        stuck_ = false;
        owner_ = src;
        pressT_ = t;
        endLocal(LocalEnd::Interrupted);  // the microphone must not hear it
        if (testing_) {
          swallowed_ = true;
          if (ev_.pttTest) ev_.pttTest({.kind = PttTest::Kind::Down, .source = src});
          return;
        }
        if (ev_.pressed) ev_.pressed();
        tx_->onPress(t);
      },
      [this](Ns t, const std::string& src, ReleaseReason why) {
        const Ns held = t - pressT_;
        held_ = false;
        if (why == ReleaseReason::Normal) {
          note({.kind = K::Up, .t = t, .source = src, .held = held});
          if (held >= cfg_.ptt.pulseLimit && src == cfg_.pttSource) pulse_ = false;
        }
        releaseT_ = t;
        if (std::exchange(swallowed_, false)) {
          if (ev_.pttTest) ev_.pttTest({.kind = PttTest::Kind::Up, .source = src, .held = held, .reason = why});
          flushPilot();
          return;
        }
        tx_->onRelease(t, why);
      },
      [this](const std::string& src) { note({.kind = K::Ignored, .t = clock_(), .source = src, .owner = owner_}); },
      [this](Ns held, const std::string& src) {
        if (src == cfg_.pttSource) pulse_ = true;  // a short tap on the screen says nothing about the pedal
        note({.kind = K::Pulse, .t = pressT_ + held, .source = src, .held = held});
      }});

  TransmissionController::Callbacks cb;
  cb.txBegin = [this](std::uint64_t id, Ns from) {
    closeNow();  // an earlier transmission still waiting for its last audio
    curId_ = id;
    openT_ = tx_->openedAt();
    note({.kind = K::Down, .t = openT_, .source = owner_, .tx = id});
    cursor_ = std::max(ring_.indexAt(from), ring_.keptFrom());
    lost_ = false;
    streaming_ = dev_.speech && dev_.speech->begin(id);
    if (ev_.txOpened) ev_.txOpened(id, openT_, streaming_);
  };
  cb.txEnd = [this](std::uint64_t id, Ns from, Ns to, TxEnd end) {
    if (end == TxEnd::Stuck) {
      stuck_ = true;
      note({.kind = K::Stuck, .t = to, .source = owner_, .tx = id});
    }
    closing_ = Closing{id, from, to, openT_, end, ring_.indexAt(to), end == TxEnd::Stuck ? to : releaseT_, clock_() + cfg_.lastAudioWait};
  };
  cb.txCancelled = [this](std::uint64_t id) {
    if (std::exchange(streaming_, false)) dev_.speech->cancel();
    note({.kind = K::Cancelled, .t = clock_(), .source = owner_, .tx = id});
    if (ev_.txCancelled) ev_.txCancelled(id, pulse_);
  };
  cb.pilotStart = [this](const Reply& r, PlaybackId id) { startPilot(r, id); };
  cb.pilotStop = [this](const Reply& r, PlaybackId, PilotStop why) {
    if (sound_ == Sound::Pilot) silence();
    if (why == PilotStop::Interrupted) cutUntil_ = clock_() + cfg_.pilotCutNotice;
    if (ev_.pilotStopped) ev_.pilotStopped(r, why);
  };
  cb.queuedOpened = [this](Ns) { if (ev_.sideTone) ev_.sideTone(); };
  tx_.emplace(cfg_.tx, std::move(cb));
}

void RadioCore::run() {
  while (!stop_) {
    {
      std::unique_lock l(m_);
      cv_.wait_for(l, std::chrono::milliseconds(5), [this] { return stop_ || !q_.empty(); });
    }
    step();
  }
}

void RadioCore::step() {
  std::vector<std::function<void()>> todo;
  {
    std::lock_guard l(m_);
    todo.swap(q_);
  }
  for (auto& f : todo) f();
  const Ns now = clock_();
  if (const auto i = ring_.newestIndex(); i != micIndex_ || mic_.state != CaptureState::Running) {
    micIndex_ = i;
    micAt_ = now;
  }
  ptt_->tick(now);
  tx_->tick(now);
  if (closing_ && (ring_.newestIndex() >= closing_->endIndex || now > closing_->deadline)) closeNow();
  else if (streaming_) feed(closing_ ? std::min(closing_->endIndex, ring_.newestIndex()) : ring_.newestIndex());
  // D-312: without a transmission only the pre-roll exists; older audio is erased at each step.
  if (!closing_ && tx_->state() != S::TxOpen && tx_->state() != S::TxTail)
    ring_.discardBefore(ring_.newestIndex() - cfg_.tx.preRoll * cfg_.rate / 1'000'000'000);
  if (now - lastPublish_ >= cfg_.publishEvery || !todo.empty()) publish(now);
}

void RadioCore::feed(std::int64_t to) {
  if (to <= cursor_) return;
  auto c = ring_.extractIndex(cursor_, to);
  if (c.missingHead) lost_ = true;
  cursor_ += std::int64_t(c.missingHead + c.samples.size());
  if (!c.samples.empty()) dev_.speech->accept(std::move(c.samples));
}

void RadioCore::closeNow() {
  if (!closing_) return;
  const Closing c = *std::exchange(closing_, std::nullopt);
  auto cut = ring_.extract(c.from, c.to);
  RadioTx t{.id = c.id, .from = c.from, .to = c.to, .press = c.press, .release = c.release, .end = c.end};
  const int wanted = int((c.press - c.from) * cfg_.rate / 1'000'000'000);
  // A missing head shortens the pre-roll; the press stays where it was. While idle only the pre-roll is
  // kept (D-312), so a press that was handled late finds a little less of it: not lost audio.
  t.gap = lost_ || cut.gap || cut.missingTail || int(cut.missingHead) > (ring_.keptFrom() ? wanted : 0);
  t.preRoll = std::max(0, wanted - int(cut.missingHead));
  t.noSpeech = checkUtterance(cut.samples, t.preRoll, {.rate = cfg_.rate}).empty;
  t.hadModel = streaming_;
  t.recognising = streaming_ && c.end == TxEnd::Normal && !t.noSpeech;
  if (t.recognising) {
    feed(c.endIndex);
    dev_.speech->finish(c.release);
  } else if (streaming_) {
    dev_.speech->cancel();
  }
  streaming_ = false;
  t.audio = std::move(cut.samples);
  if (ev_.txClosed) ev_.txClosed(std::move(t));
}

void RadioCore::publish(Ns now) {
  lastPublish_ = now;
  if (cutUntil_ && now >= *cutUntil_) cutUntil_.reset();
  if (!ev_.snapshot) return;
  RadioSnapshot s;
  s.state = tx_->state();
  const bool present = link_ == PttLink::NotSet || link_ == PttLink::Opening || link_ == PttLink::Ok;
  s.display = txDisplay({.core = s.state, .pttHeld = held_ && !swallowed_, .closedAsStuck = stuck_, .blocked = tx_->pressBlocked(),
                         .queued = tx_->pressQueued(), .pttPresent = present, .pulseSuspected = pulse_});
  s.tx = curId_;
  s.txSeconds = s.state == S::TxOpen ? double(now - openT_) / 1e9 : held_ && stuck_ ? double(cfg_.tx.maxDuration) / 1e9 : -1;
  s.active = s.state == S::TxOpen || s.state == S::TxTail || s.state == S::Recognising || closing_;
  s.pilotCut = cutUntil_.has_value();
  s.held = held_;
  s.owner = owner_;
  s.otherDown = other_;
  s.testing = testing_;
  s.ptt = link_;
  s.pttError = pttError_;
  s.pulse = pulse_;
  s.mic = mic_;
  s.micSilent = meter_.zeroRun() > std::uint64_t(cfg_.rate) || (mic_.state == CaptureState::Running && now - micAt_ > ms(1000));
  s.micDbfs = meter_.takePeakDbfs();
  ev_.snapshot(s);
}

}  // namespace atc
