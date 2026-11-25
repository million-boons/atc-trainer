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
#include "app/EngineHeaders.h"
#include "app/LiveBackend.h"
#include "audio/PulsePlayer.h"
#include "audio/WavFile.h"
#include "core/PathResolver.h"
#include <mutex>
#include <nlohmann/json.hpp>
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
std::mutex wordsMutex;
std::vector<std::string> words;  // what the next transmissions are recognised as
struct CountingRecognizer : ISpeechRecognizer {
  std::string name() const override { return "counting"; }
  void begin() override { ++begins; }
  void accept(std::span<const std::int16_t> a) override { samples += int(a.size()); }
  SttResult finish() override {
    std::lock_guard l(wordsMutex);
    if (words.empty()) return {"counted", {{"counted", 0.5}}};
    SttResult r{words.front(), {}};
    words.erase(words.begin());
    return r;
  }
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
    configure(c);
    live = std::make_unique<LiveBackend>(&lab, &stt, c, &recordings);
    ASSERT_TRUE(live->start());
    ASSERT_TRUE(within(8000, [this] { return lab.micDbfs() > -30; })) << "the simulated microphone is not heard";
  }
  virtual void configure(LiveConfig&) {}
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

// A whole departure in the app's own objects: pedal edge to the pilot's sound, only the recognised words are scripted.
struct LiveDeparture : Live {
  std::unique_ptr<LiveExercise> ex;
  std::unique_ptr<LiveBackend>& backend = live;
  std::vector<std::string> faults;
  PreparedExercise prepared;
  fs::path logs;

  void configure(LiveConfig& c) override {
    Paths p;
    std::string error;
    ASSERT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
    const std::string scenario = p.data + "/scenarios/DEPARTURE-01.json";
    const fs::path bank = dir / "bank";
    fs::create_directories(bank);
    Exercise e;
    std::vector<LoadError> errors;
    ASSERT_TRUE(loadExercise(scenarioSources(p.data, scenario), e, errors));
    std::map<std::string, RequiredClip> need;
    std::vector<std::string> bad;
    ASSERT_TRUE(requiredClips(e, need, bad));
    nlohmann::json m = {{"schemaVersion", 1}, {"voice", "test"}, {"sampleRate", 16000}, {"placeholder", true}, {"clips", nlohmann::json::array()}};
    int n = 0;
    for (const auto& [id, clip] : need) {
      const std::string file = "c" + std::to_string(n++) + ".wav";
      ASSERT_TRUE(writeWav((bank / file).string(), 16000, std::vector<std::int16_t>(1600, 1000)));
      nlohmann::json take = {{"file", file}, {"take", "primary"}};
      m["clips"].push_back({{"id", id}, {"takes", nlohmann::json::array({take})}});
    }
    std::ofstream(bank / "manifest.json") << m.dump();
    prepared = ExerciseCheck::check(p.data, bank.string(), scenario).ready;
    ASSERT_TRUE(prepared);
    logs = home / "data" / "exercises";
    LiveExerciseConfig ec;
    ec.data = p.data;
    ec.voices = bank.string();
    ec.logs = logs.string();
    ec.scenarioFile = scenario;
    ec.model = "counting";
    ec.pttMissingBlocks = false;  // there is no PTT device here: Screen PTT, as a fallback
    ex = std::make_unique<LiveExercise>(ec, &stt, &lab, &access, &settings);
    LiveExerciseSetup s;
    ASSERT_TRUE(loadParserRules(p.data + "/phraseology/placeholder.json", s.rules, error)) << error;
    s.runtime.recogniseLimit = ms(1500);
    s.callbacks = ex->callbacks();
    s.callbacks.fault = [this, f = s.callbacks.fault](const std::string& w) { faults.push_back(w); f(w); };
    c.exercise = std::move(s);
  }
  void SetUp() override {
    Live::SetUp();
    if (IsSkipped() || HasFatalFailure()) return;
    ex->connectRadio(live->exerciseRadio(), live->exerciseStatus(), live->status());
    ASSERT_TRUE(within(8000, [this] { return live->status()->inputs().bank == BankReady::CoverageOk; }));
  }
  void TearDown() override {
    Live::TearDown();
    ex.reset();
  }
  ExerciseController& c() { return *ex->controller(); }
  void begin() {
    emit live->status()->startExerciseRequested();
    ASSERT_TRUE(within(1000, [this] { return c().phase() == ExerciseController::Starting; }));
    c().start(false);
    ASSERT_TRUE(within(2000, [this] { return c().phase() == ExerciseController::Running; })) << c().notice().toStdString();
  }
  int pilotTurns() {
    int k = 0;
    for (int i = 0; i < c().transcript()->rowCount(); ++i)
      k += c().transcript()->data(c().transcript()->index(i, 0), Qt::UserRole).toInt() == ExerciseController::Pilot;
    return k;
  }
  // The pilot has spoken for the n-th time and the radio is free again.
  void pilotHeard(int n) {
    ASSERT_TRUE(within(15000, [&] { return pilotTurns() >= n; })) << n;
    ASSERT_TRUE(within(15000, [this] { return lab.txLabel() == "IDLE" && !lab.rxActive(); })) << n;
  }
  void key() {
    emit lab.pressRequested("SCREEN");
    ASSERT_TRUE(within(2000, [this] { return lab.txLabel() == "TX"; }));
    spin(700);
    emit lab.releaseRequested("SCREEN");
  }
  void script() {
    const QVariantMap card = live->status()->inputs().scenario;
    std::string call;
    for (const auto& x : prepared.exercise->airfield.callsigns)
      if (x.id == card["callsign"].toString().toStdString()) call = x.spoken;
    ASSERT_FALSE(call.empty());
    const std::string rwy = card["runway"] == "09" ? "zero nine" : "two seven", twy = card["taxiway"] == "A3" ? "alpha three" : "bravo two";
    std::lock_guard l(wordsMutex);
    words = {call + " taxi via " + twy + " runway " + rwy, call + " line up runway " + rwy, call + " runway " + rwy + " cleared for takeoff"};
  }
  QStringList logLines() const {
    QStringList l;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(logs, ec)) {
      std::ifstream f(e.path());
      for (std::string s; std::getline(f, s);) l << QString::fromStdString(s);
    }
    return l;
  }
};

