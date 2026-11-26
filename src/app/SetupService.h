#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "app/SetupBackend.h"
#include "core/Access.h"

namespace atc {

// The full Settings without a screen (5f, 07, D-335), and the Studio's bank choice (S-5). Qt-free.
// ASSUMED (needs user review): values chosen here are kept in <writable>/setup.json (owner only) and take effect at
// the next start of the program, except theme, motion and clock, which the screen applies at once. The program reads
// them with startingValues() over ui.json. Device lists and pedal learning are not built: those rows say so.
class SetupService {
 public:
  struct Config {
    std::string file;                 // setup.json
    std::string activeBank, studioBanks;  // the bank of today; the folder of the Studio's banks
    std::map<std::string, std::string> starting;  // key -> value of ui.json, as text
    std::vector<SetupFact> facts;
    std::vector<std::string> models;  // installed exercise models
  };
  struct Rules {
    std::function<Role()> role;          // GUI thread
    std::function<bool()> busy;          // an exercise or a transmission
    std::function<std::string(const std::string& oldPin, const std::string& newPin)> changePin;  // slow
    std::function<void(SetupBackend::Done)> testSentence;
    std::function<int()> guests;
    std::function<std::string(int expected, int& count)> deleteGuests;
  };
  SetupService(Config c, Rules r);
  ~SetupService();
  SetupBackend backend();
  // What the program starts with: key -> value, only those chosen by the user. Read before anything starts.
  static std::map<std::string, std::string> stored(const std::string& file);

 private:
  std::string save();
  SetupContent content() const;
  const Config cfg_;
  const Rules rules_;
  mutable std::mutex m_;
  std::map<std::string, std::string> values_;
  void post(std::function<void()> f);
  std::mutex qm_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> q_;
  bool stop_ = false;
  std::thread thread_;  // last
};

}  // namespace atc
