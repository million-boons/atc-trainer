#include <QCommandLineParser>
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
#include <memory>

#include "app/LabController.h"
#include "app/MockBackend.h"
#ifdef ATC_HAVE_LIVE
#include "app/LiveBackend.h"
#include "stt/SttFactory.h"
#endif
#include "app/SttController.h"
#ifdef ATC_HAVE_STT
#include "stt/SttCatalog.h"
#endif
#ifdef ATC_HAVE_PULSE
#include "audio/PulseSideTone.h"
#endif

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QCommandLineParser cli;
  cli.addHelpOption();
  const QCommandLineOption mock("mock", "Simulated PTT, audio and speech instead of the real devices.");
  const QCommandLineOption config("config", "UI settings file.", "file", ATC_ROOT "/config/ui.json");
  const QCommandLineOption size("size", "Window size, e.g. 1280x800. Default: full screen.", "WxH");
  const QCommandLineOption theme("theme", "dark, night or hc.", "mode");
  const QCommandLineOption state("state", "Mock case to show.", "id");
  const QCommandLineOption grab("grab", "Save a screenshot and quit.", "file.png");
  const QCommandLineOption modelsOpt("models", "Speech model list.", "file", ATC_ROOT "/config/stt_models.json");
  const QCommandLineOption root("root", "Project folder, for the model paths.", "dir", ATC_ROOT);
  const QCommandLineOption pttOpt("ptt-device", "PTT input device (overrides the settings file); \"none\" for no device.", "path");
  const QCommandLineOption pulseOpt("pulse-server", "PulseAudio server.", "address");
  cli.addOptions({pttOpt, pulseOpt, modelsOpt, root, mock, config, size, theme, state, grab});
  cli.process(app);
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
  if (!atc::loadSttCatalog(cli.value(modelsOpt).toStdString(), cli.value(root).toStdString(), models, error)) {
    models.clear();
    stt.setCatalogError(QString::fromStdString(error));
  }
#else
  stt.setCatalogError("built without the speech engines (tools/fetch_stt.sh)");
#endif
  std::unique_ptr<MockBackend> mockBackend;
  if (useMock) {
    stt.start(std::move(models), MockBackend::fakeEngine, [](const atc::SttModel& m) { return "{\"mock\":\"" + m.id + "\"}"; });
    mockBackend = std::make_unique<MockBackend>(&lab, &stt, cfg["pilotCutNoticeMs"].toInt(2000));
  }
#ifdef ATC_HAVE_LIVE
  std::unique_ptr<LiveBackend> live;
  if (!useMock) {
    LiveConfig lc;
    lc.pttDevice = cli.isSet(pttOpt) ? cli.value(pttOpt) : cfg["pttDevice"].toString();
    if (lc.pttDevice == "none") lc.pttDevice.clear();
    lc.pttName = cfg["pttName"].toString("PTT device");
    lc.pttGrab = cfg["pttGrab"].toBool(false);
    lc.micSource = cfg["micSource"].toString().toStdString();
    lc.pulseServer = cli.value(pulseOpt).toStdString();
    lab.textTag = "NOT VALIDATED";
    if (lc.pttDevice.isEmpty()) lab.idleHint = "Hold Screen PTT or Space";
    stt.start(std::move(models), [](const atc::SttModel& m, std::string& e) { return atc::makeRecognizer(m, ATC_GGML_DIR, e); },
              atc::describeModel, atc::availableMemoryMb);
    live = std::make_unique<LiveBackend>(&lab, &stt, lc);
    live->start();
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

  const QStringList wh = cli.value(size).split('x');
  const bool windowed = wh.size() == 2;
  QQmlApplicationEngine engine;
  engine.addImportPath("qrc:/");
  engine.setInitialProperties({{"lab", QVariant::fromValue(&lab)},
                               {"mock", QVariant::fromValue(mockBackend.get())},
                               {"stt", QVariant::fromValue(&stt)},
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