TEST_F(LiveDeparture, ByVoiceFromStartToResult) {
  ASSERT_TRUE(access.loginTrainee("S01"));  // consent given, and still no exercise audio (D-314)
  begin();
  ASSERT_FALSE(HasFatalFailure());
  script();
  EXPECT_TRUE(live->status()->inputs().running);
  EXPECT_TRUE(stt.locked());
  EXPECT_FALSE(stt.canLoad("counting"));
  for (int turn = 1; turn <= 3; ++turn) {
    pilotHeard(turn);
    ASSERT_FALSE(HasFatalFailure());
    key();
    ASSERT_FALSE(HasFatalFailure());
  }
  ASSERT_TRUE(within(20000, [this] { return c().phase() == ExerciseController::Complete; })) << c().progressText().toStdString();
  EXPECT_EQ(c().progressText(), "3 of 3 done");
  EXPECT_TRUE(faults.empty()) << faults.front();
  EXPECT_FALSE(live->status()->inputs().running);
  EXPECT_TRUE(within(2000, [this] { return !stt.locked(); }));
  c().showResult();
  EXPECT_EQ(c().result()["title"].toString(), "COMPLETED");
  EXPECT_EQ(lab.recordings()->rowCount(), 0);  // the exercise's, not the Lab's
  EXPECT_TRUE(stored(".wav").empty());
  EXPECT_FALSE(c().audioSaved());
  const QStringList l = logLines();
  ASSERT_GT(l.size(), 10);
  EXPECT_TRUE(l.first().contains("counting"));
  EXPECT_EQ(l.filter("fallback_ptt_used").size(), 3);
  EXPECT_EQ(l.filter("\"ended\"").size(), 1);
  // The Lab is the radio's client again.
  c().leaveResult();
  transmit();
  EXPECT_EQ(lab.recordings()->rowCount(), 1);
  EXPECT_EQ(stored(".wav").size(), 1u);
}

TEST_F(LiveDeparture, StopSilencesThePilotAndFreesTheModel) {
  begin();
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(within(15000, [this] { return lab.rxActive(); }));
  c().stop();
  EXPECT_TRUE(within(1500, [this] { return !lab.rxActive(); }));
  EXPECT_TRUE(within(2000, [this] { return c().phase() == ExerciseController::Result && !stt.locked(); }));
  EXPECT_EQ(c().result()["title"].toString(), "STOPPED BY INSTRUCTOR");
  EXPECT_FALSE(live->status()->inputs().running);
  EXPECT_TRUE(logLines().last().contains("ended"));
}

// The window closes in the middle of an exercise.
TEST_F(LiveDeparture, ClosingTheAppInAnExerciseKeepsTheLog) {
  begin();
  ASSERT_FALSE(HasFatalFailure());
  pilotHeard(1);
  emit lab.pressRequested("SCREEN");
  ASSERT_TRUE(within(2000, [this] { return lab.txLabel() == "TX"; }));
  live.reset();
  spin(100);
  EXPECT_FALSE(logLines().filter("tx_opened").isEmpty());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
