#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "app/StudioBackend.h"
#include "runtime/RadioCore.h"
#include "scenario/Scenario.h"
#include "storage/ConsentView.h"
#include "storage/DataCopy.h"
#include "voice/ClipList.h"
#include "voice/TakeStore.h"

namespace atc {

// The Phraseology Studio without a screen (09, D-331): the rules, the files and the recording session. Qt-free.
// backend() is called from the GUI thread; files are read and written on one worker thread; the on...() functions
// come from RadioCore's thread. Whoever calls them must have stopped before this object goes.
class StudioService {
 public:
  struct Config {
    std::string data;                    // installed: scenarios, airfields
    std::string phraseology, writable;   // installed phraseology; folder for the copy (D-318)
    std::string activeBank;              // of the exercise: only its name is shown
    std::string bank, pending, voice;    // of the Studio (ST-2)
    double loudRoomDb = -50;             // a warning above this
    long minFreeMb = 200;                // nothing is recorded below this
  };
  struct Radio {
    std::function<void(bool on, const std::string& why)> session;
    std::function<void(bool on)> pedal;
    std::function<void()> record, stop, stopPlay;
    std::function<void(std::vector<std::int16_t>, int rate)> play;
  };
  struct Rules {
    std::function<bool()> instructor;      // GUI thread
    std::function<bool()> exerciseActive;  // GUI thread
    std::function<std::string()> today;    // YYYY-MM-DD
    std::function<std::int64_t()> nowMs;
    std::function<void()> changed;         // data or bank: the exercise must be checked again (worker thread)
  };

  StudioService(Config c, std::shared_ptr<ConsentView> consent, std::shared_ptr<DataCopy> copy, Rules r);
  ~StudioService();
  void connect(Radio r);      // later than the constructor: RadioCore needs on...() first
  StudioBackend backend();    // valid as long as this object

  void onStudio(const StudioNote& n);
  void onTake(StudioAudio&& a);
  void onSnapshot(const RadioSnapshot& s);
  void onPlay(LocalEnd e);
  void consentChanged();  // an answer may have changed, or the instructor has logged out

  void wait();  // tests: until the worker has nothing to do

 private:
  using Done = StudioBackend::Done;
  enum class Session { None, Starting, Open };
  struct Loaded {
    std::vector<std::pair<std::string, Exercise>> exercises;  // file, content
    std::vector<std::string> errors;
  };
  struct Need {
    std::string words, pool, value, position;
    std::vector<std::string> replies;
  };
  using Needs = std::map<std::string, Need>;

  void post(std::function<void()> f);
  void run();
  StudioEvents events() const;
  std::string mayWrite() const;  // GUI thread: empty, or why not
  // Worker.
  Loaded loadAll(const std::string& phraseology) const;
  bool needs(const Exercise& e, const SpokenForms& f, Needs& out, std::string& error) const;
  bool allNeeds(const Loaded& l, const std::string& phraseology, Needs& out, std::string& error) const;
  std::string say(const Exercise& e, const SpokenForms& f, const std::string& clipId, const Need& n, StudioClip& out, std::vector<std::string>* sequence) const;
  std::string trial(const std::string& dir, const std::set<std::string>& loadedBefore) const;
  StudioTake shown(const Take& t) const;
  bool withdrawn(const Take& t) const;
  void doLoad(std::function<void(StudioContent, const std::string&)> done);
  void doPreview(StudioEntry e, int index, std::function<void(StudioPreview)> done);
  std::string doSave(const StudioEntry& e, const std::string& revision, bool add, const std::string& today);
  void doClips(const std::string& scenario, std::function<void(std::vector<StudioClip>, const std::string&)> done);
  std::string doExport(const std::string& file);
  std::string sound(const std::string& take, const std::string& mode, std::vector<std::int16_t>& out, std::string& note);
  std::string store();  // reads the takes if that has not been done; empty, or why not

  const Config cfg_;
  const std::shared_ptr<ConsentView> consent_;
  const std::shared_ptr<DataCopy> copy_;
  const Rules rules_;

  mutable std::mutex m_;
  Radio radio_;
  StudioEvents ev_;
  Session session_ = Session::None;
  Done starting_;                 // the answer to startSession
  std::string speaker_, clip_, pedalClip_, takeClip_, takeSpeaker_;
  bool recordAsked_ = false, recording_ = false, pedal_ = false;
  double noise_ = -90;
  Ns recordedAt_ = 0;
  Done playing_;
  std::string playNote_;

  // Worker.
  TakeStore takes_;
  bool read_ = false;

  std::mutex qm_;
  std::condition_variable cv_, idle_;
  std::deque<std::function<void()>> q_;
  bool stop_ = false, busy_ = false;
  std::thread thread_;
};

}  // namespace atc
