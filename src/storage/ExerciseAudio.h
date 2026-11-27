#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace atc {

// Exercise audio with consent (11, D-108, D-341). <dir>/<session>/tx<N>.wav and index.jsonl, owner only, never in
// git. Only a run begun with a speaker is kept; the answer of today is asked again before each file (a "no" writes
// nothing more). Files are written by one worker thread; the audio leaves memory when written. Thread-safe.
class ExerciseAudio {
 public:
  struct Answer {
    bool yes = false;
    std::string date, version;  // of the recording consent (D-135)
  };
  struct Config {
    std::string dir;
    long minFreeMb = 200;
    std::function<long()> freeMb;                          // empty: ask the file system
    std::function<Answer(const std::string& speaker)> answer;  // today's recording consent; any thread
  };
  struct Clip {
    std::vector<std::int16_t> samples;
    int rate = 0, preRoll = 0, keyed = 0;  // keyed: samples from the press to the release
  };

  explicit ExerciseAudio(Config c);
  ~ExerciseAudio();  // what is queued is written first

  // A run whose audio is kept (EA-1, EA-3): session and the trainee's ID. Empty speaker: nothing is kept.
  void begin(const std::string& session, const std::string& speaker);
  void end();
  // The audio of one closed transmission of the run; any thread, returns at once.
  void transmission(std::uint64_t tx, std::vector<std::int16_t> audio, int rate, int preRoll, int keyed);

  std::set<std::uint64_t> saved(const std::string& session) const;
  std::string speaker(const std::string& session) const;
  std::string note(const std::string& session) const;  // e.g. "Exercise audio not saved: disk nearly full"
  std::optional<Clip> read(const std::string& session, std::uint64_t tx) const;
  bool remove(const std::string& session);  // the caller logs it first (D-102)
  void wait();                               // tests

 private:
  void post(std::function<void()> f);
  void run();
  std::string folder(const std::string& session) const;
  const Config cfg_;
  mutable std::mutex m_;
  std::string session_, speaker_;
  bool noted_ = false;
  std::mutex qm_;
  std::condition_variable cv_, idle_;
  std::deque<std::function<void()>> q_;
  bool stop_ = false, busy_ = false;
  std::thread thread_;
};

}  // namespace atc
