#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "audio/UtteranceChecks.h"
#include "stt/SpeechRecognizer.h"

namespace atc {

// One recognition of a recording, with the exact model settings (D-39).
struct StoredResult {
  std::string modelId, modelName;
  std::string modelConfig;  // JSON from describeModel
  std::string text;
  std::vector<SttWord> words;
  double releaseToTextMs = 0;
};

struct StoredRecording {
  std::string key;           // file name without extension
  std::int64_t startedMs = 0;  // Unix time
  int tx = 0;
  std::string speaker;         // ID code of the trainee record (D-101); required
  std::string pttSource, end;  // end: normal, stuck, lost
  bool gap = false;
  int rate = 16000;
  int samples = 0, pressSample = 0, endSample = 0;
  UtteranceReport report;
  std::vector<StoredResult> results;
};

struct StoreConfig {
  std::string dir;
  long minFreeMb = 200;      // nothing is saved below this (recordingsMinFreeMb, D-306)
  std::size_t maxQueue = 16;
  std::function<long()> freeMb;  // empty: ask the file system
};

// Speech Lab recordings: <key>.wav and <key>.json in one local folder (D-44). The folder is
// created readable by the owner only. Nothing is sent anywhere. Nothing is deleted automatically (D-102):
// remove() is for the instructor role, which the caller checks, and is logged in deletions.log, which is
// never deleted.
// All file work runs on one worker thread; callbacks run on that thread.
class RecordingStore {
 public:
  using Saved = std::function<void(const std::string& key, bool ok, const std::string& why)>;
  RecordingStore(StoreConfig cfg, Saved onSaved);
  ~RecordingStore();  // finishes queued writes
  RecordingStore(const RecordingStore&) = delete;
  RecordingStore& operator=(const RecordingStore&) = delete;

  static std::string makeKey(std::int64_t startedMs, int tx);

  void save(StoredRecording rec, std::vector<std::int16_t> audio);
  void addResult(std::string key, StoredResult result);
  // Newest first. unreadable: files that are not valid recordings.
  void list(std::size_t max, std::function<void(std::vector<StoredRecording>, int unreadable)> done);
  void loadAudio(std::string key, std::function<void(std::optional<std::vector<std::int16_t>>)> done);
  // Logs first, then removes the WAV and the JSON. why is empty if both are gone.
  void remove(std::string key, std::string role, std::function<void(const std::string& why)> done);
  const std::string& dir() const { return cfg_.dir; }

 private:
  bool post(std::function<void()> f, bool always);
  void run();
  std::string prepare();  // empty if the folder is usable and has space
  std::string path(const std::string& key, const char* ext) const { return cfg_.dir + "/" + key + ext; }
  bool writeMeta(const StoredRecording& r);
  std::optional<StoredRecording> readMeta(const std::string& key) const;

  StoreConfig cfg_;
  Saved saved_;
  std::mutex m_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> q_;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace atc
