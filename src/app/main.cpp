#include <QCommandLineParser>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>
#include <memory>

#include "app/AccessBackends.h"
#include "app/LabController.h"
#include "app/MockBackend.h"
#ifdef ATC_HAVE_LIVE
#include "app/LiveBackend.h"
#include "app/StudioService.h"
#include "storage/ConsentView.h"
#include "storage/DataCopy.h"
#include "stt/SttFactory.h"
#endif
#include "app/SettingsController.h"
#include "app/StudioController.h"
#include "app/SttController.h"
#include "core/PathResolver.h"
#ifdef ATC_HAVE_STT
#include "stt/SttCatalog.h"
#endif
#ifdef ATC_HAVE_PULSE
#include "audio/PulseSideTone.h"
#endif

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  atc::Paths paths;
  std::string pathError;
  // Before the parser: the defaults of the other options depend on it.
  auto pathInputs = atc::systemPathInputs();
  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i) {
    if (args[i] == "--root") pathInputs.root = i + 1 < args.size() ? args[i + 1].toStdString() : "";
    else if (args[i].startsWith("--root=")) pathInputs.root = args[i].mid(7).toStdString();
  }
  const bool havePaths = atc::resolvePaths(pathInputs, paths, pathError);
  const auto at = [](const std::string& s) { return QString::fromStdString(s); };
  QCommandLineParser cli;
  cli.addHelpOption();
  const QCommandLineOption mock("mock", "Simulated PTT, audio and speech instead of the real devices.");
  const QCommandLineOption config("config", "UI settings file.", "file", at(paths.config + "/ui.json"));
  const QCommandLineOption size("size", "Window size, e.g. 1280x800. Default: full screen.", "WxH");
  const QCommandLineOption theme("theme", "dark, night or hc.", "mode");
  const QCommandLineOption state("state", "Mock case to show.", "id");
  const QCommandLineOption grab("grab", "Save a screenshot and quit.", "file.png");
  const QCommandLineOption modelsOpt("models", "Speech model list.", "file", at(paths.config + "/stt_models.json"));
  const QCommandLineOption root("root", "Program files (holds config/, data/ and models) instead of the installed ones.", "dir");
  const QCommandLineOption pttOpt("ptt-device", "PTT input device (overrides the settings file); \"none\" for no device.", "path");
  const QCommandLineOption pulseOpt("pulse-server", "PulseAudio server.", "address");
  cli.addOptions({pttOpt, pulseOpt, modelsOpt, root, mock, config, size, theme, state, grab});
  cli.process(app);
  if (!havePaths) {
    qCritical("%s", pathError.c_str());
    return 2;
  }
  const bool useMock = cli.isSet(mock);
#ifndef ATC_HAVE_LIVE
  if (!useMock) {
    qCritical("Built without microphone, PTT or speech libraries. Start with --mock.");
    return 2;
  }
#endif

  QJsonObject cfg;
  QFile f(cli.value(config));
  if (f.open(QIODevice::ReadOnly)) {
    QJsonParseError err;
    cfg = QJsonDocument::fromJson(f.readAll(), &err).object();
    if (err.error != QJsonParseError::NoError)
      qWarning("%s: %s; using defaults", qPrintable(f.fileName()), qPrintable(err.errorString()));
  } else {
    qWarning("%s not found; using defaults", qPrintable(f.fileName()));
  }

  for (const char* n : {"Inter-Regular.otf", "Inter-SemiBold.otf", "Inter-Bold.otf", "JetBrainsMono-Regular.ttf",
                        "JetBrainsMono-Bold.ttf"})
    if (QFontDatabase::addApplicationFont(QStringLiteral(":/Atc/fonts/") + n) < 0)
      qWarning("bundled font %s did not load", n);

  LabController lab;
  lab.micLow = cfg["micTargetLowDbfs"].toDouble(-20);
  lab.micHigh = cfg["micTargetHighDbfs"].toDouble(-6);
  lab.micTarget = cfg["micTargetDbfs"].toDouble(-12);

  SttController stt;
  std::vector<atc::SttModel> models;
#ifdef ATC_HAVE_STT
  std::string error;
  if (!atc::loadSttCatalog(cli.value(modelsOpt).toStdString(), paths.root, models, error, paths.models)) {
    models.clear();
    stt.setCatalogError(QString::fromStdString(error));
  }
#else
  stt.setCatalogError("built without the speech engines (tools/fetch_stt.sh)");
