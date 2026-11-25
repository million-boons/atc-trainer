#include "app/LiveBackend.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <algorithm>
#include <utility>

#include "app/LabChecks.h"
#include "audio/SideTone.h"

using namespace atc;
using L = LabController;
using K = PttNote::Kind;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

LiveBackend::LiveBackend(LabController* lab, SttController* stt, LiveConfig cfg, RecordingsController* recordings, QObject* parent)
    : QObject(parent), lab_(lab), stt_(stt), recordings_(recordings), cfg_(std::move(cfg)), micLow_(lab->micLow), micHigh_(lab->micHigh),
      t0_(monotonicNow()), speech_(stt) {
  lab_->setPttSource(cfg_.devices.pttDevice.empty() ? tr("no PTT device set") : cfg_.pttName);
  lab_->setText({}, tr("Waiting"), false, {}, false);

  RadioDevices dev = realDevices(cfg_.devices);
  dev.speech = &speech_;
  core_ = std::make_unique<RadioCore>(cfg_.radio, std::move(dev), events());
  status_ = new StatusController(lab, cfg_.status,
                                 {[this](bool on) { core_->setPttTest(on); },
                                  [this] { core_->playLocal(LocalSound::Tone, makeSideTone({.lengthMs = 1000}), SideToneConfig{}.rate); },
                                  [this] { core_->stopLocal(); }},
                                 this);

  connect(lab, &L::pressRequested, this, [this](const QString& s) { core_->press(s.toStdString()); });
  connect(lab, &L::releaseRequested, this, [this](const QString& s) { core_->release(s.toStdString()); });
  connect(lab, &L::playRequested, this, &LiveBackend::play);
  connect(lab, &L::exportRequested, this, [this] { lab_->setNotice(L::Caution, tr("Export is off. Recordings stay in memory.")); });

  connect(stt, &SttController::result, this,
          [this](quint64 tx, const QString& model, const QString& config, const QString& text, const QStringList& words,
                 const QList<double>& conf, double sec) {
            // A quick model may answer before the recording has arrived here.
            if (tx > lastClosed_) early_[tx] = [=, this] { result(tx, model, config, text, words, conf, sec); };
            else result(tx, model, config, text, words, conf, sec);
          });
  if (recordings_) {
    recordings_->setPlayer([this](std::vector<std::int16_t> s, int rate) { core_->playLocal(LocalSound::Recording, std::move(s), rate); });
    recordings_->setConnected();
  }
}

LiveBackend::~LiveBackend() {
  if (recordings_) recordings_->setPlayer({});
  core_->stop();
}

void LiveBackend::result(quint64 tx, const QString& model, const QString& config, const QString& text, const QStringList& words,
                         const QList<double>& conf, double sec) {
  const QString info = tr("%1  %2 s").arg(model).arg(sec, 0, 'f', 2);
  if (tx == guiTx_ && !guiOpen_) {  // never overwrite a newer transmission
    lab_->setText(text.isEmpty() ? tr("(nothing recognised)") : text, tr("Final"), text.isEmpty(), info, !text.isEmpty());
    lab_->setWords(words, conf);
  }
  lab_->setResult(int(tx), text, info, config);
  if (recordings_) {
    StoredResult r;
    r.modelId = QJsonDocument::fromJson(config.toUtf8()).object()["id"].toString().toStdString();
    r.modelName = model.toStdString();
    r.modelConfig = config.toStdString();
    r.text = text.toStdString();
    for (qsizetype i = 0; i < words.size(); ++i) r.words.push_back({words[i].toStdString(), i < conf.size() ? conf[i] : 0});
    r.releaseToTextMs = sec * 1000;
    recordings_->addResult(int(tx), std::move(r));
  }
  core_->processed(tx);
}

bool LiveBackend::start() {
  const bool ok = core_->start();
  if (!ok) notice(L::Fault, tr("The microphone could not be started."), L::Fault, tr("Mic: not started"));
  return ok;
}

void LiveBackend::event(Ns t, const std::string& src, const QString& edge, const QString& detail, bool warn) {
  ui([this, s = double(t - t0_) / 1e9, src = q(src), edge, detail, warn] { lab_->addEvent(s, src, edge, detail, warn); });
}

void LiveBackend::notice(L::Level l, const QString& n, L::Level al, const QString& a) {
  ui([=, this] {
    lab_->setNotice(l, n);
    lab_->setAlert(al, a);
  });
}

