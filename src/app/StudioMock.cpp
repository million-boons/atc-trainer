#include "app/StudioMock.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
using State = StudioTake::State;

struct Bank {
  StudioContent content;
  std::vector<StudioClip> clips;
  StudioEvents ev;
  QTimer timer;
  QElapsedTimer since;
  std::string speaker, recording;
  int nextTake = 100;
};

StudioTake take(int id, const std::string& clip, State s, bool synthetic = true, const std::string& speaker = {}) {
  StudioTake t;
  t.id = std::to_string(id);
  t.clipId = clip;
  t.number = 1;
  t.synthetic = synthetic;
  t.speaker = speaker;
  t.state = s;
  t.seconds = 0.84;
  t.levelDb = -20;
  t.trimStart = 0.18;
  t.trimEnd = 0.79;
  return t;
}

std::vector<float> wave() {
  std::vector<float> l;
  for (int i = 0; i < 240; ++i) {
    const double x = i / 240.0, env = x > 0.2 && x < 0.75 ? 0.25 + 0.5 * std::abs(std::sin(x * 23)) : 0.015;
    l.push_back(float(-env * (0.7 + 0.3 * std::sin(i * 1.7))));
    l.push_back(float(env * (0.7 + 0.3 * std::cos(i * 2.3))));
  }
  return l;
}

const char* const kPos[] = {"initial", "medial", "final"};
std::string upper(const std::string& s) { return QString::fromStdString(s).toUpper().replace(' ', '_').toStdString(); }

// Mock only: the real split is parsePhraseTemplate (voice/PhraseTemplate).
StudioPreview split(const StudioEntry& e, int index) {
  StudioPreview p;
  const QString text = index < int(e.texts.size()) ? QString::fromStdString(e.texts[std::size_t(index)]) : QString();
  if (text.count('{') != text.count('}')) {
    p.error = "a { or } is missing";
    return p;
  }
  static const QRegularExpression part(QStringLiteral("\\{[^}]*\\}|[^{},]+|,"));
  for (auto it = part.globalMatch(text); it.hasNext();) {
    const QString w = it.next().captured().trimmed();
    if (w.isEmpty()) continue;
    if (w == ",") {
      if (!p.segments.empty()) p.segments.back().commaAfter = true;
      continue;
    }
    StudioSegment s;
    s.dynamic = w.startsWith('{');
    s.text = (s.dynamic ? w.mid(1, w.size() - 2) : w).toStdString();
    if (s.dynamic && e.reply) s.pool = s.text;
    p.segments.push_back(s);
    static const QRegularExpression typed(QStringLiteral("\\b(zero|one|two|three|four|five|six|seven|eight|niner?)\\b"));
    if (!s.dynamic && w.contains(typed)) p.lint.push_back("\"" + s.text + "\" typed: use a {slot}");
  }
  const std::size_t n = p.segments.size();
  for (std::size_t i = 0; e.reply && i < n; ++i) {
    StudioSegment& s = p.segments[i];
    s.position = kPos[i + 1 == n ? 2 : i == 0 ? 0 : 1];
    if (!s.dynamic) s.clipId = "PH_" + upper(s.text) + "@" + s.position;
  }
  return p;
}

StudioEntry entry(const char* id, const char* text, const char* used, bool reply = true) {
  StudioEntry e;
  e.id = id;
  e.reply = reply;
  e.texts = {text};
  if (*used) e.usedBy = {used};
  return e;
}

void needClip(Bank& b, const std::string& id, const std::string& words, const StudioSegment& s, const std::string& value, const StudioEntry& e) {
  auto at = std::find_if(b.clips.begin(), b.clips.end(), [&](const StudioClip& c) { return c.id == id; });
  if (at == b.clips.end()) {
    StudioClip c;
    c.id = id;
    c.words = words;
    c.pool = s.pool;
    c.value = value;
    c.position = s.position;
    c.sayTarget = words;
    at = b.clips.insert(b.clips.end(), c);
  }
  if (std::find(at->replies.begin(), at->replies.end(), e.id) == at->replies.end()) at->replies.push_back(e.id);
}

