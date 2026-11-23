// Records one WAV per PTT transmission, with a manifest for the speech benchmark.
//   ./ptt_record --out DIR --speaker ID [--prompts FILE] [--device /dev/input/eventN]
//                [--start N] [--button right|left] [--grab] [--source NAME] [--server S]
//                [--tail-ms N]  audio kept after release, 0-1000 (default 200; 500 for the tail test)
// --prompts: phrase file, see src/bench/PhraseFile.h. --start N begins at phrase N.
// Without --device, Enter starts and Enter stops a transmission.
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/PulseCapture.h"
#include "audio/UtteranceChecks.h"
#include "audio/WavFile.h"
#include "bench/PhraseFile.h"
#include "core/TransmissionController.h"
#include "ptt/EvdevPttSource.h"
using namespace atc;

namespace {
std::atomic<bool> g_stop{false};

std::string json(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      o += '\\';
      o += static_cast<char>(c);
    } else if (c < 0x20) {
      char b[8];
      std::snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else {
      o += static_cast<char>(c);
    }
  }
  return o + "\"";
}
std::string jsonFields(const Fields& f) {
  std::string o = "{";
  for (const auto& [k, v] : f) o += (o.size() > 1 ? "," : "") + json(k) + ":" + json(v);
  return o + "}";
}
bool safeName(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
  return true;
}
struct Pending {
  std::uint64_t id;
  Ns from, to;
  TxEnd end;
};
}  // namespace

