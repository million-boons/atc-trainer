#include "app/SetupMock.h"

#include <memory>
#include <utility>

SetupBackend mockSetup() {
  auto c = std::make_shared<SetupContent>();
  auto pin = std::make_shared<std::string>("123456");
  const auto number = [&](const char* key, const char* page, const char* title, int v, int low, int high, int step, const char* unit, const char* applies) {
    c->numbers.push_back({key, page, title, unit, applies, v, v, low, high, step, false});
  };
  const auto choice = [&](const char* key, const char* page, const char* title, const char* v, std::vector<SetupOption> o, const char* applies, bool everybody = false) {
    c->choices.push_back({key, page, title, applies, v, v, std::move(o), everybody, false});
  };
  choice("theme", "general", "Theme", "dark", {{"dark", "Dark"}, {"night", "Night"}, {"hc", "High-contrast"}}, "", true);
  choice("motion", "general", "Motion", "normal", {{"normal", "Normal"}, {"reduce", "Reduced"}}, "", true);
  choice("clock", "general", "Clock", "both", {{"both", "UTC and local"}, {"utc", "UTC"}, {"local", "Local"}}, "", true);
  choice("mic", "audio", "Microphone", "h540", {{"h540", "H540 (simulated)"}, {"int", "Internal microphone (simulated)"}, {"gone", "USB headset", "not connected", true}}, "next transmission");
  choice("out", "audio", "Output", "h540", {{"h540", "H540 (simulated)"}, {"int", "Internal speaker (simulated)"}}, "next transmission");
  choice("ptt", "ptt", "Device", "mouse", {{"mouse", "Dell mouse (simulated), stand-in"}, {"pedal", "Foot pedal (simulated)"}}, "next start of the program");
  choice("sidetone", "ptt", "Side-tone", "on", {{"on", "On"}, {"off", "Off"}}, "next transmission");
  number("tap", "ptt", "Short tap below", 250, 100, 500, 10, "ms", "next transmission");
  number("preroll", "ptt", "Pre-roll", 300, 100, 500, 50, "ms", "next transmission");
  number("tail", "ptt", "Tail", 200, 0, 500, 50, "ms", "next transmission");
  number("longest", "ptt", "Longest transmission", 30, 10, 60, 5, "s", "next transmission");
  number("debounce", "ptt", "Debounce", 20, 5, 50, 5, "ms", "next start of the program");
  number("settle", "ptt", "Queue settle", 25, 0, 100, 5, "ms", "next transmission");
  number("cut", "ptt", "PILOT CUT notice", 2000, 500, 5000, 500, "ms", "next transmission");
  number("toneMs", "ptt", "Side-tone length", 20, 10, 100, 10, "ms", "next transmission");
  number("toneHz", "ptt", "Side-tone pitch", 1000, 300, 2000, 100, "Hz", "next transmission");
  number("toneDb", "ptt", "Side-tone level", -18, -40, -6, 2, "dBFS", "next transmission");
  choice("model", "speech", "Exercise model", "whisper-tiny-genprompt", {{"whisper-tiny-genprompt", "Whisper tiny + generated prompt", "325 MB"}, {"vosk-grammar", "Vosk grammar", "110 MB"}, {"whisper-base", "Whisper base", "not installed", true}}, "next Start");
  choice("bank", "voice", "Voice bank", "norman", {{"norman", "en_US-norman-medium", "22 clips, PLACEHOLDER VOICE"}, {"new", "Studio voice", "0 clips"}}, "next Start");
  choice("radio", "voice", "Radio effect", "light", {{"off", "Off"}, {"light", "Light"}, {"medium", "Medium"}, {"heavy", "Heavy"}}, "next Start");
  number("free", "recordings", "Stop saving below", 200, 100, 2000, 100, "MB", "");
  number("idle", "instructor", "Log out after", 10, 1, 60, 1, "min", "");
  c->facts = {{"voice", "Folder", "~/.local/share/atc-trainer/voices/mock"}, {"voice", "Clips", "22"}, {"speech", "Memory free", "1.4 GB (simulated)"},
              {"recordings", "Free space", "94 GB (simulated)"}, {"recordings", "Folder", "~/.local/state/atc-trainer (simulated)"},
              {"about", "Program", "mock"}, {"about", "Speech model", "mock hash 0000"}, {"about", "Voice bank", "mock hash 0000"},
              {"about", "Licences", "Inter, JetBrains Mono (SIL OFL); Qt (LGPL); whisper.cpp (MIT); Vosk (Apache-2.0)"}, {"about", "Data", "mock"}};
  c->guests = 3;

  SetupBackend b;
  b.load = [c](auto done) { done(*c, ""); };
  b.setNumber = [c](const std::string& key, int v, SetupBackend::Done done) {
    for (SetupNumber& n : c->numbers)
      if (n.key == key) {
        n.value = v;
        n.byUser = v != n.starting;
      }
    done("");
  };
  b.setChoice = [c](const std::string& key, const std::string& id, SetupBackend::Done done) {
    for (SetupChoice& x : c->choices)
      if (x.key == key) {
        x.value = id;
        x.byUser = id != x.starting;
      }
    done("");
  };
  b.restore = [c](const std::string& page, SetupBackend::Done done) {
    for (SetupNumber& n : c->numbers)
      if (n.page == page) {
        n.value = n.starting;
        n.byUser = false;
      }
    for (SetupChoice& x : c->choices)
      if (x.page == page) {
        x.value = x.starting;
        x.byUser = false;
      }
    done("");
  };
  b.startLearn = [](auto report, SetupBackend::Done done) {
    done("");
    report({{"pedal", "Foot pedal (simulated)", "1234:5678  HID", 2400, false, ""}, {"kbd", "Keyboard (simulated)", "0000:0001  keyboard", 300, true, "a keyboard needs an exact VID:PID"}});
  };
  b.stopLearn = [] {};
  b.changePin = [pin](const std::string& old, const std::string& next, SetupBackend::Done done) {
    if (old != *pin) return done("the old PIN is wrong");
    *pin = next;  // the mock's login keeps 123456
    done("");
  };
  b.testSentence = [](SetupBackend::Done done) { done("Mock backend: no audio playback."); };
  b.deleteGuests = [c](int expected, auto done) {
    if (expected != c->guests) return done(c->guests, "the number of guest results has changed: nothing was deleted");
    c->guests = 0;
    done(0, "");
  };
  return b;
}
