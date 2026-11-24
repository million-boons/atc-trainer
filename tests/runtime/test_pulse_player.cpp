#include <gtest/gtest.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <thread>

#include "audio/PulseCapture.h"
#include "audio/PulsePlayer.h"
using namespace atc;
namespace fs = std::filesystem;

namespace {
void nap(int millis) { std::this_thread::sleep_for(std::chrono::milliseconds(millis)); }

template <class F> bool within(int millis, F f) {
  for (int t = 0; t < millis; t += 10) {
    if (f()) return true;
    nap(10);
  }
  return f();
}

std::vector<std::int16_t> sine(int millis) {
  std::vector<std::int16_t> v(std::size_t(16 * millis));
  for (std::size_t i = 0; i < v.size(); ++i) v[i] = std::int16_t(8000 * std::sin(double(i) * 0.4));
  return v;
}

// Its own server, socket and folders: never the user's sound.
class PlayerTest : public testing::Test {
 protected:
  void SetUp() override {
    if (access("/usr/bin/pulseaudio", X_OK) != 0) GTEST_SKIP() << "pulseaudio is not installed";
    if (geteuid() == 0) GTEST_SKIP() << "pulseaudio does not run as root";
    dir = fs::path(testing::TempDir()) / ("atc_pa_" + std::to_string(getpid()));
    fs::create_directories(dir);
    server = "unix:" + (dir / "sock").string();
    startServer();
    player = std::make_unique<PulsePlayer>(
        PlayerCallbacks{[this](PlayId id, bool ok) { last = id; completed = ok; ++finished; },
                        [this](const OutputStatus& s) { std::lock_guard l(m); out = s; }},
        server, "out");
    ASSERT_TRUE(player->start());
    ASSERT_TRUE(within(5000, [this] { return status().available; })) << "no private audio server";
  }
  void TearDown() override {
    player.reset();
    stopServer();
    if (!dir.empty()) fs::remove_all(dir);
  }
  void startServer() {
    const std::string mod = "module-native-protocol-unix auth-anonymous=1 socket=" + (dir / "sock").string();
    pid = fork();
    if (pid == 0) {
      setenv("PULSE_RUNTIME_PATH", dir.c_str(), 1);
      setenv("PULSE_STATE_PATH", dir.c_str(), 1);
      setenv("HOME", dir.c_str(), 1);
      setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent", 1);  // the name belongs to the user's server
      execl("/usr/bin/pulseaudio", "pulseaudio", "-n", "--daemonize=no", "--use-pid-file=no", "--exit-idle-time=-1", "--disable-shm=yes",
            "--log-level=error", "-L", "module-null-sink sink_name=out", "-L", mod.c_str(), nullptr);
      _exit(127);
    }
  }
  void stopServer() {
    if (pid <= 0) return;
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);
    pid = 0;
    if (!dir.empty()) fs::remove(dir / "sock");
  }
  OutputStatus status() { std::lock_guard l(m); return out; }

  fs::path dir;
  std::string server;
  pid_t pid = 0;
  std::mutex m;
  OutputStatus out;
  std::atomic<PlayId> last{0};
  std::atomic<bool> completed{false};
  std::atomic<int> finished{0};
  std::unique_ptr<PulsePlayer> player;
};
}  // namespace

TEST_F(PlayerTest, PlaysToTheEndAndReports) {
  const auto t0 = monotonicNow();
  ASSERT_TRUE(player->play(5, sine(400), 16000));
  ASSERT_TRUE(within(3000, [this] { return finished == 1; }));
  EXPECT_EQ(last, 5u);
  EXPECT_TRUE(completed);
  EXPECT_GE(monotonicNow() - t0, ms(350));
  ASSERT_TRUE(player->play(6, sine(20), 16000));  // shorter than the buffer
  EXPECT_TRUE(within(3000, [this] { return finished == 2; }));
}

TEST_F(PlayerTest, StopIsConfirmedPromptAndNotReported) {
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(player->play(1, sine(3000), 16000));
    nap(i * 40);  // also before the stream is ready
    const Ns t = monotonicNow();
    EXPECT_TRUE(player->stop());
    EXPECT_LT(monotonicNow() - t, ms(100));
  }
  EXPECT_TRUE(player->stop());  // nothing playing
  nap(300);
  EXPECT_EQ(finished, 0);
}

// Upper bound through a null output, whose own buffer is larger than a headset's may be.
// The real figure is to be measured on the panel.
TEST_F(PlayerTest, NothingIsHeardAfterStop) {
  PrerollRing ring(16000, 16000 * 10);
  LevelMeter meter;
  PulseCapture cap({.source = "out.monitor", .server = server}, ring, meter);
  ASSERT_TRUE(cap.start());
  ASSERT_TRUE(within(3000, [&] { return cap.status().state == CaptureState::Running; }));
  ASSERT_TRUE(player->play(1, sine(12'000), 16000));
  meter.takePeakDbfs();
  ASSERT_TRUE(within(6000, [&] { return meter.takePeakDbfs() > -20; })) << "nothing was played";
  nap(800);
  ASSERT_TRUE(player->stop());
  const Ns stopped = monotonicNow();
  nap(1500);
  const auto before = ring.extract(stopped - ms(400), stopped - ms(200));
  const auto peak = [](const std::vector<std::int16_t>& v) { int p = 0; for (auto s : v) p = std::max(p, std::abs(int(s))); return p; };
  EXPECT_GT(peak(before.samples), 4000);
  const auto after = ring.extract(stopped, stopped + ms(1000));
  ASSERT_EQ(after.missingHead + after.missingTail, 0u);
  int lastLoud = 0;
  for (std::size_t i = 0; i < after.samples.size(); ++i)
    if (std::abs(int(after.samples[i])) > 100) lastLoud = int(i);
  RecordProperty("tail_ms", lastLoud / 16);
  printf("heard after stop(): %d ms (null output)\n", lastLoud / 16);
  EXPECT_LT(lastLoud / 16, 100);
}

TEST_F(PlayerTest, ServerLossEndsThePlaybackAndReconnects) {
  ASSERT_TRUE(player->play(1, sine(5000), 16000));
  nap(300);
  stopServer();
  ASSERT_TRUE(within(2000, [this] { return finished == 1; }));
  EXPECT_FALSE(completed);
  EXPECT_FALSE(status().available);
  EXPECT_FALSE(player->play(2, sine(100), 16000));
  EXPECT_TRUE(player->stop());
  startServer();
  ASSERT_TRUE(within(8000, [this] { return status().available; }));
  EXPECT_TRUE(player->play(3, sine(100), 16000));
  EXPECT_TRUE(within(3000, [this] { return finished == 2 && completed; }));
}

// A server that has stopped answering must not hold the core thread.
TEST_F(PlayerTest, StopGivesUpOnAHungServer) {
  ASSERT_TRUE(player->play(1, sine(5000), 16000));
  nap(500);
  kill(pid, SIGSTOP);
  const Ns t = monotonicNow();
  EXPECT_FALSE(player->stop());
  EXPECT_LT(monotonicNow() - t, ms(400));
  kill(pid, SIGCONT);
}
