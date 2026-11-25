// Makes a voice bank offline. Two steps, with the dev voice (or a speaker) in between:
//   voice_tool list  [--data DIR] [--out FILE]               one JSON line per clip: text, output_file, words
//   voice_tool build --raw DIR --out DIR --voice NAME [--data DIR] [--real] [--noise DBFS] [--target DBFS]
//                   [--pad-before MS] [--pad-after MS]
// build trims and levels DIR/<clip>.wav, writes 16 kHz WAV and manifest.json, loads the result and
// checks coverage. Nothing is written unless every clip is good; an existing bank is rebuilt in place,
// file by file, so its folder never goes missing (a missing voices folder stops every program). Raw files are only read. Audio never goes into a git working copy (D-31, D-418).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "audio/WavFile.h"
#include "core/PathResolver.h"
#include "voice/ClipList.h"
#include "voice/ClipTrim.h"
#include "voice/VoiceBank.h"
using namespace atc;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: voice_tool list [--data DIR] [--out FILE]\n"
               "       voice_tool build --raw DIR --out DIR --voice NAME [--data DIR] [--real] [--noise DBFS] [--target DBFS]\n"
               "                  [--pad-before MS] [--pad-after MS]\n");
  return 2;
}

bool inGit(const fs::path& dir) {
  std::error_code ec;
  for (fs::path p = fs::weakly_canonical(fs::absolute(dir), ec); !p.empty(); p = p.parent_path()) {
    if (fs::exists(p / ".git", ec)) return true;
    if (p == p.root_path()) break;
  }
  return false;
}

// Same rule as the voice bank: temporary name, then rename.
bool writeText(const fs::path& file, const std::string& text) {
  const fs::path part = file.string() + ".part";
  {
    std::ofstream f(part, std::ios::binary);
    f << text;
    if (!f.flush()) return false;
  }
  std::error_code ec;
  fs::rename(part, file, ec);
  return !ec;
}

