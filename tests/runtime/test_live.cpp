#include <gtest/gtest.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

#include "app/AccessBackends.h"
#include "app/LiveBackend.h"
#include "audio/PulsePlayer.h"
using namespace atc;
namespace fs = std::filesystem;

namespace {
void spin(int millis) {
  QEventLoop l;
  QTimer::singleShot(millis, Qt::PreciseTimer, &l, &QEventLoop::quit);
  l.exec();
}

template <class F> bool within(int millis, F f) {
  for (int t = 0; t < millis; t += 50) {
    if (f()) return true;
    spin(50);
  }
  return f();
}

std::atomic<int> begins{0}, samples{0};
struct CountingRecognizer : ISpeechRecognizer {
  std::string name() const override { return "counting"; }
  void begin() override { ++begins; }
  void accept(std::span<const std::int16_t> a) override { samples += int(a.size()); }
  SttResult finish() override { return {"counted", {{"counted", 0.5}}}; }
};

// The app's backend on real PulseAudio code, against a private server with a simulated microphone.
// Never the user's sound.
struct Live : testing::Test {
  fs::path dir, home;
  std::string server;
  pid_t pid = 0;
  std::unique_ptr<PulsePlayer> voice;  // speaks into the simulated microphone
  LabController lab;
  SttController stt;
  // The real store in the empty home; trainee S01 gave consent, S02 did not.
  AccessController access{mockAccess()};
  SettingsController settings{&access, QString()};
  RecordingsController recordings{&lab, &access, &settings};
  std::unique_ptr<LiveBackend> live;

  void SetUp() override {
    if (::access("/usr/bin/pulseaudio", X_OK) != 0) GTEST_SKIP() << "pulseaudio is not installed";
    if (geteuid() == 0) GTEST_SKIP() << "pulseaudio does not run as root";
    dir = fs::path(testing::TempDir()) / ("atc_live_" + std::to_string(getpid()));
    home = dir / "home";
    fs::create_directories(home / "data");
    fs::create_directories(dir / "pa");
    setenv("HOME", home.c_str(), 1);
    for (const char* v : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "TMPDIR"}) setenv(v, (home / "data").c_str(), 1);
    ASSERT_EQ(chdir(home.c_str()), 0);
    std::ofstream(dir / "pa" / "cookie").close();
    setenv("PULSE_COOKIE", (dir / "pa" / "cookie").c_str(), 1);
    server = "unix:" + (dir / "pa" / "sock").string();
    const std::string mod = "module-native-protocol-unix auth-anonymous=1 socket=" + (dir / "pa" / "sock").string();
    pid = fork();
    if (pid == 0) {
      for (const char* v : {"PULSE_RUNTIME_PATH", "PULSE_STATE_PATH", "HOME"}) setenv(v, (dir / "pa").c_str(), 1);
      setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent", 1);
      execl("/usr/bin/pulseaudio", "pulseaudio", "-n", "--daemonize=no", "--use-pid-file=no", "--exit-idle-time=-1", "--disable-shm=yes", "--log-level=error",
            "-L", "module-null-sink sink_name=out", "-L", "module-null-sink sink_name=m", "-L", "module-remap-source source_name=mic master=m.monitor",
            "-L", mod.c_str(), nullptr);
      _exit(127);
    }
    voice = std::make_unique<PulsePlayer>(PlayerCallbacks{[](PlayId, bool) {}, [](const OutputStatus&) {}}, server, "m");
    ASSERT_TRUE(voice->start());
    std::vector<std::int16_t> speech(16000 * 40);
    for (std::size_t i = 0; i < speech.size(); ++i) speech[i] = std::int16_t(8000 * std::sin(double(i) * 0.2));
    ASSERT_TRUE(within(5000, [&] { return voice->play(1, speech, 16000); })) << "no private audio server";

    SttModel m;
    m.id = m.name = "counting";
    m.installed = true;
    stt.start({m}, [](const SttModel&, std::string&) { return std::make_unique<CountingRecognizer>(); },
              [](const SttModel&) { return std::string("{\"id\":\"counting\",\"threads\":4}"); });
    stt.loadModel("counting");
    ASSERT_TRUE(within(5000, [this] { return stt.status().state == SttState::Ready; }));

