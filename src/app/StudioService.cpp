#include "app/StudioService.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "audio/RadioAudioProcessor.h"
#include "parser/Parser.h"
#include "phraseology/PhraseologyEdit.h"
#include "storage/PrivateFile.h"
#include "voice/PhraseAssembler.h"
#include "voice/PhraseTemplate.h"
#include "voice/VoiceBank.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
using SK = StudioNote::Kind;

std::vector<std::string> scenarioFiles(const std::string& data) {
  std::vector<std::string> out;
  std::error_code ec;
  for (fs::directory_iterator it(fs::path(data) / "scenarios", ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".json") out.push_back(it->path().string());
  std::sort(out.begin(), out.end());
  return out;
}

// "a, b" into one text, each once and in order.
void addOnce(std::vector<std::string>& v, const std::string& s) {
  if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

StudioEntry entryOf(const PhraseEntry& p) {
  StudioEntry e;
  e.id = p.id;
  e.reply = p.reply;
  e.texts = p.texts;
  e.validated = p.validated;
  e.placeholderFile = p.placeholderFile;
  e.sopRef = p.sopRef;
  e.validatedBy = p.validatedBy;
  e.validatedAt = p.validatedAt;
  return e;
}

// Every scenario, step and case that names the entry.
std::vector<std::string> usedBy(const PhraseEntry& p, const std::vector<std::pair<std::string, Exercise>>& all) {
  std::vector<std::string> out;
  for (const auto& [file, e] : all)
    for (const auto& s : e.scenario.steps) {
      const std::string at = e.scenario.id + "  " + s.label;
      if (p.reply && s.onEnter && s.onEnter->reply == p.id) addOnce(out, at + ": pilot call");
      if (p.reply)
        for (const auto& [outcome, r] : s.responses) {
          if (r.say && r.reply == p.id) addOnce(out, at + ": " + std::string(toString(outcome)));
          for (const auto& [field, reply] : r.withField)
            if (reply == p.id) addOnce(out, at + ": " + std::string(toString(outcome)) + ", " + field);
        }
      else
        for (const auto& x : s.expects)
          if (x.intent == p.id) addOnce(out, at);
    }
  return out;
}

void append(std::vector<std::int16_t>& out, std::size_t silenceMs) { out.resize(out.size() + silenceMs * TakeStore::kRate / 1000); }

std::vector<std::int16_t> beep() {
  std::vector<std::int16_t> b(TakeStore::kRate / 4);
  for (std::size_t i = 0; i < b.size(); ++i) b[i] = std::int16_t(1500 * std::sin(double(i) * 0.17));
  return b;
}
}  // namespace

StudioService::StudioService(Config c, std::shared_ptr<ConsentView> consent, std::shared_ptr<DataCopy> copy, Rules r)
    : cfg_(std::move(c)), consent_(std::move(consent)), copy_(std::move(copy)), rules_(std::move(r)),
      takes_({.bank = cfg_.bank, .pending = cfg_.pending, .voice = cfg_.voice, .trim = {}, .minFreeMb = cfg_.minFreeMb, .freeMb = {}}), thread_([this] { run(); }) {}

StudioService::~StudioService() {
  {
    std::lock_guard l(qm_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();  // what is queued is done first: a last take is kept
}

void StudioService::connect(Radio r) {
  std::lock_guard l(m_);
  radio_ = std::move(r);
}

void StudioService::post(std::function<void()> f) {
  {
    std::lock_guard l(qm_);
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
}

void StudioService::run() {
  for (;;) {
    std::function<void()> f;
    {
      std::unique_lock l(qm_);
      busy_ = false;
      if (q_.empty()) idle_.notify_all();
      cv_.wait(l, [this] { return stop_ || !q_.empty(); });
      if (q_.empty()) return;
      f = std::move(q_.front());
      q_.pop_front();
      busy_ = true;
    }
    f();
  }
}

void StudioService::wait() {
  std::unique_lock l(qm_);
  idle_.wait(l, [this] { return q_.empty() && !busy_; });
}

StudioEvents StudioService::events() const {
  std::lock_guard l(m_);
  return ev_;
}

std::string StudioService::mayWrite() const {
  if (!rules_.instructor || !rules_.instructor()) return "only the instructor may do this";
  if (rules_.exerciseActive && rules_.exerciseActive()) return "not during an exercise";
  return "";
}

// ---- What the screen asks for (GUI thread)

StudioBackend StudioService::backend() {
  StudioBackend b;
  b.setEvents = [this](StudioEvents e) {
    std::lock_guard l(m_);
    ev_ = std::move(e);
  };
  b.load = [this](auto done) { post([this, done = std::move(done)]() mutable { doLoad(std::move(done)); }); };
  b.preview = [this](const StudioEntry& e, int i, auto done) { post([this, e, i, done = std::move(done)]() mutable { doPreview(e, i, std::move(done)); }); };
  const auto save = [this](StudioEntry e, const std::string& revision, Done done, bool add) {
    if (std::string why = mayWrite(); !why.empty()) return done(why);
    post([this, e = std::move(e), revision, add, today = rules_.today ? rules_.today() : std::string(), done = std::move(done)] {
      const std::string why = doSave(e, revision, add, today);
      done(why);
      if (why.empty() && rules_.changed) rules_.changed();
    });
  };
  b.save = [save](StudioEntry e, const std::string& r, Done d) { save(std::move(e), r, std::move(d), false); };
  b.addReply = [save](StudioEntry e, const std::string& r, Done d) { save(std::move(e), r, std::move(d), true); };
  b.clips = [this](const std::string& s, auto done) { post([this, s, done = std::move(done)]() mutable { doClips(s, std::move(done)); }); };
  b.peaks = [this](const std::string& id, auto done) {
    post([this, id, done = std::move(done)] {
      if (std::string why = store(); !why.empty()) return done({}, why);
      const Take* t = takes_.find(id);
      if (!t) return done({}, "the take is gone");
      if (withdrawn(*t)) return done({}, "consent withdrawn");
      const auto a = t->samples ? takes_.raw(id) : takes_.cut(id);
      if (!a || a->empty()) return done({}, "the recording cannot be read");
      const std::size_t n = std::min<std::size_t>(1000, a->size());
      std::vector<float> p;
      for (std::size_t i = 0; i < n; ++i) {
        const auto [lo, hi] = std::minmax_element(a->begin() + std::ptrdiff_t(i * a->size() / n), a->begin() + std::ptrdiff_t((i + 1) * a->size() / n));
        p.push_back(float(*lo) / 32768.0f);
        p.push_back(float(*hi) / 32768.0f);
      }
      done(std::move(p), "");
    });
  };

  b.startSession = [this](const std::string& speaker, Done done) {
    if (std::string why = mayWrite(); !why.empty()) return done(why);
    if (!consent_->mayRecord(speaker)) return done("this person may not be recorded as the pilot voice: both answers must be yes and asked again if the text changed");
    Radio r;
    {
      std::lock_guard l(m_);
      if (session_ != Session::None) r = {};
      else r = radio_;
      if (r.session) {
        session_ = Session::Starting;
        starting_ = done;
        speaker_ = speaker;
        pedal_ = false;
        pedalClip_.clear();
      }
    }
    if (!r.session) return done("a session is open already, or there is no radio");
    r.session(true, "");
  };
  b.endSession = [this] {
    Radio r;
    {
      std::lock_guard l(m_);
      if (session_ == Session::None) return;
      r = radio_;
    }
    if (r.session) r.session(false, "");
  };
  b.setPedal = [this](bool on, const std::string& clip) {
    Radio r;
    {
      std::lock_guard l(m_);
      if (session_ != Session::Open) return;
      pedalClip_ = on ? clip : std::string();
      pedal_ = on && !clip.empty();  // without a clip the pedal records nothing
      r = radio_;
      on = pedal_;
    }
    if (r.pedal) r.pedal(on);
  };
  b.record = [this](const std::string& clip, Done done) {
    if (std::string why = mayWrite(); !why.empty()) return done(why);
    std::string speaker;
    Radio r;
    {
      std::lock_guard l(m_);
      if (session_ != Session::Open) return done("no recording session");
      if (recording_ || recordAsked_) return done("a take is being recorded");
      speaker = speaker_;
      r = radio_;
    }
    if (clip.empty()) return done("no clip is chosen");
    if (!consent_->mayRecord(speaker)) return done("the consent of this person has changed");
    {
      std::lock_guard l(m_);
      clip_ = clip;
      recordAsked_ = true;
    }
    done("");
    if (r.record) r.record();
  };
  b.stopRecord = [this] {
    Radio r;
    {
      std::lock_guard l(m_);
      r = radio_;
    }
    if (r.stop) r.stop();
  };

  // A change of one take, with the rules of the role; then everybody is told.
  const auto change = [this](std::function<std::string()> f, Done done, bool bankChanged) {
    if (std::string why = mayWrite(); !why.empty()) return done(why);
    post([this, f = std::move(f), done = std::move(done), bankChanged] {
      std::string why = store();
      if (why.empty()) why = f();
      done(why);
      if (!why.empty()) return;
      if (const auto e = events(); e.clipsChanged) e.clipsChanged();
      if (bankChanged && rules_.changed) rules_.changed();
    });
  };
  b.setTrim = [this, change](const std::string& id, double start, double end, Done done) {
    change([this, id, start, end]() -> std::string {
      const Take* t = takes_.find(id);
      if (t && withdrawn(*t)) return "consent withdrawn";
      if (std::string why = takes_.setTrim(id, start, end); !why.empty()) return why;
      if (const auto e = events(); e.takeReady) e.takeReady(shown(*takes_.find(id)));
      return "";
    }, std::move(done), true);
  };
  b.accept = [this, change](const std::string& id, bool primary, Done done) {
    change([this, id, primary]() -> std::string {
      const Take* t = takes_.find(id);
      if (!t) return "the take is gone";
      if (!t->synthetic && !consent_->mayUse(t->speaker.id)) return "consent withdrawn, or asked again and not answered";
      const std::string dir = copy_->dir();
      Needs n;
      std::string error;
      if (!allNeeds(loadAll(dir), dir, n, error)) return error;
      const auto it = n.find(t->clipId);
      return takes_.accept(id, primary, it == n.end() ? std::string() : it->second.words);
    }, std::move(done), true);
  };
  b.reject = [this, change](const std::string& id, Done done) { change([this, id] { return takes_.reject(id); }, std::move(done), true); };
  b.deleteTake = [this, change](const std::string& id, Done done) {
    change([this, id, when = rules_.today ? rules_.today() : std::string()]() -> std::string {
      const Take* t = takes_.find(id);
      return t ? takes_.remove(id, withdrawn(*t), "instructor", when) : "the take is gone";
    }, std::move(done), true);
  };
  b.play = [this](const std::string& id, const std::string& mode, Done done) {
    {
      std::lock_guard l(m_);
      if (recording_ || recordAsked_) return done("a take is being recorded");
    }
    post([this, id, mode, done = std::move(done)] {
      std::vector<std::int16_t> pcm;
      std::string note;
      if (std::string why = store(); !why.empty()) return done(why);
      if (std::string why = sound(id, mode, pcm, note); !why.empty()) return done(why);
      Done before;
      {
        // Under the lock: after connect({}) the radio may be gone.
        std::lock_guard l(m_);
        before = std::exchange(playing_, radio_.play ? done : Done{});
        playNote_ = note;
        if (radio_.play) radio_.play(std::move(pcm), TakeStore::kRate);
      }
      if (before) before("replaced by another playback");
      std::unique_lock l(m_);
      if (!radio_.play) {
        l.unlock();
        done("there is no radio");
      }
    });
  };
  b.stopPlay = [this] {
    Radio r;
    {
      std::lock_guard l(m_);
      r = radio_;
    }
    if (r.stopPlay) r.stopPlay();
  };
  b.exportChanges = [this](const std::string& file, Done done) {
    if (std::string why = mayWrite(); !why.empty()) return done(why);
    post([this, file, done = std::move(done)] { done(doExport(file)); });
  };
  return b;
}

// ---- What the radio reports (core thread)

void StudioService::onStudio(const StudioNote& n) {
  StudioEvents ev;
  Done answer;
  std::string why = n.why;
  bool wasRecording = false;
  {
    std::lock_guard l(m_);
    ev = ev_;
    switch (n.kind) {
      case SK::Started: break;
      case SK::Refused:
        session_ = Session::None;
        answer = std::exchange(starting_, {});
        break;
      case SK::RoomTone:
        noise_ = n.dbfs;
        session_ = Session::Open;
        answer = std::exchange(starting_, {});
        break;
      case SK::Recording:
        recording_ = true;
        takeClip_ = recordAsked_ ? clip_ : pedalClip_;
        takeSpeaker_ = speaker_;
        recordAsked_ = false;
        recordedAt_ = monotonicNow();
        break;
      case SK::NotRecording:
        wasRecording = std::exchange(recording_, false);
        recordAsked_ = false;
        break;
      case SK::Ended:
        answer = std::exchange(starting_, {});
        session_ = Session::None;
        recording_ = recordAsked_ = pedal_ = false;
        speaker_.clear();
        break;
    }
  }
  switch (n.kind) {
    case SK::Started: break;
    case SK::Refused:
      if (answer) answer(why);
      break;
    case SK::RoomTone:
      if (ev.roomTone) ev.roomTone(n.dbfs, n.dbfs > cfg_.loudRoomDb ? "the room is loud: the trim may keep noise" : "");
      if (answer) answer("");
      break;
    case SK::Recording:
      if (ev.recording) ev.recording(true);
      break;
    case SK::NotRecording:
      if (wasRecording && ev.recording) ev.recording(false);
      if (ev.recordingStopped) ev.recordingStopped(why);
      break;
    case SK::Ended:
      if (answer) answer(why.empty() ? "the session was ended" : why);
      else if (ev.sessionEnded) ev.sessionEnded(why);
      break;
  }
}

void StudioService::onTake(StudioAudio&& a) {
  std::string clip, speaker;
  double noise;
  {
    std::lock_guard l(m_);
    clip = takeClip_;
    speaker = takeSpeaker_;
    noise = noise_;
  }
  post([this, a = std::move(a), clip, speaker, noise]() mutable {
    const auto ev = events();
    const auto stop = [&](const std::string& why) {
      if (ev.recordingStopped) ev.recordingStopped(why);
    };
    // Checked before every save (D-132, D-133): otherwise the audio is gone from memory and nothing is written.
    const auto p = consent_->find(speaker);
    if (!p || !consent_->mayRecord(speaker)) return stop("the consent of this person has changed: the take was not kept");
    if (std::string why = store(); !why.empty()) return stop(why);
    const std::string dir = copy_->dir();
    Needs need;
    std::string error;
    if (!allNeeds(loadAll(dir), dir, need, error)) return stop(error);
    const auto it = need.find(clip);
    if (it == need.end()) return stop("no scenario needs this clip: the take was not kept");
    NewTake t;
    t.clipId = clip;
    t.words = it->second.words;
    t.speaker = {p->id, {p->consent, p->consentDate, p->consentVersion}, {p->pilotVoice, p->pilotVoiceDate, p->pilotVoiceVersion}};
    t.recordedMs = rules_.nowMs ? rules_.nowMs() : 0;
    t.raw = std::move(a.samples);
    t.noiseFloorDb = noise;
    if (a.gap) t.warnings.push_back("audio was lost");
    if (!a.why.empty()) t.warnings.push_back("stopped: " + a.why);
    Take out;
    if (std::string why = takes_.add(std::move(t), out); !why.empty()) return stop("the take could not be kept: " + why);
    if (ev.takeReady) ev.takeReady(shown(out));
    if (ev.clipsChanged) ev.clipsChanged();
  });
}

void StudioService::onSnapshot(const RadioSnapshot& s) {
  if (!s.studio) return;
  StudioEvents ev;
  Ns since = 0;
  {
    std::lock_guard l(m_);
    ev = ev_;
    since = recordedAt_;
  }
  if (ev.level) ev.level(s.micDbfs);
  if (s.recording && ev.elapsed) ev.elapsed(int((monotonicNow() - since) / ms(1)));
}

void StudioService::onPlay(LocalEnd e) {
  if (e == LocalEnd::Started) {
    Done done;
    std::string note;
    {
      std::lock_guard l(m_);
      done = std::exchange(playing_, {});
      note = std::exchange(playNote_, {});
    }
    if (done) done(note);  // the note names what is missing
    return;
  }
  if (e != LocalEnd::Refused && e != LocalEnd::Failed) return;
  Done done;
  {
    std::lock_guard l(m_);
    done = std::exchange(playing_, {});
  }
  if (done) done(e == LocalEnd::Refused ? "the radio is in use" : "it could not be played: no audio output");
}

void StudioService::consentChanged() {
  Radio r;
  bool end = false;
  {
    std::lock_guard l(m_);
    r = radio_;
    end = session_ != Session::None && (!(rules_.instructor && rules_.instructor()) || !consent_->mayRecord(speaker_));
  }
  if (end && r.session) r.session(false, rules_.instructor && rules_.instructor() ? "the consent of this person has changed" : "the instructor logged out");
  if (const auto e = events(); e.clipsChanged) e.clipsChanged();
}

// ---- Files (worker thread)

std::string StudioService::store() {
  if (read_) return "";
  if (std::string why = takes_.load(); !why.empty()) return why;
  read_ = true;
  return "";
}

bool StudioService::withdrawn(const Take& t) const { return !t.synthetic && !consent_->mayUse(t.speaker.id); }

StudioTake StudioService::shown(const Take& t) const {
  StudioTake s;
  s.id = t.id;
  s.clipId = t.clipId;
  s.speaker = t.speaker.id;
  s.number = t.number;
  s.synthetic = t.synthetic;
  s.state = withdrawn(t) ? StudioTake::State::Withdrawn
            : t.state == Take::State::Primary ? StudioTake::State::Primary
            : t.state == Take::State::Alternate ? StudioTake::State::Alternate
            : t.state == Take::State::Rejected ? StudioTake::State::Rejected : StudioTake::State::Pending;
  s.seconds = double(t.durationMs) / 1000;
  s.levelDb = t.speechDb;
  if (t.samples) {
    s.trimStart = double(t.trimStart) / double(t.samples);
    s.trimEnd = double(t.trimEnd) / double(t.samples);
  }
  s.warnings = t.warnings;
  return s;
}

StudioService::Loaded StudioService::loadAll(const std::string& phraseology) const {
  Loaded l;
  for (const auto& f : scenarioFiles(cfg_.data)) {
    ScenarioSources src = scenarioSources(cfg_.data, f);
    src.phraseologyDir = phraseology;
    Exercise e;
    std::vector<LoadError> errors;
    if (loadExercise(src, e, errors)) l.exercises.emplace_back(f, std::move(e));
    else
      for (const auto& x : errors) l.errors.push_back(toString(x));
  }
  return l;
}

bool StudioService::needs(const Exercise& e, const SpokenForms& f, Needs& out, std::string& error) const {
  std::map<std::string, RequiredClip> req;
  std::vector<std::string> bad;
  std::vector<ClipLine> lines;
  if (!requiredClips(e, req, bad) || !clipList(e, f, lines, bad)) {
    error = bad.empty() ? e.scenario.id + ": the clips cannot be listed" : bad.front();
    return false;
  }
  for (const auto& l : lines) {
    Need& n = out[l.id];
    n.words = l.words;
    if (const auto it = req.find(l.id); it != req.end()) {
      n.pool = it->second.pool;
      n.value = it->second.value;
    }
    if (const auto at = l.id.rfind('@'); at != std::string::npos) n.position = l.id.substr(at + 1);
  }
  // Every reply that uses a clip, not only the first.
  for (const auto& [id, r] : e.replies) {
    std::vector<PhraseSegment> segs;
    if (!parsePhraseTemplate(r.text, segs, error)) return false;
    for (const auto& s : segs) {
      if (!s.dynamic) {
        if (const auto it = out.find(phraseClipId(s.text, s.position)); it != out.end()) addOnce(it->second.replies, id);
        continue;
      }
      const auto g = e.scenario.generators.find(s.text);
      if (g == e.scenario.generators.end()) continue;
      for (auto& [clip, n] : out)
        if (n.pool == g->second.pool && clip == valueClipId(n.pool, n.value, s.position)) addOnce(n.replies, id);
    }
  }
  return true;
}

bool StudioService::allNeeds(const Loaded& l, const std::string& phraseology, Needs& out, std::string& error) const {
  if (l.exercises.empty()) {
    error = l.errors.empty() ? "no scenario" : l.errors.front();
    return false;
  }
  SpokenForms forms;
  if (!loadSpokenForms(phraseology, forms, error)) return false;
  for (const auto& [f, e] : l.exercises)
    if (!needs(e, forms, out, error)) return false;
  return true;
}

// The reply in which the clip is said, with values that fit together: "taxi via alpha three runway **zero niner** ...".
std::string StudioService::say(const Exercise& e, const SpokenForms& f, const std::string& clipId, const Need& n, StudioClip& out, std::vector<std::string>* sequence) const {
  if (n.replies.empty()) return "no reply uses this clip";
  const auto reply = e.replies.find(n.replies.front());
  if (reply == e.replies.end()) return "no reply uses this clip";
  std::vector<PhraseSegment> segs;
  std::string error;
  if (!parsePhraseTemplate(reply->second.text, segs, error)) return error;
  const auto idOf = [&](const PhraseSegment& s, const ParamValues& v) {
    if (!s.dynamic) return phraseClipId(s.text, s.position);
    const auto g = e.scenario.generators.find(s.text);
    const auto val = v.find(s.text);
    return g == e.scenario.generators.end() || val == v.end() ? std::string() : valueClipId(g->second.pool, val->second, s.position);
  };
  for (std::uint64_t seed = 0; seed < 4096; ++seed) {
    ParamValues v;
    if (!generateParams(e.scenario, e.airfield, seed, v)) return "no values";
    const auto target = std::find_if(segs.begin(), segs.end(), [&](const PhraseSegment& s) { return idOf(s, v) == clipId; });
    if (target == segs.end()) continue;
    std::string before, after, word;
    for (auto it = segs.begin(); it != segs.end(); ++it) {
      std::string w = it->text;
      if (it->dynamic) w = spokenValue(e.airfield, f, e.scenario.generators.at(it->text).pool, v.at(it->text), error);
      if (it->commaAfter) w += ",";
      if (it == target) word = it->commaAfter ? w.substr(0, w.size() - 1) : w;
      else (it < target ? before : after) += (it < target ? before : after).empty() ? w : " " + w;
      if (it == target && it->commaAfter) after = ",";
    }
    out.sayBefore = before;
    out.sayTarget = word;
    out.sayAfter = after;
    if (sequence) *sequence = clipSequence(reply->second.text, e.scenario, v, error);
    return error;
  }
  return "no values use this clip";
}

void StudioService::doLoad(std::function<void(StudioContent, const std::string&)> done) {
  StudioContent c;
  PhraseSet s;
  std::string error;
  const std::string dir = copy_->dir();
  if (!readPhraseology(dir, s, error)) return done({}, error);
  const Loaded l = loadAll(dir);
  for (const auto& p : s.entries) {
    c.entries.push_back(entryOf(p));
    c.entries.back().usedBy = usedBy(p, l.exercises);
  }
  for (const auto& [f, e] : l.exercises) c.scenarios.push_back({e.scenario.id, e.airfield.id});
  c.voice = cfg_.voice;
  c.activeVoice = fs::path(cfg_.activeBank).filename().string();
  c.changedHere = copy_->exists();
  c.revision = s.revision;
  const auto add = [&c](const std::string& t) { c.notice += (c.notice.empty() ? "" : "  ") + t; };
  if (copy_->installedIsNewer()) add("The installed data has changed since the copy on this machine was made. Nothing is merged.");
  if (!l.errors.empty()) add("A scenario cannot be loaded: " + l.errors.front());
  read_ = false;  // read the takes again as well
  if (std::string why = store(); !why.empty()) add(why);
  c.placeholderVoice = takes_.placeholder();
  if (takes_.unlisted()) add(std::to_string(takes_.unlisted()) + " audio files in the voice folder belong to no clip.");
  done(std::move(c), "");
}

void StudioService::doPreview(StudioEntry e, int index, std::function<void(StudioPreview)> done) {
  StudioPreview p;
  const std::string t = index >= 0 && std::size_t(index) < e.texts.size() ? e.texts[std::size_t(index)] : std::string();
  std::vector<PhraseSegment> segs;
  if (!parsePhraseTemplate(t, segs, p.error)) return done(std::move(p));
  const std::string dir = copy_->dir();
  Loaded l = loadAll(dir);
  for (const auto& s : segs) {
    StudioSegment x;
    x.dynamic = s.dynamic;
    x.commaAfter = s.commaAfter;
    x.text = s.text;
    if (e.reply) {
      x.position = toString(s.position);
      if (!s.dynamic) x.clipId = phraseClipId(s.text, s.position);
      else
        for (const auto& [f, ex] : l.exercises)
          if (const auto g = ex.scenario.generators.find(s.text); g != ex.scenario.generators.end()) x.pool = g->second.pool;
    }
    p.segments.push_back(std::move(x));
  }
  for (const auto& [f, ex] : l.exercises)
    for (const auto& w : lintPhrase(segs, ex.airfield)) addOnce(p.lint, w);
  if (e.reply)
    for (const auto& x : p.segments)
      if (x.dynamic && x.pool.empty()) addOnce(p.lint, "{" + x.text + "} is not a value of any scenario");
  if (e.reply) {
    Needs was, will;
    std::string error;
    if (store().empty() && allNeeds(l, dir, was, error)) {
      for (auto& [f, ex] : l.exercises)
        if (const auto r = ex.replies.find(e.id); r != ex.replies.end()) {
          r->second.text = t;
          templateSlots(t, r->second.slots);
        }
      if (allNeeds(l, dir, will, error)) {
        for (const auto& [id, n] : will)
          if (!was.count(id) && takes_.bankWords(id).empty()) p.missing.push_back(id);
        for (const auto& [id, n] : was)
          if (!will.count(id)) p.unused.push_back(id);
      } else {
        addOnce(p.lint, error);
      }
    }
  }
  done(std::move(p));
}

// Empty, or why the new text must not be saved: a scenario that loads today would not load.
std::string StudioService::trial(const std::string& dir, const std::set<std::string>& before) const {
  const Loaded l = loadAll(dir);
  std::set<std::string> now;
  for (const auto& [f, e] : l.exercises) now.insert(f);
  for (const auto& f : before)
    if (!now.count(f)) return "a scenario would no longer load: " + (l.errors.empty() ? f : l.errors.front());
  for (const auto& name : phraseologyFiles(dir)) {
    if (name != "placeholder.json") continue;  // the one the parser reads
    ParserRules r;
    std::string error;
    if (!loadParserRules(dir + "/" + name, r, error)) return "the recogniser's rules would no longer load: " + error;
  }
  Needs n;
  std::string error;
  if (!before.empty() && !allNeeds(l, dir, n, error)) return error;
  return "";
}

std::string StudioService::doSave(const StudioEntry& e, const std::string& revision, bool add, const std::string& today) {
  const std::string dir = copy_->dir();
  PhraseSet s;
  std::string error;
  if (!readPhraseology(dir, s, error)) return error;
  if (s.revision != revision) return "the file changed on disk: undo and edit again";
  PhraseEntry w;
  w.id = e.id;
  w.reply = e.reply || add;
  w.texts = e.texts;
  w.validated = e.validated;
  w.sopRef = e.sopRef;
  for (const auto& t : w.texts) {
    std::vector<PhraseSegment> segs;
    if (!parsePhraseTemplate(t, segs, error)) return error;
  }
  const Loaded now = loadAll(dir);
  std::set<std::string> loads, slots;
  for (const auto& [f, x] : now.exercises) {
    loads.insert(f);
    for (const auto& [name, g] : x.scenario.generators) slots.insert(name);
  }
  if (w.reply)
    for (const auto& t : w.texts) {
      std::vector<std::string> used;
      templateSlots(t, used);
      for (const auto& u : used)
        if (!slots.count(u)) return "{" + u + "} is not a value of any scenario";
    }
  if (add) {
    for (const auto& x : s.entries)
      if (x.reply && x.id == w.id) return w.id + " exists already";
    for (const auto& f : phraseologyFiles(dir))
      if (w.file.empty() && f.starts_with("replies/")) w.file = f;
    if (w.file.empty()) return "there is no file of replies";
  } else {
    const PhraseEntry* was = s.find(w.id, w.reply);
    if (!was) return w.id + " is gone";
    w.file = was->file;
  }
  std::ifstream in(dir + "/" + w.file, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string next;
  // Who: the instructor's login has no person (D-111).
  if (std::string why = add ? appendReply(bytes, w, next) : changeEntry(bytes, w, {"instructor", today}, next); !why.empty()) return why;
  return copy_->write(w.file, next, [this, &loads](const std::string& d) { return trial(d, loads); });
}

void StudioService::doClips(const std::string& scenario, std::function<void(std::vector<StudioClip>, const std::string&)> done) {
  if (std::string why = store(); !why.empty()) return done({}, why);
  const std::string dir = copy_->dir();
  const Loaded l = loadAll(dir);
  SpokenForms forms;
  Needs here, all;
  std::string error;
  if (!loadSpokenForms(dir, forms, error)) return done({}, error);
  const Exercise* ex = nullptr;
  for (const auto& [f, e] : l.exercises)
    if (e.scenario.id == scenario) ex = &e;
  if (!ex) return done({}, l.errors.empty() ? scenario + " is not there" : l.errors.front());
  if (!needs(*ex, forms, here, error) || !allNeeds(l, dir, all, error)) return done({}, error);
  std::map<std::string, StudioClip> out;
  for (const auto& [id, n] : here) {
    StudioClip& c = out[id];
    c.id = id;
    c.words = n.words;
    c.pool = n.pool;
    c.value = n.value;
    c.position = n.position;
    c.replies = n.replies;
    c.sayTarget = n.words;
    (void)say(*ex, forms, id, n, c, nullptr);
    c.bankWords = takes_.bankWords(id);
  }
  for (const auto& t : takes_.takes()) {
    if (!out.count(t.clipId)) {
      if (all.count(t.clipId)) continue;  // another scenario needs it
      StudioClip& c = out[t.clipId];
      c.id = t.clipId;
      c.words = c.sayTarget = t.words;
      c.bankWords = takes_.bankWords(t.clipId);
      c.needed = false;
      if (const auto at = t.clipId.rfind('@'); at != std::string::npos) c.position = t.clipId.substr(at + 1);
    }
    out[t.clipId].takes.push_back(shown(t));
  }
  std::vector<StudioClip> list;
  for (auto& [id, c] : out) {
    // A clip whose only takes are withdrawn is missing for the exercise.
    if (!c.takes.empty() && std::none_of(c.takes.begin(), c.takes.end(), [](const StudioTake& t) { return t.state == StudioTake::State::Primary || t.state == StudioTake::State::Alternate; })) c.bankWords.clear();
    list.push_back(std::move(c));
  }
  done(std::move(list), "");
}

std::string StudioService::doExport(const std::string& file) {
  if (!copy_->exists()) return "nothing was changed on this machine";
  PhraseSet a, b;
  std::string error;
  if (!readPhraseology(copy_->installed(), a, error) || !readPhraseology(copy_->dir(), b, error)) return error;
  std::error_code ec;
  if (file.empty() || fs::is_directory(file, ec)) return "choose a file name";
  if (!fs::is_directory(fs::path(file).parent_path(), ec)) return "that folder does not exist";
  return writePrivateFile(file, describeChanges(a, b)) ? "" : file + ": cannot write";
}

std::string StudioService::sound(const std::string& id, const std::string& mode, std::vector<std::int16_t>& out, std::string& note) {
  const Take* t = takes_.find(id);
  if (!t) return "the take is gone";
  if (withdrawn(*t)) return "consent withdrawn";
  const auto cut = takes_.cut(id);
  if (mode == "raw") {
    const auto raw = takes_.raw(id);
    if (!raw) return "there is no raw recording of this take";
    out = *raw;
    return "";
  }
  if (!cut || cut->empty()) return "the recording cannot be read";
  if (mode == "trimmed") {
    out = *cut;
    return "";
  }
  if (mode == "radio") {
    out = applyRadioEffect(*cut, TakeStore::kRate, RadioIntensity::Light, 1);
    return "";
  }
  if (mode != "sentence") return "unknown way to play";
  const std::string dir = copy_->dir();
  const Loaded l = loadAll(dir);
  SpokenForms forms;
  std::string error;
  if (!loadSpokenForms(dir, forms, error)) return error;
  for (const auto& [f, e] : l.exercises) {
    Needs n;
    if (!needs(e, forms, n, error)) return error;
    const auto it = n.find(t->clipId);
    if (it == n.end()) continue;
    StudioClip c;
    std::vector<std::string> sequence;
    if (std::string why = say(e, forms, t->clipId, it->second, c, &sequence); !why.empty()) return why;
    const AssemblerConfig a;
    std::vector<std::int16_t> pcm;
    for (const auto& clip : sequence) {
      if (clip == kCommaPause) {
        append(pcm, std::size_t(a.commaGapMs));
        continue;
      }
      if (!pcm.empty()) append(pcm, std::size_t(a.wordGapMs));
      std::optional<std::vector<std::int16_t>> s;
      if (clip == t->clipId) s = cut;
      else
        for (const auto& x : takes_.takes())
          if (x.clipId == clip && !withdrawn(x) && (x.state == Take::State::Primary || (!s && x.state == Take::State::Alternate))) s = takes_.cut(x.id);
      if (!s) {
        note += (note.empty() ? "Missing: " : ", ") + clip;
        s = beep();
      }
      pcm.insert(pcm.end(), s->begin(), s->end());
    }
    out = applyRadioEffect(pcm, TakeStore::kRate, RadioIntensity::Light, 1);
    return "";
  }
  return "no scenario needs this clip";
}

}  // namespace atc
