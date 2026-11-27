#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "app/HistoryBackend.h"
#include "core/Access.h"
#include "runtime/RadioCore.h"
#include "storage/ExerciseAudio.h"

namespace atc {

// Assessment and Debrief without a screen (5e, 07, D-335). Qt-free. Everything is rebuilt from the event logs with
// the same code as a live result; the instructor's reviews are kept beside a log, which is never changed.
// History rules (D-124, D-127, D-131): a trainee sees their own, the instructor all, guests are in nobody's.
class HistoryService {
 public:
  struct Config {
    std::string data;  // scenarios, for building a result
    std::string logs;  // the folder of the event logs
    std::shared_ptr<ExerciseAudio> audio;                     // empty: no exercise audio
    std::function<bool(const std::string& speaker)> mayPlay;  // recording consent today (EA-2)
  };
  struct Rules {
    std::function<std::pair<Role, std::string>()> who;  // GUI thread
    std::function<std::string()> now;                   // "YYYY-MM-DD HH:MM"
    std::function<void(std::vector<std::int16_t>, int rate)> play;  // through the radio; any thread
    std::function<void()> stop;
  };
  HistoryService(Config c, Rules r);
  ~HistoryService();
  HistoryBackend backend();
  // For Settings (D-131): the guest results and their deletion, each logged first.
  int guests() const;
  std::string deleteGuests(int expected, int& count);
  void wait();  // tests
  void onPlay(LocalEnd e);  // the radio's answer to play()

 private:
  void post(std::function<void()> f);
  void run();
  std::vector<std::string> files() const;
  std::string file(const std::string& id) const;  // empty if there is no such log
  const Config cfg_;
  const Rules rules_;
  std::mutex qm_;
  std::condition_variable cv_, idle_;
  std::deque<std::function<void()>> q_;
  bool stop_ = false, busy_ = false;
  std::mutex pm_;
  HistoryBackend::Done playing_;
  std::thread thread_;
};

}  // namespace atc
