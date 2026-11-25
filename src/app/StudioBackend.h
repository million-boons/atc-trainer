#pragma once
#include <functional>
#include <string>
#include <vector>

// What the Phraseology Studio needs from outside (07, "Studio interface"; 09 section 8, approved by D-331).
// The only place that knows this interface besides StudioController is a backend. Texts: empty, or the reason of a
// refusal. Callbacks may come on any thread and after the call has returned; StudioController brings them to the GUI.

struct StudioSegment {
  bool dynamic = false;      // a {slot}
  bool commaAfter = false;   // a pause, never a clip
  std::string text{};        // words, or the slot name
  std::string position{};    // "initial", "medial", "final"; empty for ATC examples
  std::string clipId{};      // static segments of a reply
  std::string pool{};        // dynamic segments of a reply
};

struct StudioPreview {
  std::string error{};       // the text cannot be read
  std::vector<StudioSegment> segments{};
  std::vector<std::string> lint{};
  std::vector<std::string> missing{}, unused{};  // clips, if this text were saved
};

// A pilot reply (one text) or an ATC intent (its examples).
struct StudioEntry {
  std::string id{};
  bool reply = true;
  std::vector<std::string> texts{};
  bool validated = false;
  bool placeholderFile = true;
  std::string sopRef{}, validatedBy{}, validatedAt{};
  std::vector<std::string> usedBy{};   // "DEPARTURE-01  Taxi: CORRECT"
};

struct StudioScenario {
  std::string id{}, airfield{};
};

struct StudioContent {
  std::vector<StudioEntry> entries{};
  std::vector<StudioScenario> scenarios{};
  std::string voice{};         // the bank that is recorded
  std::string activeVoice{};   // the bank of the exercise: may be another one (ST-2)
  bool placeholderVoice = true;
  bool changedHere = false;    // the writable copy is in use (D-318)
  std::string notice{};        // e.g. the installed data is newer than the copy's origin
  std::string revision{};      // given back when saving: the file must not have changed meanwhile
};

struct StudioTake {
  enum class State { Pending, Primary, Alternate, Rejected, Withdrawn };
  std::string id{}, clipId{}, speaker{};
  int number = 0;
  bool synthetic = false;      // not a person's voice
  State state = State::Pending;
  double seconds = 0;          // of the kept part
  double levelDb = 0;
  double trimStart = 0, trimEnd = 1;  // fractions of the raw take
  std::vector<std::string> warnings{};
};

struct StudioClip {
  std::string id{};
  std::string words{}, bankWords{};  // of the data; of the manifest (empty: no clip)
  std::string sayBefore{}, sayTarget{}, sayAfter{};
  std::string pool{}, value{}, position{};
  std::vector<std::string> replies{};  // that use it
  bool needed = true;
  std::vector<StudioTake> takes{};
};

struct StudioEvents {
  std::function<void(double dbfs)> level;         // at most 25 per second
  std::function<void(double dbfs, const std::string& warning)> roomTone;  // measured while a session starts
  std::function<void(bool on)> recording;         // the core's real start and stop, also by pedal: what RECORDING shows
  std::function<void(int ms)> elapsed;
  std::function<void(const std::string& why)> recordingStopped;  // the reason; empty: as asked
  std::function<void(StudioTake)> takeReady;
  std::function<void(const std::string& why)> sessionEnded;
  std::function<void()> clipsChanged;
};

struct StudioBackend {
  using Done = std::function<void(const std::string& why)>;
  std::function<void(StudioEvents)> setEvents;
  std::function<void(std::function<void(StudioContent, const std::string& why)>)> load;
  std::function<void(const StudioEntry& entry, int textIndex, std::function<void(StudioPreview)>)> preview;  // writes nothing
  std::function<void(StudioEntry, const std::string& revision, Done)> save;
  std::function<void(StudioEntry, const std::string& revision, Done)> addReply;
  std::function<void(const std::string& scenario, std::function<void(std::vector<StudioClip>, const std::string& why)>)> clips;
  std::function<void(const std::string& take, std::function<void(std::vector<float>, const std::string& why)>)> peaks;  // of the raw take: min, max, min, max ... in -1..1, at most 1000 pairs
  std::function<void(const std::string& speaker, Done)> startSession;  // answers after the room tone (about 2 s)
  std::function<void()> endSession;               // stops a recording and keeps it as pending; during the room tone: startSession is refused; twice: harmless
  // The pedal starts and stops a recording of this clip; it never transmits here. Called again when another clip is
  // chosen while it is on; a take that runs keeps its clip. Without a clip the pedal records nothing.
  std::function<void(bool on, const std::string& clipId)> setPedal;
  std::function<void(const std::string& clipId, Done)> record;
  std::function<void()> stopRecord;
  std::function<void(const std::string& take, double start, double end, Done)> setTrim;
  std::function<void(const std::string& take, const std::string& mode, Done)> play;  // raw, trimmed, radio, sentence (as sayBefore and sayAfter show it; the text names missing clips)
  std::function<void()> stopPlay;
  std::function<void(const std::string& take, bool primary, Done)> accept;
  std::function<void(const std::string& take, Done)> reject;
  std::function<void(const std::string& take, Done)> deleteTake;
  std::function<void(const std::string& file, Done)> exportChanges;
};
