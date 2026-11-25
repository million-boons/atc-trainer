#include "engine/Simulator.h"

#include <algorithm>

namespace atc {

Simulator::Simulator(SequenceEngine engine, IEventLog& log, Options opt) : logged_(std::move(engine), log), opt_(opt) {}

UtteranceReport Simulator::goodReport() {
  UtteranceReport r;
  r.empty = false;
  r.peakDbfs = -12;
  r.keyToSpeechMs = 300;
  return r;
}

ParsedInstruction Simulator::instruction(const std::string& intent, const std::map<std::string, std::string>& fields) {
  ParsedInstruction p;
  p.cls = RecognitionClass::Recognized;
  p.intent = intent;
  p.rawText = intent;
  for (const auto& [name, value] : fields) {
    ParsedField f;
    f.value = f.heard = value;
    p.fields[name] = f;
    p.rawText += " " + value;
  }
  p.normalisedText = p.rawText;
  return p;
}

void Simulator::apply(const EngineInput& input) {
  for (const auto& o : logged_.apply(now_, input)) {
    if (const auto* s = std::get_if<out::TimerSet>(&o)) {
      timers_[s->timer] = {s->generation, now_ + s->delay};
    } else if (const auto* c = std::get_if<out::TimerCancelled>(&o)) {
      timers_.erase(c->timer);
    } else if (const auto* p = std::get_if<out::PilotRequested>(&o)) {
      if (queue_.empty() && !speaking_) eligibleSince_ = now_;
      if (p->utterance.first) queue_.push_front(p->utterance);
      else queue_.push_back(p->utterance);
    } else if (const auto* e = std::get_if<out::Ended>(&o)) {
      reason_ = e->reason;
      queue_.clear();
    }
  }
}

std::optional<Ns> Simulator::nextPilot() const {
  if (speaking_) return speechEnd_;
  if (queue_.empty() || talking_ || (txBusy_ && !queue_.front().first)) return std::nullopt;
  return std::max(now_, eligibleSince_ + queue_.front().delay);
}

void Simulator::pilotStep() {
  if (speaking_) {
    const std::uint64_t id = speaking_->id;
    speaking_.reset();
    eligibleSince_ = now_;
    apply(in::PilotEnded{id, in::PilotEnd::Completed});
  } else {
    speaking_ = queue_.front();
    queue_.pop_front();
    speechEnd_ = now_ + opt_.pilotTalk;
    apply(in::PilotStarted{speaking_->id});
  }
}

void Simulator::advanceTo(Ns t) {
  while (true) {
    std::optional<in::Timer> which;
    Ns at = t;
    for (const auto& [k, d] : timers_)
      if (d.at <= t && (!which || d.at < at)) {
        which = k;
        at = d.at;
      }
    const auto pilot = nextPilot();
    if (pilot && *pilot <= t && (!which || *pilot < at)) {
      now_ = std::max(now_, *pilot);
      pilotStep();
    } else if (which) {
      now_ = std::max(now_, at);
      const std::uint64_t generation = timers_[*which].generation;
      timers_.erase(*which);
      apply(in::TimerFired{*which, generation});
    } else {
      break;
    }
  }
  now_ = std::max(now_, t);
}

void Simulator::start() { apply(in::Started{}); }

void Simulator::wait(Ns duration) { advanceTo(now_ + duration); }

void Simulator::waitForPilot() {
  while (const auto p = nextPilot()) advanceTo(*p);
}

void Simulator::transmit(ParsedInstruction p, const UtteranceReport& report, TxEnd end, const std::string& sttFault) {
  waitForPilot();
  p.tx = ++tx_;
  txBusy_ = talking_ = true;
  apply(in::TxOpened{tx_, now_, false});
  advanceTo(now_ + opt_.talk);
  apply(in::TxClosed{tx_, now_, end, report});
  talking_ = false;
  advanceTo(now_ + opt_.recognise);
  txBusy_ = false;
  eligibleSince_ = now_;
  in::Recognised r;
  r.instruction = std::move(p);
  r.releaseToText = opt_.recognise;
  r.sttFault = sttFault;
  apply(r);
}

void Simulator::stop() { apply(in::InstructorStop{}); }

void Simulator::resume() { apply(in::InstructorContinue{}); }

}  // namespace atc