void collect(Bank& b) {
  const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> pools{
      {"callsign", {{"EAGLE-421", "eagle four two one"}, {"FALCON-732", "falcon seven three two"}}},
      {"runway", {{"09", "zero niner"}, {"27", "two seven"}}},
      {"taxiway", {{"A3", "alpha three"}, {"B2", "bravo two"}}}};
  for (StudioClip& c : b.clips) c.replies.clear();
  for (const StudioEntry& e : b.content.entries) {
    if (!e.reply) continue;
    const StudioPreview p = split(e, 0);
    for (std::size_t i = 0; i < p.segments.size(); ++i) {
      const StudioSegment& s = p.segments[i];
      if (!s.dynamic) {
        needClip(b, s.clipId, s.text, s, {}, e);
      } else {
        for (const auto& [name, values] : pools)
          if (name == s.pool)
            for (const auto& [v, words] : values) needClip(b, upper(name) + "_" + v + "@" + s.position, words, s, v, e);
      }
    }
    // "Say this" for the clips of this reply.
    for (StudioClip& c : b.clips) {
      if (c.replies.empty() || c.replies.front() != e.id) continue;
      QStringList before, after;
      bool past = false;
      for (const StudioSegment& s : p.segments) {
        const bool here = s.dynamic ? s.pool == c.pool && s.position == c.position : s.clipId == c.id;
        QString w = QString::fromStdString(s.text);
        for (const auto& [name, values] : pools)
          if (s.dynamic && name == s.pool) w = QString::fromStdString(values.front().second);
        if (here) past = true;
        else (past ? after : before) << w + (s.commaAfter ? "," : "");
      }
      c.sayBefore = before.join(' ').toStdString();
      c.sayAfter = after.join(' ').toStdString();
    }
  }
  for (StudioClip& c : b.clips) c.needed = !c.replies.empty();
  std::erase_if(b.clips, [](const StudioClip& c) { return !c.needed && c.takes.empty(); });
}

StudioTake* findTake(Bank& b, const std::string& id) {
  for (StudioClip& c : b.clips)
    for (StudioTake& t : c.takes)
      if (t.id == id) return &t;
  return nullptr;
}
}  // namespace

