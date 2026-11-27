#include "app/HistoryMock.h"

#include <algorithm>
#include <memory>

namespace {
using Kind = HistoryEntry::Kind;

HistoryRow row(const char* id, const char* who, const char* scenario, std::int64_t at, HistoryRow::End end, int done, int errors, int missed, int ms, int stt, int ptt) {
  HistoryRow r;
  r.id = id;
  r.trainee = who;
  r.scenario = scenario;
  r.startedMs = at;
  r.endedMs = at + 197'000;
  r.end = end;
  r.stepsDone = done;
  r.steps = 3;
  r.errors = errors;
  r.missed = missed;
  r.responseMs = ms;
  r.suspected = stt;
  r.ptt = ptt;
  return r;
}

HistoryEntry said(Kind k, const char* time, const char* text, const char* outcome = "", int level = 0) {
  HistoryEntry e;
  e.kind = k;
  e.time = time;
  e.text = text;
  e.outcome = outcome;
  e.level = level;
  return e;
}

HistoryDetail detail(const HistoryRow& r) {
  HistoryDetail d;
  d.row = r;
  d.callsign = "EAGLE-421";
  d.model = "Whisper tiny + generated prompt";
  d.pttNotes = {"Released early, Line up, attempt 1", "Keyed over the pilot, Taxi"};
  if (r.stepsDone < r.steps) d.missedSteps = {"Takeoff"};
  HistoryEntry a = said(Kind::Atc, "10:41:32", "eagle four two one taxi via alpha three runway zero nine", "CORRECT", 1);
  a.step = "Taxi";
  a.heard = a.text;
  a.parsed = "TWY A3   RWY 09";
  a.expected = "TWY A3   RWY 09";
  a.cls = "RECOGNIZED";
  a.delay = "2.0 s";
  a.audio = true;
  HistoryEntry b = said(Kind::Atc, "10:42:10", "eagle four two one line up runway two seven", "INCORRECT runway", 2);
  b.step = "Line up";
  b.heard = b.text;
  b.parsed = "RWY 27";
  b.expected = "RWY 09";
  b.cls = "RECOGNIZED";
  b.delay = "2.1 s";
  b.error = true;
  b.audio = true;
  HistoryEntry c = said(Kind::Atc, "10:42:31", "eagle four two one line up runway 0.9", "AMBIGUOUS", 2);
  c.step = "Line up";
  c.heard = c.text;
  c.parsed = "RWY \"0.9\" ?";
  c.expected = "RWY 09";
  c.cls = "AMBIGUOUS";
  c.delay = "2.4 s";
  c.suspected = true;
  d.entries = {said(Kind::Pilot, "10:41:20", "request taxi, EAGLE-421"), a, said(Kind::Pilot, "10:41:38", "taxi via A3, runway 09, EAGLE-421"),
               said(Kind::Ptt, "10:42:08", "Keyed over the pilot", "PILOT CUT", 2), b, said(Kind::Pilot, "10:42:16", "query instruction, EAGLE-421"), c,
               said(Kind::Pilot, "10:42:38", "say again, EAGLE-421"), said(Kind::Note, "10:43:10", "No ATC call for 30 s", "MISSED RESPONSE", 2)};
  if (r.suspected > 0) d.suspectedNotes = {"Line up, attempt 2: runway heard as \"0.9\", classed AMBIGUOUS"};
  return d;
}
}  // namespace

HistoryBackend mockHistory(std::int64_t now) {
  using E = HistoryRow::End;
  const std::int64_t day = 86'400'000;
  auto all = std::make_shared<std::vector<HistoryDetail>>();
  std::vector<HistoryRow> rows{row("h1", "S01", "Departure 01", now - 3'600'000, E::Completed, 3, 1, 0, 4100, 1, 2),
                               row("h2", "S01", "Departure 01", now - day, E::Stopped, 1, 2, 1, 6000, 0, 1),
                               row("h3", "S01", "Departure 01", now - day - 3'000'000, E::Fault, 2, 0, 0, 4400, 3, 0),
                               row("h4", "S02", "Departure 01", now - 2 * day, E::Completed, 3, 3, 2, 7100, 0, 0),
                               row("h5", "S01", "Ground 01", now - 3 * day, E::Completed, 3, 0, 0, 3900, 0, 0),
                               row("h6", "S01", "Departure 01", now - 60 * day, E::Completed, 3, 2, 0, -1, 0, 0),
                               row("g1", "", "Departure 01", now - 2'000'000, E::Completed, 3, 0, 0, 3000, 0, 0),
                               row("g2", "", "Departure 01", now - day, E::Stopped, 0, 0, 0, -1, 0, 0)};
  rows[0].plan = "Departure, runway 27";
  rows[0].run = 3;
  rows[4].assessed = true;
  rows[4].placeholder = false;
  rows[5].scenarioChanged = true;
  for (const HistoryRow& r : rows) all->push_back(detail(r));
  const auto find = [all](const std::string& id) { return std::find_if(all->begin(), all->end(), [&](const HistoryDetail& d) { return d.row.id == id; }); };

  HistoryBackend b;
  b.list = [all](const std::string& person, auto done) {
    std::vector<HistoryRow> l;
    for (const HistoryDetail& d : *all)
      if (!d.row.trainee.empty() && (person.empty() || d.row.trainee == person)) l.push_back(d.row);
    done(l, "");
  };
  b.open = [all, find](const std::string& id, auto done) {
    const auto at = find(id);
    if (at == all->end()) return done({}, "the result is gone");
    done(*at, "");
  };
  b.reclassify = [all, find](const std::string& id, int no, const std::string& verdict, const std::string& note, HistoryBackend::Done done) {
    const auto at = find(id);
    if (at == all->end() || no < 0 || no >= int(at->entries.size())) return done("the entry is gone");
    if (verdict != "trainee" && verdict != "stt" && verdict != "none") return done("unknown class");  // as the live backend
    auto& e = at->entries[std::size_t(no)];
    if (e.reviews.empty()) ++at->row.reclassified;
    e.reviews.push_back({verdict, note, "instructor", "mock time"});
    done("");
  };
  b.remove = [all](const std::string& id, HistoryBackend::Done done) {
    std::erase_if(*all, [&](const HistoryDetail& d) { return d.row.id == id; });
    done("");
  };
  b.play = [](const std::string&, int, bool, HistoryBackend::Done done) { done("Mock backend: no audio playback."); };
  b.stopPlay = [] {};
  return b;
}