RadioCore::Events LiveBackend::events() {
  RadioCore::Events e;
  e.snapshot = [this](const RadioSnapshot& s) { ui([this, s] { snapshot(s); }); };
  e.note = [this](const PttNote& n) { note(n); };
  e.pressed = [this] { notice(L::None, {}, L::Ok, tr("Ready")); };
  e.txOpened = [this](std::uint64_t id, Ns, bool on) {
    ui([this, id, on] {
      guiTx_ = id;
      guiOpen_ = true;
      lab_->showRecording(-1);
      lab_->setWords({}, {});
      lab_->setText(on ? tr("Text appears after release") : tr("No speech model loaded: recording only"), tr("Listening"), true, {}, false);
    });
  };
  e.txClosed = [this](RadioTx&& t) { closed(std::move(t)); };
  e.txCancelled = [this](std::uint64_t, bool pulse) {
    if (pulse) notice(L::Fault, tr("Press was too short for a foot press: pulse-type pedal suspected. Hold-to-talk needs a hold-type pedal."), L::Fault, tr("Pulse-type device suspected"));
    else notice(L::Caution, tr("Press was shorter than 250 ms. Not recorded."), L::Caution, tr("Short tap cancelled"));
    ui([this] {
      guiOpen_ = false;
      lab_->setText({}, tr("Waiting"), false, {}, false);
    });
  };
  e.sideTone = [this] { ui([this] { emit sideTone(); }); };
  e.pilotStopped = [this](const Reply&, PilotStop why) {
    if (why == PilotStop::Timeout)
      notice(L::Fault, tr("Pilot voice stopped: playback did not finish in time."), L::Fault, tr("Pilot voice fault: stopped by the time limit"));
  };
  e.outputFault = [this](const std::string& why) { notice(L::Fault, tr("Audio output: %1.").arg(q(why)), L::Fault, tr("Audio output fault")); };
  e.local = [this](LocalSound what, LocalEnd end) {
    ui([this, what, end] {
      if (what == LocalSound::Tone) status_->onTone(end);
      else if (end == LocalEnd::Started) lab_->setNotice(L::None, {});
      else if (end == LocalEnd::Refused) lab_->setNotice(L::Caution, tr("No playback while the radio is in use."));
      else if (end == LocalEnd::Failed) lab_->setNotice(L::Fault, tr("The recording could not be played: no audio output."));
    });
  };
  e.pttTest = [this](const PttTest& t) { ui([this, t] { status_->onPttTest(t); }); };
  e.output = [this](const OutputStatus& s) { ui([this, s] { status_->onOutput(s); }); };
  return e;
}

void LiveBackend::note(const PttNote& n) {
  const QString msHeld = tr("held %1 ms").arg(double(n.held) / 1e6, 0, 'f', 1);
  switch (n.kind) {
    case K::Down:
      sources_[n.tx] = n.source;
      if (sources_.size() > 8) sources_.erase(sources_.begin());
      return event(n.t, n.source, tr("down"), tr("TX %1 open").arg(n.tx), false);
    case K::Up: return event(n.t, n.source, tr("up"), n.held < ms(50) ? msHeld : tr("held %1 s").arg(double(n.held) / 1e9, 0, 'f', 2), false);
    case K::Lost:
      event(n.t, n.source, tr("lost"), tr("unplugged"), true);
      return notice(L::Fault, tr("PTT disconnected. Reconnect it, or use Screen PTT."), L::Fault, tr("PTT device lost"));
    case K::Ignored:
      event(n.t, n.source, tr("ignored"), tr("%1 owns TX").arg(q(n.owner)), true);
      return ui([this, t = tr("%1 ignored: %2 is already transmitting.").arg(q(n.source), q(n.owner))] { lab_->setNotice(L::Caution, t); });
    case K::Pulse: return event(n.t, n.source, tr("pulse"), msHeld, true);
    case K::Ready: return event(n.t, n.source, tr("ready"), tr("device open"), false);
    case K::HeldAtOpen: return event(n.t, n.source, tr("ready"), tr("held: release"), true);
    case K::OtherDown: return event(n.t, n.source, tr("down"), tr("reported only"), false);
    case K::OtherUp: return event(n.t, n.source, tr("up"), tr("reported only"), false);
    case K::Stuck:
      event(n.t, "TX " + std::to_string(n.tx), tr("stuck"), tr("at 30 s limit"), true);
      return notice(L::Fault, tr("Transmission closed at 30 s. Lift your foot to transmit again."), L::Fault, tr("PTT stuck: closed at maximum duration"));
    case K::Cancelled: return event(n.t, "TX " + std::to_string(n.tx), tr("cancel"), tr("short tap"), true);
  }
}

