#include "app/ExerciseMock.h"

#include "app/LabController.h"

namespace exercise_mock {
namespace {
using L = LabController;
QString t(const char* s) { return ExerciseController::tr(s); }

QVariantList chips(std::initializer_list<const char*> values) {  // a trailing "?" marks an uncertain value
  QVariantList l;
  for (const char* s : values) {
    const QString v = s;
    l.append(QVariantMap{{"value", v}, {"uncertain", v.endsWith('?')}});
  }
  return l;
}
void taxi(ExerciseController& c) {
  c.addAtc("10:41:32", "eagle four two one taxi via alpha three runway zero nine", "TAXI", chips({"EAGLE-421", "TWY A3", "RWY 09"}), "RECOGNIZED", true, "2.1 s");
  c.addPilot("10:41:38", "taxi via A3, runway 09, EAGLE-421");
}
void call(ExerciseController& c) { c.addPilot("10:41:20", "request taxi, EAGLE-421"); }
void lineUp(ExerciseController& c) {
  c.addAtc("10:43:02", "eagle four two one runway zero nine line up and wait", "LINE UP AND WAIT", chips({"EAGLE-421", "RWY 09"}), "RECOGNIZED", true, "2.1 s");
  c.addPilot("10:43:08", "lining up runway 09 and waiting, EAGLE-421");
}

QVariantMap stepRow(const QString& name, int level, const QString& outcome, int attempts, const QString& errors, const QString& response, const QString& again, const QString& missed = "0") {
  return {{"name", name}, {"level", level}, {"outcome", outcome}, {"attempts", attempts}, {"errors", errors}, {"response", response}, {"sayAgain", again}, {"missed", missed}};
}

void result(ExerciseController& c, bool stopped) {
  const auto row = [](const QString& name, const QString& value) { return QVariantMap{{"name", name}, {"value", value}}; };
  QVariantList steps{stepRow("1 Taxi", L::Ok, t("Correct"), 1, t("none"), "4.1 s", "0")};
  if (stopped)
    steps << stepRow("2 Line up", L::Caution, t("Not completed"), 2, t("INCORRECT runway"), "6.0 s", "1", "1, 30.0 s")
          << stepRow("3 Takeoff", L::None, t("Not reached"), 0, "-", "-", "0");
  else
    steps << stepRow("2 Line up", L::Ok, t("Correct"), 3, t("INCORRECT runway"), "5.2 s", "1")
          << stepRow("3 Takeoff", L::Ok, t("Correct"), 1, t("none"), "3.6 s", "0");
  const QVariantList ptt{row(t("Transmissions"), stopped ? "3" : "5"), row(t("Spoke before keying"), "1"), row(t("Released early"), "0"),
                         row(t("Short taps"), "1"), row(t("No speech"), "0"), row(t("Keyed over the pilot"), "1"), row(t("Called before the pilot"), "0"), row(t("Key to speech, median"), "0.41 s"),
                         row(t("Fallback PTT used"), t("no"))};
  const QVariantList speech{row(t("Speech model"), "Whisper tiny + generated prompt"), row(t("Release to text, median"), "2.1 s"), row(t("Faults during exercise"), "0")};
  QList<QVariantList> line{
      QVariantList{"10:41:20", "PILOT", "request taxi, EAGLE-421", "", 0},
      QVariantList{"10:41:32", "ATC", "eagle four two one taxi via alpha three runway zero nine", "CORRECT", int(L::Ok)},
      QVariantList{"10:41:38", "PILOT", "taxi via A3, runway 09, EAGLE-421", "", 0},
      QVariantList{"10:42:10", "ATC", "eagle four two one runway two seven line up and wait", "INCORRECT", int(L::Caution)},
      QVariantList{"10:42:16", "PILOT", "query instruction, EAGLE-421", "", 0},
      QVariantList{"10:42:31", "ATC", "eagle four two one runway 0.9 line up and wait", t("AMBIGUOUS, not counted"), int(L::Caution)},
      QVariantList{"10:42:38", "PILOT", "say again, EAGLE-421", "", 0}};
  if (!stopped) {
    line << QVariantList{"10:43:02", "ATC", "eagle four two one runway zero nine line up and wait", "CORRECT", int(L::Ok)}
         << QVariantList{"10:43:08", "PILOT", "lining up runway 09 and waiting, EAGLE-421", "", 0};
  }
  c.setResult(QVariantMap{{"title", stopped ? t("STOPPED BY INSTRUCTOR") : t("COMPLETED")},
               {"level", int(stopped ? L::Caution : L::Ok)},
               {"sub", t("S01   Departure 01   EAGLE-421   24 Nov 10:41 to 10:44")},
               {"stepsDone", stopped ? t("1 of 3") : t("3 of 3")},
               {"missed", stopped ? "1" : "0"},
               {"duration", "3:17"},
               {"steps", steps},
               {"ptt", ptt},
               {"suspectedLevel", int(L::Caution)},
               {"suspectedTitle", t("1, not counted against the trainee")},
               {"suspectedText", t("Line up, attempt 2: runway heard as \"0.9\", classed AMBIGUOUS. The pilot said \"say again\".")},
               {"speech", speech}},
              line);
}
}  // namespace

bool isCase(const QString& id) { return id.startsWith("ex"); }
bool running(const QString& id) { return isCase(id) && id != "exstart" && id != "exresult" && id != "exresulta"; }

QString radioCase(const QString& id) {
  if (id == "extx") return "tx";
  if (id == "exproc") return "recog";
  if (id == "excall" || id == "exrx" || id == "extime") return "rx";
  if (id == "excut") return "cut";
  if (id == "exblocked") return "blocked";
  if (id == "exqueued") return "queued";
  if (id == "exlost") return "lost";
  if (id == "exwd") return "ptimeout";
  return "idle";
}

QVariantList cases() {
  const std::pair<const char*, const char*> all[] = {
      {"exstart", "Start dialog"}, {"excall", "Pilot calls first"}, {"exwait", "Waiting for ATC"}, {"extx", "Transmitting"},
      {"exproc", "Processing"}, {"exrx", "Pilot replying"}, {"examb", "Ambiguous: say again"}, {"exwrong", "Wrong runway"},
      {"excs", "Wrong callsign: silence"}, {"extime", "No call: pilot calls again"}, {"exchoice", "No call: instructor decides"}, {"excut", "PILOT CUT"}, {"exblocked", "BLOCKED"},
      {"exqueued", "Queued"}, {"exlimit", "Say-again limit"}, {"exlost", "PTT lost"}, {"exwd", "Pilot voice fault"},
      {"exdone", "All steps complete"}, {"exresult", "RESULT: completed"}, {"exresulta", "RESULT: stopped"}};
  QVariantList l;
  for (auto [id, name] : all) l.append(QVariantMap{{"id", id}, {"name", name}});
  return l;
}

void show(ExerciseController& c, const QString& id, bool assessed, bool consent, const PlanChoice* plan) {
  using C = ExerciseController;
  ExerciseContext x{.scenario = t("Departure 01"), .callsign = "EAGLE-421", .runway = "09", .taxiway = "A3", .type = "C295",
                    .rule = t("interrupt"), .model = "Whisper tiny + generated prompt", .trainee = "S01", .placeholder = true};
  if (plan && id == "exstart") {
    const auto q = [](const std::string& v) { return QString::fromStdString(v); };
    x.callsign = q(plan->values.callsign);
    x.type = q(plan->values.type);
    x.runway = q(plan->values.runway);
    x.taxiway = q(plan->values.taxiway);
    x.plan = ExercisePlan{q(plan->plan.name), q(plan->plan.trainee), plan->plan.assessed, plan->run};
  }
  c.begin(std::move(x), assessed, consent, assessed ? t("PREVIEW: Assessed is off in the slice (placeholder text)") : QString());
  c.setPttStandIn(id == "exresulta");  // the stopped result shows a stand-in run (D-435)
  const auto steps = [&c](int done, int a, int b, int d) { c.setSteps({{t("Taxi"), a}, {t("Line up"), b}, {t("Takeoff"), d}}, done); };
  const QString cTaxi = t("Taxi via A3, runway 09"), cLine = t("Line up and wait runway 09"), cGo = t("Cleared for takeoff 09");
  C::Phase phase = C::Running;
  // Defaults: waiting for the second instruction.
  steps(1, 1, 0, 0);
  c.setClearances({cTaxi});
  c.setWaiting("00:14");
  c.setAlert(L::Ok, t("Exercise running"));

  if (id == "exstart") {
    phase = C::Starting;
    steps(0, 0, 0, 0);
    c.setClearances({});
    c.setWaiting({});
    c.setAlert(L::Ok, t("Exercise not started"));
  } else if (id == "excall") {
    steps(0, 0, 0, 0);
    c.setClearances({});
    c.setWaiting({});
    call(c);
  } else if (id == "extx") {
    call(c); taxi(c);
    c.setAlert(L::Ok, t("Text appears after release"));
  } else if (id == "exproc") {
    call(c); taxi(c);
    c.addNote(L::None, t("Processing: the text appears here"));
  } else if (id == "exrx") {
    steps(2, 1, 1, 0);
    taxi(c); lineUp(c);
    c.setClearances({cTaxi, cLine});
    c.setWaiting({});
  } else if (id == "examb") {
    steps(1, 1, 1, 0);
    taxi(c);
    c.addAtc("10:43:02", "eagle four two one runway 0.9 line up and wait", "LINE UP AND WAIT", chips({"EAGLE-421", "RWY \"0.9\" ?"}), "AMBIGUOUS", false, "2.4 s");
    c.addPilot("10:43:09", "say again, EAGLE-421");
    c.setNotice(L::Caution, t("Runway was not clear. Not counted as your error."), true);
    c.setAlert(L::Caution, t("Suspected speech recognition error"), true);
  } else if (id == "exwrong") {
    steps(1, 1, 1, 0);
    taxi(c);
    c.addAtc("10:43:02", "eagle four two one runway two seven line up and wait", "LINE UP AND WAIT", chips({"EAGLE-421", "RWY 27"}), "RECOGNIZED", true, "2.1 s");
    c.addPilot("10:43:08", "query instruction, EAGLE-421");
    c.setNotice(L::Caution, t("Step not advanced: INCORRECT (runway)."), true);
    c.setAlert(L::Caution, t("Line up: attempt 1 INCORRECT"), true);
  } else if (id == "excs") {
    steps(1, 1, 1, 0);
    taxi(c);
    c.addAtc("10:43:02", "eagle four five one runway zero nine line up and wait", "LINE UP AND WAIT", chips({"EAGLE-451", "RWY 09"}), "RECOGNIZED", true, "2.1 s");
    c.addNote(L::Caution, t("No reply from the pilot"));
    c.setNotice(L::Caution, t("Step not advanced: WRONG CALLSIGN."), true);
    c.setAlert(L::Caution, t("Line up: attempt 1 WRONG CALLSIGN"), true);
  } else if (id == "extime") {
    call(c); taxi(c);
    c.addNote(L::Caution, t("No ATC call for 30 s"));
    c.addPilot("10:42:40", "holding short runway 09, EAGLE-421");
    c.setNotice(L::Caution, t("The pilot is calling again (1 of 2)."));
    c.setWaiting({});
  } else if (id == "exchoice") {
    call(c); taxi(c);
    c.addNote(L::Caution, t("No ATC call for 30 s"));
    c.setWaiting({});
    c.setChoice(t("No ATC call"), t("Line up: 2 pilot calls without an ATC call."));
  } else if (id == "excut") {
    call(c); taxi(c);
    c.setNotice(L::Caution, t("You keyed over the pilot. The readback will be repeated after the answer to you."));
  } else if (id == "exlimit") {
    steps(1, 1, 3, 0);
    taxi(c);
    c.addNote(L::Caution, t("Third \"say again\" in this step"));
    c.addPilot("10:44:20", "say again, EAGLE-421");
    c.setNotice(L::Caution, t("Instructor: speech was not understood 3 times in this step."));
    c.setAlert(L::Caution, t("Instructor attention: say-again limit"));
  } else if (id == "exlost") {
    phase = C::Paused;
    call(c); taxi(c);
    c.setAlert(L::Fault, t("PTT device lost: exercise paused"));
  } else if (id == "exwd") {
    call(c); taxi(c);
    c.setNotice(L::Fault, t("Pilot voice stopped: playback did not finish. The reply is shown as text only."));
    c.setAlert(L::Fault, t("Fault logged: pilot speech watchdog"));
  } else if (id == "exdone") {
    phase = C::Complete;
    steps(3, 1, 1, 1);
    lineUp(c);
    c.addAtc("10:44:31", "eagle four two one runway zero nine cleared for takeoff", "TAKEOFF", chips({"EAGLE-421", "RWY 09"}), "RECOGNIZED", true, "2.1 s");
    c.addPilot("10:44:37", "runway 09 cleared for takeoff, EAGLE-421");
    c.setClearances({cTaxi, cLine, cGo});
    c.setNotice(L::Ok, t("Exercise complete."));
    c.setAlert(L::Ok, t("Exercise complete"));
    c.setWaiting({});
  } else if (id == "exresult" || id == "exresulta") {
    phase = C::Result;
    result(c, id == "exresulta");
    c.setAlert(L::Ok, t("Exercise finished"));
  } else {  // exwait, exblocked, exqueued
    call(c); taxi(c);
  }
  c.setPhase(phase);
  emit c.screenRequested(phase == C::Result ? "ass" : "ex");
}
}  // namespace exercise_mock