double tenth(double x) { return std::round(x * 10) / 10; }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string command = argv[1];
  if (command != "list" && command != "build") return usage();
  std::string data, raw, out, voice;
  bool real = false;
  TrimConfig trim;
  for (int i = 2; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--real" && command == "build") {
      real = true;
      continue;
    }
    if (i + 1 >= argc) return usage();
    const std::string v = argv[++i];
    if (k == "--data") data = v;
    else if (k == "--out") out = v;
    else if (command == "build" && k == "--raw") raw = v;
    else if (command == "build" && k == "--voice") voice = v;
    else if (command == "build" && k == "--noise") trim.noiseFloorDb = std::strtod(v.c_str(), nullptr);
    else if (command == "build" && k == "--target") trim.targetDb = std::strtod(v.c_str(), nullptr);
    else if (command == "build" && k == "--pad-before") trim.padBeforeMs = std::atoi(v.c_str());
    else if (command == "build" && k == "--pad-after") trim.padAfterMs = std::atoi(v.c_str());
    else return usage();
  }
  if (command == "build" && (raw.empty() || out.empty() || voice.empty())) return usage();
  if (real) {  // a person's take needs the speaker and the consent in the manifest (D-132, D-319)
    std::fprintf(stderr, "--real is not available: a person's clips are recorded in the Phraseology Studio, with speaker and consent\n");
    return 1;
  }
  std::string error;
  if (data.empty()) {
    Paths p;
    if (!resolvePaths(systemPathInputs(), p, error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 2;
    }
    data = p.data;
  }

  // The clips of every scenario together.
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::directory_iterator it(fs::path(data) / "scenarios", ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".json") files.push_back(it->path());
  std::sort(files.begin(), files.end());
  SpokenForms forms;
  if (!loadSpokenForms((fs::path(data) / "phraseology").string(), forms, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  std::vector<Exercise> exercises;
  std::map<std::string, ClipLine> clips;
  bool ok = !files.empty();
  if (!ok) std::fprintf(stderr, "no scenario files in %s/scenarios\n", data.c_str());
  for (const auto& f : files) {
    Exercise e;
    std::vector<LoadError> errors;
    if (!loadExercise(scenarioSources(data, f.string()), e, errors)) {
      ok = false;
      for (const auto& x : errors) std::fprintf(stderr, "%s\n", toString(x).c_str());
      continue;
    }
    std::vector<ClipLine> lines;
    std::vector<std::string> bad;
    if (!clipList(e, forms, lines, bad)) ok = false;
    for (const auto& b : bad) std::fprintf(stderr, "%s: %s\n", e.scenario.id.c_str(), b.c_str());
    for (auto& l : lines) {
      const auto [it, added] = clips.emplace(l.id, l);
      if (!added && it->second.words != l.words) {
        ok = false;
        std::fprintf(stderr, "%s: said as \"%s\" and as \"%s\"\n", l.id.c_str(), it->second.words.c_str(), l.words.c_str());
      }
    }
    exercises.push_back(std::move(e));
  }
  if (!ok) return 1;

  if (command == "list") {
    std::string text;
    for (const auto& [id, c] : clips) text += json{{"text", c.prompt()}, {"output_file", c.file()}, {"words", c.words}}.dump() + "\n";
    if (out.empty()) {
      std::fputs(text.c_str(), stdout);
    } else if (!writeText(out, text)) {
      std::fprintf(stderr, "cannot write %s\n", out.c_str());
      return 1;
    }
    std::fprintf(stderr, "%zu clips\n", clips.size());
    return 0;
  }

  if (inGit(out)) {
    std::fprintf(stderr, "%s is inside a git working copy; audio must stay outside\n", out.c_str());
    return 1;
  }
  fs::create_directories(out, ec);
  if (fs::weakly_canonical(out, ec) == fs::weakly_canonical(raw, ec)) {
    std::fprintf(stderr, "--raw and --out must be different folders\n");
    return 1;
  }
  std::map<std::string, TrimResult> made;
  json list = json::array();
  int warnings = 0;
  for (const auto& [id, c] : clips) {
    const fs::path source = fs::path(raw) / c.file();
    const auto wav = readWav(source.string());
    TrimResult r;
    error = wav ? "" : "missing or not 16-bit mono PCM";
    if (!wav || !trimClip(resample(wav->samples, wav->rate, VoiceBank::kRate), VoiceBank::kRate, trim, r, error)) {
      ok = false;
      std::fprintf(stderr, "%s: %s\n", source.string().c_str(), error.c_str());
      continue;
    }
    for (const auto& w : r.warnings) {
      ++warnings;
      std::printf("warning: %s: %s\n", id.c_str(), w.c_str());
    }
    json take = {{"file", c.file()}, {"take", "primary"}, {"synthetic", true}, {"durationMs", r.samples.size() * 1000 / VoiceBank::kRate},
                 {"trimStart", r.start}, {"trimEnd", r.end}, {"speechDb", tenth(r.speechDb)}, {"gainDb", tenth(r.gainDb)}};
    list.push_back({{"id", id}, {"words", c.words}, {"takes", json::array({take})}});
    made.emplace(c.file(), std::move(r));
  }
  if (!ok) {
    std::fprintf(stderr, "nothing written\n");
    return 1;
  }
  for (const auto& [file, r] : made)
    if (!writeWav((fs::path(out) / file).string(), VoiceBank::kRate, r.samples)) {
      std::fprintf(stderr, "cannot write %s\n", file.c_str());
      return 1;
    }
  const json manifest = {{"schemaVersion", 1}, {"voice", voice}, {"sampleRate", VoiceBank::kRate}, {"placeholder", !real}, {"clips", list}};
  if (!writeText(fs::path(out) / "manifest.json", manifest.dump(1) + "\n")) {
    std::fprintf(stderr, "cannot write manifest.json\n");
    return 1;
  }
  VoiceBank bank;
  std::vector<LoadError> errors;
  ok = bank.load(out, errors);
  for (const auto& x : errors) std::fprintf(stderr, "%s\n", toString(x).c_str());
  for (const auto& e : exercises) {
    if (!ok) break;
    ok = checkCoverage(e, bank, errors);
    for (const auto& x : errors) std::fprintf(stderr, "%s\n", toString(x).c_str());
  }
  if (!ok) return 1;
  std::printf("%zu clips, %zu kB in memory, %d warnings, coverage OK\n", bank.clipCount(), bank.bytes() / 1024, warnings);
  return 0;
}
