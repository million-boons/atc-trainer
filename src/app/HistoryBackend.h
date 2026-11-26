#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// What Assessment and Debrief needs from outside (04 section 5e, 07). Built by runtime (HistoryService, D-335).
// Texts: empty, or the reason of a refusal. Callbacks may come on any thread, after the call has returned.
// Everything is rebuilt from the event logs; nothing here is typed, except the instructor's notes.

struct HistoryRow {
  std::string id{};            // session
  std::string trainee{};       // record ID; empty: a guest, who is in nobody's history (D-127)
  std::string scenario{}, plan{};
  int run = 0;
  bool assessed = false, placeholder = true;
  std::int64_t startedMs = 0, endedMs = 0;
  enum class End { Completed, Stopped, Fault };
  End end = End::Completed;    // a log that stops early is a fault
  int stepsDone = 0, steps = 0, errors = 0, missed = 0, suspected = 0, ptt = 0, reclassified = 0;
  int responseMs = -1;         // median; -1: none
};

struct HistoryReview {
  std::string verdict{}, note{}, role{}, at{};
};

struct HistoryEntry {
  enum class Kind { Pilot, Atc, Ptt, Note };
  Kind kind = Kind::Note;
  std::string time{}, text{}, outcome{};
  int level = 0;               // LabController.Level
  bool error = false;          // a trainee error, as the engine judged it
  bool suspected = false;      // suspected recognition error: never counted as a trainee error
  std::string step{}, heard{}, parsed{}, expected{}, cls{}, delay{};
  bool audio = false;          // saved with consent
  std::vector<HistoryReview> reviews{};  // beside the original, which never changes
};

struct HistoryDetail {
  HistoryRow row{};
  std::string model{}, callsign{};
  bool fallbackPtt = false;
  std::vector<std::string> missedSteps{}, pttNotes{}, suspectedNotes{};
  std::vector<HistoryEntry> entries{};
};

struct HistoryBackend {
  using Done = std::function<void(const std::string& why)>;
  // person empty: all trainees (instructor). The backend checks the role again and leaves guests out.
  std::function<void(const std::string& person, std::function<void(std::vector<HistoryRow>, const std::string& why)>)> list;
  std::function<void(const std::string& id, std::function<void(HistoryDetail, const std::string& why)>)> open;
  // instructor; verdict: "trainee", "stt" or "none" (the screen shows words for them)
  std::function<void(const std::string& id, int entry, const std::string& verdict, const std::string& note, Done)> reclassify;
  std::function<void(const std::string& id, Done)> remove;   // instructor; logged first (H-3)
  std::function<void(const std::string& id, int entry, bool keyedOnly, Done)> play;
  std::function<void()> stopPlay;
};