#endif
  // Login, trainee records, settings and recordings (1c). Before the backends, which use them.
  const AccessBackend accessBackend = useMock ? mockAccess() : realAccess(paths.writable);
  AccessController access(accessBackend);
  access.setLab(&lab);
  access.setIdleLimit(atc::ms(60'000) * std::max(1, cfg["instructorIdleMinutes"].toInt(10)));
  app.installEventFilter(&access);
  SettingsController settings(&access, useMock ? QString() : at(paths.writable) + "/settings.json");
  RecordingsController recordings(&lab, &access, &settings);
  recordings.setBackend(useMock ? mockRecordings(recordings)
                                : realRecordings(paths.writable, cfg["recordingsMinFreeMb"].toInt(200), recordings));
  std::unique_ptr<MockBackend> mockBackend;
  if (useMock) {
    stt.start(std::move(models), MockBackend::fakeEngine, [](const atc::SttModel& m) { return "{\"mock\":\"" + m.id + "\"}"; });
    mockBackend = std::make_unique<MockBackend>(&lab, &stt, cfg["pilotCutNoticeMs"].toInt(2000));
    mockBackend->attach(&access, &recordings);
  }
#ifdef ATC_HAVE_LIVE
  // Order: the radio stops first (live), then the Studio's worker, then what it reports to.
  auto consent = std::make_shared<atc::ConsentView>();
  auto phraseology = std::make_shared<atc::DataCopy>(paths.data + "/phraseology", paths.writable);
  StudioController studio(&access);
  std::unique_ptr<LiveExercise> exercise;  // before the backend, which calls it until its radio has stopped
  std::unique_ptr<atc::StudioService> studioService;
  std::unique_ptr<LiveBackend> live;
  if (!useMock) {
    LiveConfig lc;
    QString ptt = cli.isSet(pttOpt) ? cli.value(pttOpt) : cfg["pttDevice"].toString();
    if (ptt == "none") ptt.clear();
    lc.pttName = cfg["pttName"].toString("PTT device");
    lc.devices.pttDevice = ptt.toStdString();
    lc.devices.pttGrab = cfg["pttGrab"].toBool(false);
    lc.devices.pttVidPid = cfg["pttVidPid"].toString().toStdString();
    lc.devices.pttCode = cfg["pttCode"].toInt(lc.devices.pttCode);
    lc.devices.micSource = cfg["micSource"].toString().toStdString();
    lc.devices.pulseServer = cli.value(pulseOpt).toStdString();
    lc.radio.pilotCutNotice = atc::ms(cfg["pilotCutNoticeMs"].toInt(2000));
    lc.status.pttName = lc.pttName;
    lc.status.pttStandIn = cfg["pttStandIn"].toBool(!lc.devices.pttGrab);  // an exclusive device is the pedal
    lc.status.pttMissingBlocks = cfg["pttMissingBlocks"].toBool(true);
    lc.status.storagePath = at(paths.writable);
    lc.status.storageWarnMb = cfg["recordingsWarnFreeMb"].toInt(1000);
    lab.textTag = "NOT VALIDATED";
    if (ptt.isEmpty()) lab.idleHint = "Hold Screen PTT or Space";
    stt.start(std::move(models), [engines = paths.engines](const atc::SttModel& m, std::string& e) { return atc::makeRecognizer(m, engines, e); },
              atc::describeModel, atc::availableMemoryMb);
    LiveExerciseSetup setup;
    std::string rulesError;
    if (!atc::loadParserRules(phraseology->dir() + "/placeholder.json", setup.rules, rulesError)) {
      qCritical("no exercise: %s", rulesError.c_str());
    } else {
      LiveExerciseConfig ec;
      ec.data = paths.data;
      ec.voices = paths.voices;
      ec.logs = paths.writable + "/exercises";
      ec.scenarioFile = paths.data + "/scenarios/" + cfg["scenario"].toString("DEPARTURE-01").toStdString() + ".json";
      ec.model = cfg["exerciseModel"].toString(at(ec.model)).toStdString();
      ec.pttMissingBlocks = lc.status.pttMissingBlocks;
      ec.mayUse = [consent](const std::string& id) { return consent->mayUse(id); };
      ec.phraseology = [phraseology] { return phraseology->dir(); };
      const std::string effect = cfg["radioEffect"].toString("light").toStdString();
      if (const auto r = atc::radioIntensityFromString(effect)) ec.radio = *r;
      else qWarning("radioEffect \"%s\" is not known; using light", effect.c_str());
      setup.runtime.recogniseLimit = atc::ms(std::clamp(cfg["recogniserTimeoutMs"].toInt(10'000), 1000, 60'000));
      exercise = std::make_unique<LiveExercise>(ec, &stt, &lab, &access, &settings);
      setup.callbacks = exercise->callbacks();
      lc.exercise = std::move(setup);
    }
    const auto answers = [&accessBackend, consent] { return consent->set(accessBackend.people ? accessBackend.people() : std::vector<atc::Person>{}, accessBackend.versions); };
    answers();
    if (exercise) {  // the Studio (09, D-331)
      atc::StudioService::Config sc;
      sc.data = paths.data;
      sc.phraseology = paths.data + "/phraseology";
      sc.writable = paths.writable;
      sc.activeBank = paths.voices;
      sc.voice = cfg["studioVoice"].toString("studio-1").toStdString();  // a new bank, never the one of the exercise (ST-2)
      sc.bank = paths.writable + "/voices/" + sc.voice;
      sc.pending = paths.writable + "/voices-pending/" + sc.voice;
      sc.minFreeMb = cfg["recordingsMinFreeMb"].toInt(200);
      LiveExercise* ex = exercise.get();
      studioService = std::make_unique<atc::StudioService>(
          sc, consent, phraseology,
          atc::StudioService::Rules{.instructor = [&access] { return access.instructor(); },
                                    .exerciseActive = [ex] { return ex->active(); },
                                    .today = [] { return QDate::currentDate().toString(Qt::ISODate).toStdString(); },
                                    .nowMs = [] { return QDateTime::currentMSecsSinceEpoch(); },
                                    .changed = [ex] { QMetaObject::invokeMethod(ex, [ex] { ex->recheck(); }, Qt::QueuedConnection); }});
      lc.studio = studioService.get();
      studio.setBackend(studioService->backend());
      QObject::connect(&access, &AccessController::changed, ex, [answers, ex, service = studioService.get()] {
        const bool differs = answers();
        service->consentChanged();  // also at log-out
        if (differs) ex->recheck();
      });
    }
    live = std::make_unique<LiveBackend>(&lab, &stt, lc, &recordings);
    live->start();
    if (exercise) exercise->connectRadio(live->exerciseRadio(), live->exerciseStatus(), live->status());
  }
#endif

#ifdef ATC_HAVE_PULSE
  atc::PulseSideTone tone({.lengthMs = cfg["sideToneMs"].toInt(20), .hz = cfg["sideToneHz"].toDouble(1000),
                           .dbfs = cfg["sideToneDbfs"].toDouble(-18)});
  if (cfg["sideTone"].toBool(true) && !cli.isSet(grab)) {
    tone.start();
    const auto play = [&] {
      if (!tone.play()) qWarning("side-tone not played: no audio server");
    };
    if (mockBackend) QObject::connect(mockBackend.get(), &MockBackend::sideTone, &app, play);
#ifdef ATC_HAVE_LIVE
    if (live) QObject::connect(live.get(), &LiveBackend::sideTone, &app, play);
#endif
  }
#endif

  StatusFeed* feed = mockBackend ? mockBackend->status() : nullptr;
#ifdef ATC_HAVE_LIVE
  if (live) feed = live->status();
#endif
  const QStringList wh = cli.value(size).split('x');
  const bool windowed = wh.size() == 2;
  QQmlApplicationEngine engine;
  engine.addImportPath("qrc:/");
  ExerciseController* exerciseView = nullptr;
#ifdef ATC_HAVE_LIVE
  if (exercise) exerciseView = exercise->controller();
#endif
  StudioController* studioView = nullptr;
#ifdef ATC_HAVE_LIVE
  if (studioService) studioView = &studio;
#endif
  engine.setInitialProperties({{"lab", QVariant::fromValue(&lab)},
                               {"studio", QVariant::fromValue(studioView)},
                               {"exercise", QVariant::fromValue(exerciseView)},
                               {"mock", QVariant::fromValue(mockBackend.get())},
                               {"stt", QVariant::fromValue(&stt)},
                               {"feed", QVariant::fromValue(feed)},
                               {"access", QVariant::fromValue(&access)},
                               {"settings", QVariant::fromValue(&settings)},
                               {"recordings", QVariant::fromValue(&recordings)},
                               {"exerciseModel", cfg["exerciseModel"].toString("whisper-tiny-genprompt")},  // D-414
                               {"themeMode", cli.isSet(theme) ? cli.value(theme) : cfg["theme"].toString("dark")},
                               {"reduceMotion", cfg["reduceMotion"].toBool(false)}});
  engine.load(QUrl("qrc:/Atc/Main.qml"));
  if (engine.rootObjects().isEmpty()) return 1;
  auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (windowed) {
    win->resize(wh[0].toInt(), wh[1].toInt());
    win->show();
  } else {
    win->showFullScreen();
  }
  if (mockBackend && cli.isSet(state)) mockBackend->run(cli.value(state));
  if (cli.isSet(grab))
    QTimer::singleShot(useMock ? 700 : 2500, &app, [&] {
      // Item grab, not window grab: the offscreen platform cannot read its window back.
      const auto shot = win->contentItem()->grabToImage();
      if (!shot) { qCritical("grab: not possible"); return app.exit(3); }
      QObject::connect(shot.data(), &QQuickItemGrabResult::ready, &app, [&, shot] {
        QImage img(shot->image().size(), QImage::Format_RGB32);
        img.fill(win->color());
        QPainter(&img).drawImage(0, 0, shot->image());
        const bool ok = !img.isNull() && img.save(cli.value(grab));
        if (!ok) qCritical("grab: cannot write %s", qPrintable(cli.value(grab)));
        app.exit(ok ? 0 : 3);
      });
    });
  return app.exec();
}