StudioBackend mockStudio() {
  auto b = std::make_shared<Bank>();
  auto& en = b->content.entries;
  en.push_back(entry("REQUEST_TAXI", "{callsign}, request taxi", "DEPARTURE-01  Taxi: pilot call"));
  en.push_back(entry("READY_DEPARTURE", "{callsign}, ready for departure runway {runway}", "DEPARTURE-01  Line up: pilot call"));
  en.push_back(entry("READBACK_TAXI", "taxi via {taxiway} runway {runway}, {callsign}", "DEPARTURE-01  Taxi: CORRECT"));
  en.push_back(entry("READBACK_TAXI_HOLD_SHORT", "taxi via {taxiway} hold short runway {runway}, {callsign}", "DEPARTURE-01  Taxi: CORRECT, hold short"));
  en.push_back(entry("READBACK_LINE_UP", "lining up runway {runway} and waiting, {callsign}", "DEPARTURE-01  Line up: CORRECT"));
  en.push_back(entry("READBACK_TAKEOFF", "runway {runway} cleared for takeoff, {callsign}", "DEPARTURE-01  Takeoff: CORRECT"));
  en.push_back(entry("REQUEST_MISSING", "say again all parts, {callsign}", "DEPARTURE-01  every step: MISSING_INFO"));
  en.push_back(entry("SAY_AGAIN", "say again, {callsign}", "DEPARTURE-01  every step: AMBIGUOUS, UNINTELLIGIBLE"));
  en.push_back(entry("CHALLENGE", "confirm instruction, {callsign}", "DEPARTURE-01  every step: CONFLICT"));
  en.push_back(entry("QUERY", "query instruction, {callsign}", "DEPARTURE-01  every step: UNEXPECTED, INCORRECT"));
  en.push_back(entry("CALL_AGAIN", "{callsign}, standing by", "DEPARTURE-01  every step: TIMEOUT"));
  StudioEntry taxi = entry("TAXI_INSTRUCTION", "{callsign} taxi via {taxiway} runway {runway}", "DEPARTURE-01  Taxi", false);
  taxi.texts.push_back("{callsign} taxi via {taxiway} hold short runway {runway}");
  en.push_back(taxi);
  en.push_back(entry("LINE_UP_AND_WAIT", "{callsign} runway {runway} line up and wait", "DEPARTURE-01  Line up", false));
  en.push_back(entry("TAKEOFF_CLEARANCE", "{callsign} runway {runway} cleared for takeoff", "DEPARTURE-01  Takeoff", false));
  en.push_back(entry("LANDING_CLEARANCE", "{callsign} runway {runway} cleared to land", "", false));
  b->content.scenarios = {{"DEPARTURE-01", "SAMPLE-AF"}};
  b->content.voice = "mock voice";
  b->content.activeVoice = "mock Piper";
  b->content.revision = "1";
  collect(*b);
  int n = 0;
  for (StudioClip& c : b->clips) {
    c.bankWords = c.words;
    if (c.id == "PH_HOLD_SHORT_RUNWAY@medial") {  // missing
      c.bankWords.clear();
      continue;
    }
    c.takes.push_back(take(++n, c.id, State::Primary));
    if (c.id == "RUNWAY_09@medial") {  // an undecided and a rejected take beside the primary one
      c.bankWords = "zero nine";
      StudioTake p = take(++n, c.id, State::Pending, false, "S01");
      p.number = 3;
      p.levelDb = -34;
      p.warnings = {"quiet: check the microphone"};
      c.takes.insert(c.takes.begin(), p);
      StudioTake r = take(++n, c.id, State::Rejected, false, "S01");
      r.number = 2;
      c.takes.push_back(r);
    }
    if (c.id == "RUNWAY_27@medial") {  // only the take of somebody who withdrew
      c.takes = {take(++n, c.id, State::Withdrawn, false, "S02")};
    }
    if (c.id == "TAXIWAY_B2@medial") c.takes.front().state = State::Alternate;
  }
  StudioClip old;
  old.id = "PH_REQUEST_START@final";
  old.words = old.bankWords = old.sayTarget = "request start";
  old.position = "final";
  old.needed = false;
  old.takes = {take(++n, old.id, State::Primary)};
  b->clips.push_back(old);

  b->timer.setInterval(50);
  Bank* raw = b.get();  // the timer is a member: no cycle
  QObject::connect(&b->timer, &QTimer::timeout, &b->timer, [raw] {
    const int t = int(raw->since.elapsed());
    if (raw->ev.elapsed) raw->ev.elapsed(t);
    if (raw->ev.level) raw->ev.level(-16 + 5 * std::sin(t / 130.0));
  });
  const auto stop = [raw](const std::string& why) {
    if (raw->recording.empty()) return;
    raw->timer.stop();
    for (StudioClip& c : raw->clips) {
      if (c.id != raw->recording) continue;
      StudioTake t = take(++raw->nextTake, c.id, State::Pending, false, raw->speaker);
      for (const StudioTake& x : c.takes) t.number = std::max(t.number, x.number + 1);
      c.takes.insert(c.takes.begin(), t);
      raw->recording.clear();
      if (raw->ev.recording) raw->ev.recording(false);
      if (raw->ev.recordingStopped) raw->ev.recordingStopped(why);
      if (raw->ev.takeReady) raw->ev.takeReady(t);
      return;
    }
  };

  StudioBackend s;
  s.setEvents = [b](StudioEvents e) { b->ev = std::move(e); };
  s.load = [b](auto done) { done(b->content, ""); };
  s.preview = [b](const StudioEntry& e, int index, auto done) {
    StudioPreview p = split(e, index);
    if (e.reply && p.error.empty()) {  // as if it were saved
      Bank test;
      test.content = b->content;
      test.clips = b->clips;
      for (StudioEntry& x : test.content.entries)
        if (x.reply && x.id == e.id) x.texts = e.texts;
      collect(test);
      for (const StudioClip& c : test.clips) {
        const auto was = std::find_if(b->clips.begin(), b->clips.end(), [&](const StudioClip& o) { return o.id == c.id; });
        if (c.needed && was == b->clips.end()) p.missing.push_back(c.id);
        if (!c.needed && was != b->clips.end() && was->needed) p.unused.push_back(c.id);
      }
    }
    done(p);
  };
  s.save = [b](StudioEntry e, const std::string& revision, StudioBackend::Done done) {
    if (revision != b->content.revision) return done("the file changed on disk: undo and edit again");
    if (e.validated && e.placeholderFile) return done("the file is marked PLACEHOLDER");
    for (StudioEntry& x : b->content.entries)
      if (x.id == e.id && x.reply == e.reply) {
        if (x.texts != e.texts) e.validated = false;  // as the live backend (D-332)
        x = e;
      }
    b->content.changedHere = true;
    b->content.revision += "1";
    collect(*b);
    done("");
  };
  s.addReply = [b](StudioEntry e, const std::string&, StudioBackend::Done done) {
    e.usedBy.clear();
    b->content.entries.push_back(e);
    b->content.changedHere = true;
    b->content.revision += "1";
    collect(*b);
    done("");
  };
  s.clips = [b](const std::string&, auto done) { done(b->clips, ""); };
  s.peaks = [](const std::string&, auto done) { done(wave(), ""); };
  s.startSession = [b](const std::string& speaker, StudioBackend::Done done) {
    b->speaker = speaker;
    if (b->ev.roomTone) b->ev.roomTone(-62, "");  // the mock does not wait
    done("");
  };
  s.endSession = [b, stop] {
    stop("");
    b->speaker.clear();
  };
  s.setPedal = [](bool, const std::string&) {};
  s.record = [b](const std::string& clip, StudioBackend::Done done) {
    if (b->speaker.empty()) return done("no recording session");
    b->recording = clip;
    b->since.start();
    b->timer.start();
    done("");
    if (b->ev.recording) b->ev.recording(true);
  };
  s.stopRecord = [stop] { stop(""); };
  s.setTrim = [b](const std::string& id, double start, double end, StudioBackend::Done done) {
    StudioTake* t = findTake(*b, id);
    if (!t) return done("the take is gone");
    t->trimStart = start;
    t->trimEnd = end;
    t->seconds = 1.38 * (end - start);
    if (b->ev.takeReady) b->ev.takeReady(*t);
    done("");
  };
  s.play = [](const std::string&, const std::string&, StudioBackend::Done done) { done("Mock backend: no audio playback."); };
  s.stopPlay = [] {};
  s.accept = [b](const std::string& id, bool primary, StudioBackend::Done done) {
    StudioTake* t = findTake(*b, id);
    if (!t) return done("the take is gone");
    for (StudioClip& c : b->clips)
      if (c.id == t->clipId) {
        c.bankWords = c.words;
        for (StudioTake& o : c.takes)
          if (primary && o.state == State::Primary) o.state = State::Alternate;
      }
    t->state = primary ? State::Primary : State::Alternate;
    done("");
  };
  s.reject = [b](const std::string& id, StudioBackend::Done done) {
    StudioTake* t = findTake(*b, id);
    if (!t) return done("the take is gone");
    t->state = State::Rejected;
    done("");
  };
  s.deleteTake = [b](const std::string& id, StudioBackend::Done done) {
    for (StudioClip& c : b->clips) std::erase_if(c.takes, [&](const StudioTake& t) { return t.id == id; });
    done("");
  };
  s.exportChanges = [](const std::string&, StudioBackend::Done done) { done("Mock backend: nothing is written."); };
  return s;
}
