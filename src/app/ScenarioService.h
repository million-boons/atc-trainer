#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "app/ScenarioBackend.h"
#include "voice/VoiceBank.h"

namespace atc {

// Scenario Selection without a screen (5d, 07, D-335). Qt-free. Reads data/scenarios on a worker thread; select()
// is checked on the GUI thread and handed to the exercise there.
class ScenarioService {
 public:
  struct Config {
    std::string data, voices;
  };
  struct Rules {
    std::function<bool()> exerciseActive;               // GUI thread
    std::function<std::string()> phraseology;           // the folder in use (D-318)
    VoiceBank::MayUse mayUse;
    std::function<void(std::function<void()>)> gui;     // later, on the GUI thread
    std::function<void(const std::string& file)> select; // GUI thread: the scenario of the Dashboard; replaces a plan
  };
  ScenarioService(Config c, Rules r);
  ~ScenarioService();
  ScenarioBackend backend();
  void wait();  // tests

 private:
  void post(std::function<void()> f);
  void run();
  std::vector<std::string> files() const;
  const Config cfg_;
  const Rules rules_;
  std::mutex qm_;
  std::condition_variable cv_, idle_;
  std::deque<std::function<void()>> q_;
  bool stop_ = false, busy_ = false;
  std::thread thread_;
};

}  // namespace atc
