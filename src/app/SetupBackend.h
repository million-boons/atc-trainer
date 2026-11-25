#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

// What the full Settings screen needs from outside (04 section 5f, 07). PROPOSED: b2 has not accepted it yet.
// Texts: empty, or the reason of a refusal. Callbacks may come on any thread, after the call has returned.
// A value stored by the user wins over the starting value; restore() removes it. The backend checks role, range and
// "no exercise" again.

struct SetupNumber {
  std::string key{}, page{}, title{}, unit{}, applies{};
  int value = 0, starting = 0, low = 0, high = 0, step = 1;
  bool byUser = false;
};

struct SetupOption {
  std::string id{}, text{}, note{};
  bool off = false;
};

struct SetupChoice {
  std::string key{}, page{}, title{}, applies{}, value{}, starting{};
  std::vector<SetupOption> options{};
  bool everybody = false;   // also a trainee or a guest (E-1)
  bool byUser = false;
};

struct SetupFact {
  std::string page{}, name{}, value{};
};

struct SetupContent {
  std::vector<SetupNumber> numbers{};
  std::vector<SetupChoice> choices{};
  std::vector<SetupFact> facts{};
  int guests = 0;           // guest results: a count, never a list (D-131)
};

struct SetupPress {         // while a pedal is learned: one device that reports presses
  std::string id{}, name{}, info{};  // VID:PID and type
  int heldMs = 0;
  bool down = false;
  std::string refused{};    // why it may not be used, e.g. a keyboard that is not an exact VID:PID (D-310)
};

struct SetupBackend {
  using Done = std::function<void(const std::string& why)>;
  std::function<void(std::function<void(SetupContent, const std::string& why)>)> load;
  std::function<void(const std::string& key, int value, Done)> setNumber;
  std::function<void(const std::string& key, const std::string& id, Done)> setChoice;
  std::function<void(const std::string& page, Done)> restore;          // the starting values of a page
  std::function<void(std::function<void(std::vector<SetupPress>)>, Done)> startLearn;  // never transmits; reports until stopLearn()
  std::function<void()> stopLearn;
  std::function<void(const std::string& oldPin, const std::string& newPin, Done)> changePin;  // slow; never logged
  std::function<void(Done)> testSentence;                              // through the radio effect that is chosen
  // Deletes all guest results, each logged first. Refused if there are not exactly `expected`; then the count is the new one.
  std::function<void(int expected, std::function<void(int count, const std::string& why)>)> deleteGuests;
};
