// radio_ab: one pilot reply in every radio intensity, for listening tests (03 section 4).
//   radio_ab --in speech.wav --out DIR
//   radio_ab [--bank DIR] --clips "ID,ID,,,ID" --out DIR     (an empty entry is a comma pause)
//   add --play to hear them in turn on the default output, --seed N for another noise
// Writes off.wav, light.wav, medium.wav, heavy.wav and prints peak and RMS. The choice is made by people
// on the real headset; the tool recommends nothing.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

#include "audio/RadioAudioProcessor.h"
#include "audio/WavFile.h"
#include "core/PathResolver.h"
#include "voice/PhraseAssembler.h"
#ifdef ATC_HAVE_PULSE
#include <atomic>
#include <thread>

#include "audio/PulsePlayer.h"
#endif

using namespace atc;

namespace {
int usage() {
  std::cerr << "radio_ab --in speech.wav --out DIR [--seed N] [--play]\n"
               "radio_ab [--bank DIR] --clips \"ID,ID,,ID\" --out DIR [--seed N] [--play]\n";
  return 2;
}
double db(double x) { return x > 0 ? 20 * std::log10(x / 32768) : -120; }
}  // namespace

int main(int argc, char** argv) {
  std::string in, bankDir, clips, out;
  std::uint64_t seed = 1;
  bool play = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto next = [&](std::string& v) { return i + 1 < argc ? (v = argv[++i], true) : false; };
    std::string n;
    if (a == "--play") play = true;
    else if (a == "--in" ? !next(in) : a == "--bank" ? !next(bankDir) : a == "--clips" ? !next(clips) : a == "--out" ? !next(out) : a == "--seed" ? !next(n) : true) return usage();
    if (!n.empty()) seed = std::strtoull(n.c_str(), nullptr, 10);
  }
  if (out.empty() || in.empty() == clips.empty()) return usage();

  WavData speech;
  if (!in.empty()) {
    auto w = readWav(in);
    if (!w || w->samples.empty()) {
      std::cerr << in << ": not a 16-bit mono WAV file\n";
      return 1;
    }
    speech = std::move(*w);
  } else {
    std::string error;
    if (bankDir.empty()) {
      Paths p;
      if (!resolvePaths(systemPathInputs(), p, error)) {
        std::cerr << error << "\n";
        return 1;
      }
      bankDir = p.voices;
    }
    VoiceBank bank;
    std::vector<LoadError> errors;
    if (!bank.load(bankDir, errors)) {
      for (const auto& e : errors) std::cerr << toString(e) << "\n";
      return 1;
    }
    std::vector<std::string> ids;
    for (std::size_t from = 0; from <= clips.size();) {
      const auto to = std::min(clips.find(',', from), clips.size());
      const std::string id = clips.substr(from, to - from);
      ids.push_back(id.empty() ? "," : id);
      from = to + 1;
    }
    std::mt19937_64 rng(seed);
    speech.rate = VoiceBank::kRate;
    if (!PhraseAssembler(bank).assemble(ids, rng, speech.samples, error)) {
      std::cerr << error << "\n";
      return 1;
    }
    if (bank.placeholder()) std::cout << "voice " << bank.voice() << ": PLACEHOLDER\n";
  }

  std::error_code ec;
  std::filesystem::create_directories(out, ec);
  for (const auto i : {RadioIntensity::Off, RadioIntensity::Light, RadioIntensity::Medium, RadioIntensity::Heavy}) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto pcm = applyRadioEffect(speech.samples, speech.rate, i, seed);
    const double took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const std::string file = out + "/" + std::string(toString(i)) + ".wav";
    if (!writeWav(file, speech.rate, pcm)) {
      std::cerr << file << ": cannot be written\n";
      return 1;
    }
    double peak = 0, power = 0;
    for (const double x : pcm) {
      peak = std::max(peak, std::abs(x));
      power += x * x;
    }
    std::printf("%-7s %6.2f s  peak %6.1f dBFS  rms %6.1f dBFS  processed in %.1f ms on this machine\n", std::string(toString(i)).c_str(),
                double(pcm.size()) / speech.rate, db(peak), db(std::sqrt(power / double(pcm.size()))), took);
#ifdef ATC_HAVE_PULSE
    if (play) {
      std::atomic<bool> done{false};
      PulsePlayer p({[&](PlayId, bool) { done = true; }, [](const OutputStatus&) {}});
      bool ok = p.start();
      for (int k = 0; ok && k < 50 && !(ok = p.play(1, pcm, speech.rate)); ++k, ok = true) std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(int(1000.0 * double(pcm.size()) / speech.rate) + 2000);
      while (!done && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
      if (!done) std::cerr << "not played: no audio output\n";
    }
#else
    if (play) std::cerr << "built without PulseAudio: not played\n";
#endif
  }
  return 0;
}