    access.setLab(&lab);
    recordings.setBackend(realRecordings((home / "data").string(), 0, recordings));
    LiveConfig c;
    c.devices.micSource = "mic";
    c.devices.pulseServer = server;
    c.status.storagePath = QString::fromStdString((home / "data").string());
    live = std::make_unique<LiveBackend>(&lab, &stt, c, &recordings);
    ASSERT_TRUE(live->start());
    ASSERT_TRUE(within(8000, [this] { return lab.micDbfs() > -30; })) << "the simulated microphone is not heard";
  }
  void TearDown() override {
    live.reset();
    EXPECT_FALSE(recordings.playerReady());  // nothing points at the radio any more
    recordings.setBackend({});
    voice.reset();
    if (pid > 0) {
      kill(pid, SIGTERM);
      waitpid(pid, nullptr, 0);
    }
    if (!dir.empty()) {
      EXPECT_EQ(chdir("/"), 0);
      fs::remove_all(dir);
    }
  }
  // One transmission from the screen button, until it is in the list and, if recognised, its text has come.
  void transmit() {
    const int before = lab.recordings()->rowCount();
    emit lab.pressRequested("SCREEN");
    EXPECT_TRUE(within(2000, [this] { return lab.txLabel() == "TX"; }));
    spin(700);
    emit lab.releaseRequested("SCREEN");
    EXPECT_TRUE(within(5000, [&] { return lab.recordings()->rowCount() == before + 1; }));
    EXPECT_TRUE(within(5000, [this] { return lab.txLabel() == "IDLE" && lab.textState() == "Final"; }));
    spin(300);  // a save would be written by now
  }
  void instructor() {
    access.loginInstructor("123456");
    EXPECT_TRUE(within(5000, [this] { return !access.busy() && access.instructor(); }));
  }
  std::set<std::string> stored(const char* ext) const {
    std::set<std::string> s;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(home / "data" / "recordings", ec))
      if (e.path().extension() == ext) s.insert(e.path().string());
    return s;
  }
  std::set<std::string> files() const {
    std::set<std::string> s;
    for (const auto& e : fs::recursive_directory_iterator(home))
      if (e.is_regular_file()) s.insert(e.path().string() + " " + std::to_string(e.file_size()));
    return s;
  }
};
}  // namespace

// D-312, in the app: the open microphone hears speech, and without PTT nothing comes of it.
TEST_F(Live, OpenMicrophoneWithoutPttRecognisesRecordsAndWritesNothing) {
  const auto before = files();
  EXPECT_EQ(lab.txSub(), "Mic open – not transmitting");
  spin(2000);
  emit live->status()->micTestRequested();
  spin(5500);
  EXPECT_EQ(live->status()->inputs().micTestLeft, 0);
  emit live->status()->pttTestRequested();
  spin(200);
  emit lab.pressRequested("SPACE");
  spin(600);
  emit lab.releaseRequested("SPACE");
  spin(200);
  emit live->status()->cancelTestsRequested();
  spin(500);
  EXPECT_GT(lab.micDbfs(), -30);  // it was listening to speech all the time
  EXPECT_EQ(lab.recordings()->rowCount(), 0);
  EXPECT_EQ(begins, 0);
  EXPECT_EQ(samples, 0);
  EXPECT_EQ(lab.textState(), "Waiting");
  EXPECT_TRUE(lab.text().isEmpty());
  EXPECT_EQ(files(), before);

  // Control: the same set-up records and recognises as soon as PTT is pressed.
  emit lab.pressRequested("SCREEN");
  EXPECT_TRUE(within(2000, [this] { return lab.txLabel() == "TX"; }));
  EXPECT_FALSE(live->status()->inputs().micOpenIdle);
  spin(800);
  emit lab.releaseRequested("SCREEN");
  EXPECT_TRUE(within(5000, [this] { return lab.recordings()->rowCount() == 1; }));
  EXPECT_TRUE(within(5000, [] { return begins == 1 && samples > 8000; }));
  std::ofstream(home / "data" / "control") << "x";
  EXPECT_NE(files(), before);
}

