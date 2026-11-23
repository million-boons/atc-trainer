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

#include "app/LabController.h"
#include "app/MockBackend.h"
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
  const QCommandLineOption mock("mock", "Run on the mock backend (the only backend so far).");
  const QCommandLineOption config("config", "UI settings file.", "file", "config/ui.json");
  const QCommandLineOption size("size", "Window size, e.g. 1280x800. Default: full screen.", "WxH");
  const QCommandLineOption theme("theme", "dark, night or hc.", "mode");
  const QCommandLineOption state("state", "Mock case to show.", "id");
  const QCommandLineOption grab("grab", "Save a screenshot and quit.", "file.png");
  const QCommandLineOption modelsOpt("models", "Speech model list.", "file", "config/stt_models.json");
  const QCommandLineOption root("root", "Project folder, for the model paths.", "dir", ".");
  cli.addOptions({modelsOpt, root, mock, config, size, theme, state, grab});
  cli.process(app);
  if (!cli.isSet(mock)) {
    qCritical("Only the mock backend exists. Start with --mock.");
    return 2;
  }

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
  stt.start(std::move(models), MockBackend::fakeEngine, [](const atc::SttModel& m) { return "{\"mock\":\"" + m.id + "\"}"; });
  MockBackend backend(&lab, &stt, cfg["pilotCutNoticeMs"].toInt(2000));

#ifdef ATC_HAVE_PULSE
  atc::PulseSideTone tone({.lengthMs = cfg["sideToneMs"].toInt(20), .hz = cfg["sideToneHz"].toDouble(1000),
                           .dbfs = cfg["sideToneDbfs"].toDouble(-18)});
  if (cfg["sideTone"].toBool(true) && !cli.isSet(grab)) {
    tone.start();
    QObject::connect(&backend, &MockBackend::sideTone, &app, [&] {
      if (!tone.play()) qWarning("side-tone not played: no audio server");
    });
  }
#endif

  const QStringList wh = cli.value(size).split('x');
  const bool windowed = wh.size() == 2;
  QQmlApplicationEngine engine;
  engine.addImportPath("qrc:/");
  engine.setInitialProperties({{"lab", QVariant::fromValue(&lab)},
                               {"mock", QVariant::fromValue(&backend)},
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
  if (cli.isSet(state)) backend.run(cli.value(state));
  if (cli.isSet(grab))
    QTimer::singleShot(700, &app, [&] {
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