void LiveBackend::closed(RadioTx&& t) {
  const LabChecks checks = labChecks(t.audio, t.preRoll, t.end, t.gap, micLow_, micHigh_);
  const int rate = cfg_.radio.rate;
  auto rec = std::make_shared<LabRecording>();
  const double n = std::max<double>(1, double(t.audio.size()));
  const double keyed = double(t.release - t.press) / 1e9 * rate;
  rec->id = int(t.id);
  rec->checks = checks.list;
  rec->pressFrac = std::clamp(t.preRoll / n, 0.0, 1.0);
  rec->endFrac = std::clamp((t.preRoll + keyed) / n, 0.0, 1.0);
  if (t.press == t.from) rec->startLabel = tr("TX OPEN");  // queued: no pre-roll
  rec->endLabel = t.end == TxEnd::Stuck ? tr("30 s LIMIT") : t.end == TxEnd::DeviceLost ? tr("LOST") : tr("RELEASE");
  rec->tailLabel = t.end == TxEnd::Stuck ? tr("no tail") : tr("tail");
  StoredRecording meta;
  meta.startedMs = QDateTime::currentMSecsSinceEpoch() - (monotonicNow() - t.press) / ms(1);
  meta.tx = rec->id;
  meta.pttSource = sources_[t.id];
  sources_.erase(t.id);
  meta.end = t.end == TxEnd::Stuck ? "stuck" : t.end == TxEnd::DeviceLost ? "lost" : "normal";
  meta.gap = t.gap;
  meta.rate = rate;
  meta.samples = int(t.audio.size());
  meta.pressSample = int(rec->pressFrac * double(t.audio.size()));
  meta.endSample = int(rec->endFrac * double(t.audio.size()));
  meta.report = checks.report;
  rec->audio = std::move(t.audio);
  ui([this, rec, meta = std::move(meta), recognise = t.recognising, hadModel = t.hadModel, issue = checks.issue, noSpeech = t.noSpeech,
      normal = t.end == TxEnd::Normal, id = t.id] {
    if (id == guiTx_) guiOpen_ = false;
    if (recordings_) recordings_->offer(meta, rec->audio);
    lab_->addRecording(std::move(*rec), QTime::currentTime().toString("HH:mm:ss"));
    if (issue) {
      lab_->setNotice(issue->level, issue->notice);
      lab_->setAlert(L::Caution, tr("TX %1: %2").arg(id).arg(issue->title.toLower()));
    } else if (!hadModel && normal && !noSpeech) {
      lab_->setNotice(L::Caution, tr("No speech model was loaded: recorded, not recognised."));
    }
    if (recognise) lab_->setText(tr("Processing"), tr("Processing"), true, {}, false);
    else lab_->setText({}, tr("Not run"), false, {}, false);
    lastClosed_ = id;
    if (const auto e = early_.extract(id)) e.mapped()();
  });
  if (!t.recognising) core_->processed(t.id);
}

void LiveBackend::snapshot(const RadioSnapshot& s) {
  if (s.ptt != link_) {
    link_ = s.ptt;
    if (link_ == PttLink::Ok) lab_->setAlert(L::Ok, tr("Ready"));
    if (link_ == PttLink::NotOpen) lab_->setAlert(L::Fault, tr("PTT: %1").arg(q(s.pttError)));
  }
  lab_->setTx(s.display, int(s.tx));
  lab_->setTxSeconds(s.txSeconds);
  lab_->setRx(s.pilotSpeaking());
  lab_->setPilotCut(s.pilotCut);
  lab_->setButtons(s.held && s.owner == "RIGHT", s.otherDown);
  lab_->setMic(s.micDbfs);
  lab_->setMicOpen(s.mic.state == CaptureState::Running);
  stt_->setTransmissionActive(s.active);
  status_->onSnapshot(s);
}

void LiveBackend::play(bool keyedPartOnly) {
  const auto* a = lab_->waveform();
  if (!a || a->empty()) return;
  auto from = a->begin(), to = a->end();
  if (keyedPartOnly) {
    to = a->begin() + std::ptrdiff_t(lab_->endFrac() * double(a->size()));
    from = a->begin() + std::ptrdiff_t(lab_->pressFrac() * double(a->size()));
  }
  if (from < to) core_->playLocal(LocalSound::Recording, {from, to}, cfg_.radio.rate);
}