// Every refusal leaves the disk as it was; the radio works the same.
TEST_F(Live, NothingIsSavedWithoutSwitchSpeakerAndConsent) {
  const auto before = files();
  transmit();  // nobody logged in
  EXPECT_TRUE(recordings.lineText().startsWith("Not saved: log in")) << recordings.lineText().toStdString();
  ASSERT_TRUE(access.loginTrainee("S02"));
  transmit();
  EXPECT_EQ(recordings.lineText(), "Not saved: no consent");
  instructor();  // is not a speaker
  transmit();
  settings.setSaveRecordings(false);
  ASSERT_TRUE(access.loginTrainee("S01"));
  transmit();
  EXPECT_EQ(recordings.lineText(), "Saving is off");
  EXPECT_EQ(files(), before);
  // Control: the switch was the only thing missing.
  instructor();
  settings.setSaveRecordings(true);
  ASSERT_TRUE(access.loginTrainee("S01"));
  EXPECT_EQ(recordings.lineText(), "Saved on this machine only");
  transmit();
  EXPECT_EQ(stored(".wav").size(), 1u);
}

TEST_F(Live, SavesTheTransmissionWithSpeakerSourceAndText) {
  spin(1000);  // the microphone has run for longer than the pre-roll
  ASSERT_TRUE(access.loginTrainee("S01"));
  const auto before = files();
  const auto t0 = QDateTime::currentMSecsSinceEpoch();
  transmit();
  ASSERT_EQ(stored(".wav").size(), 1u);
  ASSERT_EQ(stored(".json").size(), 1u);
  EXPECT_EQ(files().size(), before.size() + 2) << *files().rbegin();  // and nowhere else
  std::ifstream f(*stored(".json").begin());
  const QJsonObject j = QJsonDocument::fromJson(QByteArray::fromStdString({std::istreambuf_iterator<char>(f), {}})).object();
  EXPECT_EQ(j["speaker"], "S01");
  EXPECT_EQ(j["ptt_source"], "SCREEN");
  EXPECT_EQ(j["end"], "normal");
  EXPECT_EQ(j["tx"], 1);
  EXPECT_LT(std::abs(j["started_ms"].toDouble() - double(t0)), 500);
  EXPECT_GT(j["samples"].toInt(), 16000 * 7 / 10);
  EXPECT_NEAR(j["press_sample"].toInt(), 4800, 800);  // 0.3 s before the press
  EXPECT_NEAR(j["end_sample"].toInt() - j["press_sample"].toInt(), 16000 * 7 / 10, 1600);
  ASSERT_EQ(j["recognition"].toArray().size(), 1);
  const QJsonObject r = j["recognition"].toArray()[0].toObject();
  EXPECT_EQ(r["raw"], "counted");
  EXPECT_EQ(r["stt_model_id"], "counting");
  EXPECT_EQ(r["stt_model_config"].toObject()["threads"], 4);
  EXPECT_EQ(r["words"].toArray().size(), 1);
  EXPECT_EQ(fs::status(*stored(".wav").begin()).permissions(), fs::perms::owner_read | fs::perms::owner_write);
  EXPECT_EQ(fs::file_size(*stored(".wav").begin()), 44u + 2u * unsigned(j["samples"].toInt()));
}

TEST_F(Live, StoredRecordingPlaysOnlyForTheInstructorAndNotOnTheAir) {
  ASSERT_TRUE(access.loginTrainee("S01"));
  transmit();
  EXPECT_FALSE(recordings.canPlay());
  instructor();
  ASSERT_TRUE(recordings.canPlay());
  recordings.refreshStored();
  ASSERT_TRUE(within(3000, [this] { return recordings.stored()->rowCount() == 1; }));
  emit lab.pressRequested("SCREEN");
  ASSERT_TRUE(within(2000, [this] { return lab.txLabel() == "TX"; }));
  recordings.playStored(0);
  EXPECT_TRUE(within(3000, [this] { return lab.notice() == "No playback while the radio is in use."; })) << lab.notice().toStdString();
  emit lab.releaseRequested("SCREEN");
  EXPECT_TRUE(within(5000, [this] { return lab.txLabel() == "IDLE"; }));
  lab.setNotice(LabController::Caution, "before");
  recordings.playStored(0);
  EXPECT_TRUE(within(3000, [this] { return lab.notice().isEmpty(); }));  // cleared when the sound starts
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
