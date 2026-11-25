#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "voice/ClipTrim.h"

namespace atc {

// The Studio's takes (D-123, D-132, D-133, D-331). Two folders, both outside every git work tree:
//   bank     manifest.json and the cut clips of accepted takes: what VoiceBank reads
//   pending  raw recording and description of every take: read by nothing else
// The manifest alone says whether a take is accepted, so a crash between two steps leaves a bank that loads.
// Not thread-safe: one worker owns it. Every change reports empty, or why not.
struct TakeAnswer {  // of a person; version: of the consent text (D-135)
  bool consent = false;
  std::string date, version;
};
struct TakeSpeaker {
  std::string id;  // code, never a name
  TakeAnswer recording, pilotVoice;
};
struct Take {
  enum class State { Pending, Primary, Alternate, Rejected };
  std::string id, clipId, words;
  int number = 0;
  bool synthetic = false;
  TakeSpeaker speaker;       // empty for a synthetic take
  State state = State::Pending;
  std::int64_t recordedMs = 0;
  std::size_t samples = 0;   // of the raw recording; 0: there is none
  std::size_t trimStart = 0, trimEnd = 0;
  double speechDb = 0, gainDb = 0;
  std::size_t durationMs = 0;
  std::vector<std::string> warnings;
  std::string file;          // in the bank; empty unless it is in the manifest
};
struct NewTake {
  std::string clipId, words;
  TakeSpeaker speaker;
  std::int64_t recordedMs = 0;
  std::vector<std::int16_t> raw;  // 16 kHz
  double noiseFloorDb = -90;      // the session's room tone
  std::vector<std::string> warnings;
};

class TakeStore {
 public:
  static constexpr int kRate = 16000;
  struct Config {
    std::string bank, pending;  // folders
    std::string voice;          // name of the bank, for a new manifest
    TrimConfig trim;            // noise floor: from each take
    long minFreeMb = 200;
    std::function<long()> freeMb;  // empty: ask the file system
  };
  explicit TakeStore(Config c) : cfg_(std::move(c)) {}

  std::string load();  // reads both folders; no manifest yet is an empty bank
  const std::vector<Take>& takes() const { return takes_; }
  const Take* find(const std::string& id) const;
  std::string bankWords(const std::string& clipId) const;  // of the manifest; empty: no active take
  int unlisted() const { return unlisted_; }               // clips in the bank that no manifest entry names
  bool placeholder() const { return placeholder_; }

  // Trims, and keeps the raw recording as a pending take. Nothing is written if it says no.
  std::string add(NewTake t, Take& out);
  std::optional<std::vector<std::int16_t>> raw(const std::string& id) const;
  std::optional<std::vector<std::int16_t>> cut(const std::string& id) const;  // as it is, or would be, in the bank
  std::string setTrim(const std::string& id, double start, double end);       // fractions of the raw recording
  // words: of the data now. A take that says other words is refused, and active takes of the clip that say other
  // words are rejected: a clip never has two wordings. A replaced primary becomes an alternate, a synthetic one is
  // rejected (ST-3).
  std::string accept(const std::string& id, bool primary, const std::string& words);
  std::string reject(const std::string& id);  // the files stay
  // Only a pending or rejected take, or any whose consent is withdrawn. The line in deletions.log is on disk first.
  std::string remove(const std::string& id, bool withdrawn, const std::string& role, const std::string& when);

 private:
  std::string prepare(const std::string& dir) const;
  std::string writeTake(const Take& t) const;
  std::string writeManifest(const std::string& bytes);
  std::string path(const std::string& id, const char* ext) const { return cfg_.pending + "/" + id + ext; }
  std::string readManifest(std::string& bytes) const;
  Take* mutableFind(const std::string& id);

  const Config cfg_;
  std::vector<Take> takes_;
  std::map<std::string, std::string> words_;
  std::string manifest_;  // bytes as read: a change on disk by somebody else is noticed
  bool placeholder_ = true;
  int unlisted_ = 0;
};

}  // namespace atc
