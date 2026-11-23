// Push to talk, see the text. Choose a speech model from the list, load it, then talk.
//   ./ptt_text [--device /dev/input/eventN] [--button right|left] [--grab] [--model ID]
//              [--source NAME] [--server S]
// Type a number + Enter to load a model, u to unload, l to list, q to quit.
// Without --device, Enter alone starts and stops a transmission.
// Saves nothing. The text is unvalidated: no model has passed the accuracy target.
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "audio/PulseCapture.h"
#include "core/TransmissionController.h"
#include "ptt/EvdevPttSource.h"
#include "stt/SttFactory.h"
#include "stt/SttService.h"
using namespace atc;

namespace {
std::atomic<bool> g_stop{false};
}

int main(int argc, char** argv) {
  CaptureConfig cc;
  EvdevConfig ec;
  std::string startModel, config = ATC_ROOT "/config/stt_models.json";
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--device") ec.path = next();
    else if (k == "--button") ec.code = next() == "left" ? 0x110 : 0x111;
    else if (k == "--grab") ec.grab = true;
    else if (k == "--model") startModel = next();
    else if (k == "--source") cc.source = next();
    else if (k == "--server") cc.server = next();
    else {
      std::fprintf(stderr, "unknown option %s\n", k.c_str());
      return 2;
    }
  }
  std::vector<SttModel> models;
  std::string error;
  if (!loadSttCatalog(config, ATC_ROOT, models, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
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

  TransmissionController* txp = nullptr;
  SttService svc(
      models, [](const SttModel& x, std::string& e) { return makeRecognizer(x, ATC_GGML_DIR, e); }, describeModel,
      {[&](const SttStatus& s) {
         post([s] {
           if (s.state == SttState::Loading) std::printf("MODEL: loading %s ...\n", s.modelId.c_str());
           else if (s.state == SttState::Ready && s.message.empty()) return;
           else if (!s.message.empty()) std::printf("MODEL: %s (%s)\n", s.message.c_str(), toString(s.state));
           else if (s.state == SttState::NotLoaded) std::puts("MODEL: unloaded");
         });
       },
       [&](std::uint64_t id, const std::string& model, const std::string&, const SttResult& r, Ns delay) {
         post([&, id, model, r, delay] {
           std::printf("    TEXT [%s, %.2f s after release]: %s\n", model.c_str(), double(delay) / 1e9,
                       r.text.empty() ? "(nothing recognised)" : r.text.c_str());
           if (txp) txp->onProcessed(id, std::nullopt, monotonicNow());
         });
       }},
      availableMemoryMb);

  const auto list = [&] {
    const auto st = svc.status();
    std::puts("\nSpeech models (number + Enter loads, u unloads, k locks/unlocks, q quits):");
    for (std::size_t i = 0; i < models.size(); ++i)
      std::printf(" %c%zu  %-26s %s%s\n", st.state == SttState::Ready && st.modelId == models[i].id ? '*' : ' ', i + 1,
                  models[i].name.c_str(), models[i].installed ? "" : "[NOT INSTALLED] ", models[i].note.c_str());
    std::puts("");
  };

  TxConfig tc;
  bool streaming = false;
  std::int64_t cursor = 0;
  struct Closing {
    std::int64_t end;
    Ns release, deadline;
  };
  std::optional<Closing> closing;
  const auto feed = [&](std::int64_t to) {
    const auto c = ring.extractIndex(cursor, to);
    if (c.missingHead) std::puts("    WARNING: audio lost");
    if (!c.samples.empty()) svc.accept(c.samples);
    cursor += std::int64_t(c.missingHead + c.samples.size());
  };
  const auto close = [&] {
    if (!closing) return;
    feed(closing->end);
    if (cursor < closing->end) std::puts("    WARNING: end of audio missing");
    svc.finish(closing->release);
    closing.reset();
    streaming = false;
  };
  TransmissionController tx(tc, {
      [&](std::uint64_t id, Ns from) {
        std::puts(">>> RT START");
        close();  // an earlier transmission still waiting for its last audio
        streaming = svc.begin(id);
        cursor = ring.indexAt(from);
        if (!streaming) std::puts("    NO MODEL READY: this transmission will not be recognised");
      },
      [&](std::uint64_t id, Ns, Ns to, TxEnd e) {
        if (e == TxEnd::Stuck) std::puts("<<< TX LIMIT REACHED, transmission closed. Release PTT.");
        if (!streaming) {
          txp->onProcessed(id, std::nullopt, monotonicNow());
          return;
        }
        closing = Closing{ring.indexAt(to), e == TxEnd::Stuck ? to : to - tc.tail, monotonicNow() + ms(1000)};
      },
      [&](std::uint64_t) {
        std::puts("    too short, ignored");
        if (streaming) svc.cancel();
        streaming = false;
      },
      nullptr, nullptr});
  txp = &tx;
  PttInputManager pim({}, {
      [&](Ns t, const std::string&) { tx.onPress(t); },
      [&](Ns t, const std::string&, ReleaseReason r) {
        std::puts("<<< RT STOP");
        tx.onRelease(t, r);
      },
      nullptr,
      [](Ns, const std::string&) { std::puts("    WARNING: pulse-type device, cannot do hold-to-talk"); }});

  if (!cap.start()) return 1;
  EvdevPttSource src(ec, {
      [&](Ns t, bool d) { post([&, t, d] { pim.rawEdge(t, "ptt", d); }); },
      [&](bool d) { post([&, d] { std::puts("PTT device open"); pim.setInitialState("ptt", d); }); },
      [&](Ns t) { post([&, t] { std::puts("PTT DEVICE LOST"); pim.deviceLost(t, "ptt"); }); },
      [&](const std::string& e) { post([e] { std::printf("PTT: cannot open %s\n", e.c_str()); }); }});
  const bool keyboard = ec.path.empty();
  if (keyboard) std::puts("No --device: Enter alone starts and stops a transmission.");
  else src.start();
  list();
  if (!startModel.empty()) svc.load(startModel);

  bool keyDown = false, stdinOpen = true;
  std::string line;
  std::string announced;
  while (!g_stop) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    if (stdinOpen && poll(&p, 1, 5) > 0) {
      char b[256];
      const ssize_t n = ::read(STDIN_FILENO, b, sizeof b);
      if (n <= 0) {
        stdinOpen = false;
        if (keyboard) break;
      }
      for (ssize_t i = 0; i < n; ++i) {
        if (b[i] != '\n') {
          line += b[i];
          continue;
        }
        const std::string cmd = std::exchange(line, {});
        if (cmd.empty() && keyboard) pim.rawEdge(monotonicNow(), "keyboard", keyDown = !keyDown);
        else if (cmd == "q") g_stop = true;
        else if (cmd == "u") svc.unload();
        else if (cmd == "l") list();
        else if (cmd == "k") {  // as an exercise will do
          if (svc.status().locked) svc.unlock(), std::puts("MODEL: unlocked");
          else if (svc.lock()) std::printf("MODEL: locked  %s\n", svc.status().modelConfig.c_str());
        }
        else if (!cmd.empty() && cmd.find_first_not_of("0123456789") == std::string::npos && cmd.size() < 4 &&
                 std::stoul(cmd) >= 1 && std::stoul(cmd) <= models.size())
          svc.load(models[std::stoul(cmd) - 1].id);
        else if (!cmd.empty()) std::puts("number, u, k, l or q");
      }
    } else if (!stdinOpen) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::vector<std::function<void()>> todo;
    {
      std::lock_guard l(m);
      todo.swap(q);
    }
    for (auto& f : todo) f();
    const auto st = svc.status();
    const std::string now = st.state == SttState::Ready ? st.modelId : "";
    if (st.state != SttState::Busy && now != announced) {
      announced = now;
      if (!now.empty()) std::printf("MODEL READY: %s (loaded in %.2f s). Hold PTT and talk.\n", now.c_str(), st.loadMs / 1000);
    }
    const Ns t = monotonicNow();
    pim.tick(t);
    tx.tick(t);
    if (closing && (ring.newestIndex() >= closing->end || t > closing->deadline)) close();
    else if (streaming) feed(closing ? std::min(closing->end, ring.newestIndex()) : ring.newestIndex());
  }
  src.stop();
  cap.stop();
  std::puts("");
  return 0;
}