int main(int argc, char** argv) {
  CaptureConfig cc;
  EvdevConfig ec;
  std::string out, speaker, prompts;
  int startAt = 1;
  TxConfig tc;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--out") out = next();
    else if (k == "--speaker") speaker = next();
    else if (k == "--prompts") prompts = next();
    else if (k == "--start") startAt = std::atoi(next().c_str());
    else if (k == "--device") ec.path = next();
    else if (k == "--button") ec.code = next() == "left" ? 0x110 : 0x111;
    else if (k == "--grab") ec.grab = true;
    else if (k == "--source") cc.source = next();
    else if (k == "--server") cc.server = next();
    else if (k == "--tail-ms") {
      const std::string v = next();
      const bool digits = !v.empty() && v.size() <= 4 && v.find_first_not_of("0123456789") == std::string::npos;
      if (!digits || std::stoi(v) > 1000) {
        std::fprintf(stderr, "--tail-ms must be 0 to 1000\n");
        return 2;
      }
      tc.tail = ms(std::stoi(v));
    }
    else {
      std::fprintf(stderr, "unknown option %s\n", k.c_str());
      return 2;
    }
  }
  if (out.empty() || !safeName(speaker)) {
    std::fprintf(stderr, "need --out DIR and --speaker ID (letters, digits, - and _)\n");
    return 2;
  }
  std::vector<Phrase> lines;
  if (!prompts.empty()) {
    std::ifstream f(prompts);
    std::string error = "cannot read";
    if (!f || !readPhrases(f, lines, error)) {
      std::fprintf(stderr, "%s: %s\n", prompts.c_str(), error.c_str());
      return 2;
    }
  }
  if (startAt < 1 || (startAt > 1 && static_cast<std::size_t>(startAt) > lines.size())) {
    std::fprintf(stderr, "--start must be a phrase number\n");
    return 2;
  }
  std::error_code ec2;
  std::filesystem::create_directories(out, ec2);
  std::ofstream manifest(out + "/manifest.jsonl", std::ios::app);
  if (!manifest) {
    std::fprintf(stderr, "cannot write in %s\n", out.c_str());
    return 1;
  }
  std::signal(SIGINT, [](int) { g_stop = true; });
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  std::mutex m;
  std::vector<std::function<void()>> q;
  const auto post = [&](std::function<void()> f) {
    std::lock_guard l(m);
    q.push_back(std::move(f));
  };

  PrerollRing ring(cc.rate, static_cast<std::size_t>(cc.rate) * 60);
  LevelMeter meter;
  PulseCapture cap(cc, ring, meter, [&](const CaptureStatus& s) {
    post([s] {
      if (s.state == CaptureState::Lost) std::printf("MICROPHONE: %s (retrying)\n", s.message.c_str());
      if (s.state == CaptureState::Running)
        std::printf("MICROPHONE: %s%s\n", s.description.empty() ? s.source.c_str() : s.description.c_str(),
                    s.muted ? "  *** MUTED ***" : "");
    });
  });

  std::deque<Pending> pending;
  TransmissionController tx(tc, {
      [](std::uint64_t, Ns) { std::puts(">>> RT START"); },
      [&](std::uint64_t id, Ns a, Ns b, TxEnd e) {
        if (e == TxEnd::Stuck) std::puts("<<< TX LIMIT REACHED, transmission closed. Release PTT.");
        pending.push_back({id, a, b, e});
      },
      [](std::uint64_t) { std::puts("    too short, ignored"); }, nullptr, nullptr});
  PttInputManager pim({}, {
      [&](Ns t, const std::string&) { tx.onPress(t); },
      [&](Ns t, const std::string&, ReleaseReason r) {
        std::puts("<<< RT STOP");
        tx.onRelease(t, r);
      },
      nullptr,
      [](Ns, const std::string&) { std::puts("    WARNING: pulse-type device, cannot do hold-to-talk"); }});

  std::size_t idx = static_cast<std::size_t>(startAt) - 1;
  const auto prompt = [&] {
    if (lines.empty()) return;
    if (idx < lines.size())
      std::printf("\n[%zu/%zu] SAY: %s\n", idx + 1, lines.size(), lines[idx].text.c_str());
    else
      std::puts("\nAll phrases recorded. Ctrl+C to finish.");
  };
  int saved = 0;
  const auto save = [&](const Pending& p) {
    const auto cut = ring.extract(p.from, p.to);
    if (cut.samples.empty()) {
      std::puts("    NO AUDIO CAPTURED, nothing saved");
      prompt();
      return;
    }
    const int pre = static_cast<int>(tc.preRoll * cc.rate / 1'000'000'000) - static_cast<int>(cut.missingHead);
    const auto rep = checkUtterance(cut.samples, std::max(0, pre));
    const bool incomplete = cut.missingHead || cut.missingTail || cut.gap;
    const bool usable = !rep.empty && !rep.overload && !incomplete &&
                        !rep.spokeBeforeKeying && !rep.releasedEarly && p.end == TxEnd::Normal;
    std::string name;
    for (int n = 1;; ++n) {  // never overwrite an earlier recording
      char b[32];
      std::snprintf(b, sizeof b, "_%04d.wav", n);
      name = speaker + b;
      if (!std::filesystem::exists(out + "/" + name)) break;
    }
    if (!writeWav(out + "/" + name, cc.rate, cut.samples)) {
      std::printf("    CANNOT WRITE %s\n", name.c_str());
      return;
    }
    const bool hasPrompt = idx < lines.size();
    manifest << "{\"file\":" << json(name) << ",\"speaker\":" << json(speaker)
             << ",\"transcript\":" << (hasPrompt ? json(lines[idx].text) : "null")
             << ",\"kind\":" << (hasPrompt ? (lines[idx].negative ? "\"negative\"" : "\"scripted\"") : "null")
             << ",\"expect\":" << (hasPrompt ? jsonFields(lines[idx].expect) : "null")
             << ",\"tailSamples\":" << (p.end == TxEnd::Stuck ? 0 : tc.tail * cc.rate / 1'000'000'000)
             << ",\"usable\":" << (usable ? "true" : "false") << ",\"preRollSamples\":" << std::max(0, pre)
             << ",\"peakDbfs\":" << rep.peakDbfs << ",\"empty\":" << (rep.empty ? "true" : "false")
             << ",\"overload\":" << (rep.overload ? "true" : "false")
             << ",\"spokeBeforeKeying\":" << (rep.spokeBeforeKeying ? "true" : "false")
             << ",\"releasedEarly\":" << (rep.releasedEarly ? "true" : "false")
             << ",\"keyToSpeechMs\":" << (rep.keyToSpeechMs ? std::to_string(*rep.keyToSpeechMs) : "null")
             << ",\"missingHead\":" << cut.missingHead << ",\"missingTail\":" << cut.missingTail
             << ",\"gap\":" << (cut.gap ? "true" : "false") << ",\"end\":"
             << (p.end == TxEnd::Normal ? "\"normal\"" : p.end == TxEnd::Stuck ? "\"stuck\"" : "\"deviceLost\"") << "}\n";
    manifest.flush();
    if (!manifest) std::puts("    CANNOT WRITE MANIFEST");
    ++saved;
    std::printf("    saved %s  %.2f s  peak %.1f dBFS%s%s%s%s%s%s\n", name.c_str(),
                double(cut.samples.size()) / cc.rate, rep.peakDbfs, rep.empty ? "  NO SPEECH" : "",
                rep.overload ? "  TOO LOUD" : "", rep.spokeBeforeKeying ? "  SPOKE BEFORE PTT" : "",
                rep.releasedEarly ? "  RELEASED EARLY" : "", incomplete ? "  AUDIO INCOMPLETE" : "",
                usable ? "  OK" : "  -> say it again");
    if (p.end == TxEnd::Stuck) std::puts("    reason: held to the time limit");
    if (p.end == TxEnd::DeviceLost) std::puts("    reason: PTT device lost");
    // Advice only: not clipped, but a slightly louder phrase would be.
    if (!rep.overload && rep.peakDbfs > -3.0) std::puts("    CAUTION: close to overload, lower the microphone volume");
    if (hasPrompt && usable) ++idx;
    prompt();
  };

  if (!cap.start()) return 1;
  EvdevPttSource src(ec, {
      [&](Ns t, bool d) { post([&, t, d] { pim.rawEdge(t, "ptt", d); }); },
      [&](bool d) { post([&, d] { std::puts("PTT device open"); pim.setInitialState("ptt", d); }); },
      [&](Ns t) { post([&, t] { std::puts("PTT DEVICE LOST"); pim.deviceLost(t, "ptt"); }); },
      [&](const std::string& e) { post([e] { std::printf("PTT: cannot open %s\n", e.c_str()); }); }});
  const bool keyboard = ec.path.empty();
  if (keyboard)
    std::puts("No --device: press Enter to start, Enter to stop.");
  else
    src.start();
  prompt();

  bool keyDown = false;
  while (!g_stop) {
    if (keyboard) {
      pollfd p{STDIN_FILENO, POLLIN, 0};
      if (poll(&p, 1, 5) > 0) {
        char b[256];
        const ssize_t n = ::read(STDIN_FILENO, b, sizeof b);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i)
          if (b[i] == '\n') pim.rawEdge(monotonicNow(), "keyboard", keyDown = !keyDown);
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::vector<std::function<void()>> todo;
    {
      std::lock_guard l(m);
      todo.swap(q);
    }
    for (auto& f : todo) f();
    const Ns t = monotonicNow();
    pim.tick(t);
    tx.tick(t);
    // Wait until the ring really holds the tail, or give up after 1 s.
    while (!pending.empty() && (ring.newestTime() >= pending.front().to || t - pending.front().to > ms(1000))) {
      const Pending p = pending.front();
      pending.pop_front();
      save(p);
      tx.onProcessed(p.id, std::nullopt, t);
    }
  }
  src.stop();
  cap.stop();
  std::printf("\n%d recordings saved in %s\n", saved, out.c_str());
}
