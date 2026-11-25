#include "app/AccessController.h"

#include <QDate>
#include <QEvent>
#include <QMetaObject>
#include <algorithm>
#include <utility>

using namespace atc;

AccessController::AccessController(AccessBackend b, std::function<Ns()> clock, QObject* parent)
    : QObject(parent), b_(std::move(b)), clock_(std::move(clock)) {
  if (b_.load) loadError_ = QString::fromStdString(b_.load());
  message_ = loadError_;
  idleTimer_.setInterval(5000);
  connect(&idleTimer_, &QTimer::timeout, this, &AccessController::checkIdle);
  idleTimer_.start();
}

AccessController::~AccessController() {
  if (worker_.joinable()) worker_.join();
}

bool AccessController::consent() const {
  if (!trainee()) return false;
  for (const Person& p : b_.people())
    if (QString::fromStdString(p.id) == id_) return mayRecord(p, versions());
  return false;
}

QString AccessController::shortText() const { return instructor() ? tr("Instructor") : trainee() ? id_ : tr("Log in"); }
QString AccessController::longText() const { return instructor() ? tr("Instructor") : trainee() ? tr("%1  trainee").arg(id_) : tr("Not logged in"); }
QString AccessController::today() const { return QDate::currentDate().toString(Qt::ISODate); }
QString AccessController::draft(const std::string& version) const {
  return isDraftConsent(version) ? " [" + tr("draft — to be approved by the unit") + "]" : QString();
}

QString AccessController::consentText() const {
  return tr("Voice recordings stay on this machine. Consent covers copies to project machines on the local network (offline, never cloud).") + draft(versions().recording);
}

QString AccessController::pilotVoiceText() const {
  return tr("Pilot voice (separate answer): Your recorded phrases may be used as the pilot's voice in training exercises and heard by other trainees. You can withdraw this at any time through the instructor; your phrases are then taken out of use immediately.") + draft(versions().pilotVoice);
}

QVariantList AccessController::people() const {
  QVariantList l;
  if (!b_.people) return l;
  const ConsentVersions v = versions();
  for (const Person& p : b_.people()) {
    QVariantMap m{{"id", QString::fromStdString(p.id)}, {"consent", p.consent}, {"date", QString::fromStdString(p.consentDate)},
                  {"again", recordingAgain(p, v)}, {"ask", recordingState(p, v) == ConsentState::Ask},
                  {"pilotAsk", pilotVoiceState(p, v) == ConsentState::Ask}, {"pilot", p.pilotVoice},
                  {"pilotDate", QString::fromStdString(p.pilotVoiceDate)}, {"pilotAgain", pilotVoiceAgain(p, v)}};
    if (instructor()) {
      m["name"] = QString::fromStdString(p.name);
      m["rank"] = QString::fromStdString(p.rank);
    }
    l.append(m);
  }
  return l;
}

bool AccessController::refuse(const QString& why) {
  message_ = why;
  emit changed();
  return false;
}

bool AccessController::transmitting() {
  if (!onAir()) return false;
  refuse(tr("Finish the transmission first."));
  return true;
}

void AccessController::clearMessage() {
  message_ = loadError_;
  emit changed();
}

void AccessController::checkIdle() {
  if (!instructor()) return;
  const Ns now = clock_();
  if (onAir()) lastInput_ = now;
  if (now - lastInput_ < idleLimit_) return;
  role_ = Role::None;
  message_ = tr("The instructor was logged out: no input for %1 min.").arg(idleLimit_ / ms(60'000));
  emit changed();
}

bool AccessController::eventFilter(QObject* o, QEvent* e) {
  switch (e->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate: lastInput_ = clock_(); break;
    default: break;
  }
  return QObject::eventFilter(o, e);
}

bool AccessController::loginTrainee(const QString& id) {
  if (busy_ || transmitting()) return false;
  const auto all = b_.people ? b_.people() : std::vector<Person>{};
  const bool known = std::any_of(all.begin(), all.end(), [&](const Person& p) { return QString::fromStdString(p.id) == id; });
  if (!known) return refuse(tr("Choose an ID from the list."));
  role_ = Role::Trainee;
  id_ = id;
  later_ = false;
  message_.clear();
  emit changed();
  emit loggedIn();
  return true;
}

void AccessController::loginInstructor(const QString& pin) {
  if (!busy_ && !transmitting()) checkPin(pin, For::Login);
}

void AccessController::confirmInstructor(const QString& pin) {
  if (!busy_) checkPin(pin, For::Confirm);
}

void AccessController::checkPin(const QString& pin, For what) {
  if (!pinSet() || !b_.checkPin) {
    refuse(tr("No instructor PIN is set on this machine."));
    return;
  }
  const Ns left = guard_.left(clock_());
  if (left > 0) {
    refuse(tr("Wait %1 s.").arg((left + ms(999)) / ms(1000)));
    return;
  }
  busy_ = true;
  message_.clear();
  emit changed();
  if (worker_.joinable()) worker_.join();  // finished: its answer has arrived
  worker_ = std::thread([this, what, check = b_.checkPin, p = pin.toStdString()] {
    const bool ok = check(p);
    QMetaObject::invokeMethod(this, [this, ok, what] { pinChecked(ok, what); }, Qt::QueuedConnection);
  });
}

void AccessController::pinChecked(bool ok, For what) {
  busy_ = false;
  if (!ok) {
    const Ns now = clock_();
    guard_.failed(now);
    refuse(guard_.allowed(now) ? tr("Wrong PIN. %1 tries left.").arg(guard_.triesLeft()) : tr("Wrong PIN. Wait %1 s.").arg(guard_.left(now) / ms(1000)));
    return;
  }
  guard_.succeeded();
  if (what == For::Answer) {
    message_.clear();
    const Answer x = std::exchange(answer_, {});
    // Still the same person, and still a question.
    bool open = false;
    for (const QVariant& v : reconsent()) open = open || v.toMap()["kind"].toString() == x.kind;
    if (trainee() && x.id == id_ && open) {
      const std::string id = x.id.toStdString(), day = today().toStdString();
      const auto& store = x.kind == QLatin1String("pilot") ? b_.setPilotVoice : b_.setConsent;
      message_ = QString::fromStdString(store ? store(id, x.yes, day, day) : std::string("the answer cannot be stored"));
    }
    emit changed();
    return;
  }
  if (what == For::Confirm) {  // who is logged in does not change
    message_.clear();
    emit changed();
    emit confirmed();
    return;
  }
  if (transmitting()) return;  // a press during the check
  role_ = Role::Instructor;
  lastInput_ = clock_();
  id_.clear();
  message_.clear();
  emit changed();
  emit loggedIn();
}

QVariantList AccessController::reconsent() const {
  QVariantList l;
  if (!trainee() || later_ || !b_.people) return l;
  const ConsentVersions v = versions();
  const auto all = b_.people();
  for (const Person& p : all) {
    if (QString::fromStdString(p.id) != id_) continue;
    const auto ask = [&](const char* kind, const QString& title, const QString& text, ConsentState st) {
      if (st == ConsentState::Valid) return;
      l.append(QVariantMap{{"kind", kind}, {"title", title}, {"text", text},
                           {"later", st == ConsentState::Ask ? tr("Later: your old answer stays valid. You are asked again at the next login.")
                                                             : tr("Later: it counts as no until you have answered with the instructor present.")}});
    };
    ask("recording", tr("Consent to be recorded"), consentText(), recordingState(p, v));
    ask("pilot", tr("Use as the pilot voice"), pilotVoiceText(), pilotVoiceState(p, v));
  }
  return l;
}

void AccessController::reconsentLater() {
  if (busy_) return;
  later_ = true;
  message_.clear();
  emit changed();
}

void AccessController::answerReconsent(const QString& kind, bool yes, const QString& pin) {
  if (busy_ || !trainee()) return;
  answer_ = {id_, kind, yes};
  checkPin(pin, For::Answer);
}

QVariantList AccessController::consentMarks() const {
  QVariantList l;
  if (!instructor()) return l;
  const ConsentVersions v = versions();
  const auto one = [&](const char* kind, const QString& title, const std::string& now, const std::vector<std::string>& old, const std::map<std::string, bool>& wider) {
    if (isDraftConsent(now) || old.empty()) return;
    const auto m = wider.find(now);
    l.append(QVariantMap{{"kind", kind}, {"title", tr("%1: text \"%2\" has replaced \"%3\"").arg(title, QString::fromStdString(now), QString::fromStdString(old.back()))},
                         {"mark", m == wider.end() ? -1 : m->second ? 1 : 0},
                         {"note", m == wider.end() ? tr("Not marked: counts as wider use, old answers count as no.")
                                  : m->second ? tr("Wider use: old answers count as no until they are given again.")
                                              : tr("Wording only: old answers stay valid, asked again at the next login.")}});
  };
  one("recording", tr("Recording"), v.recording, v.recordingOld, v.recordingWider);
  one("pilot", tr("Pilot voice"), v.pilotVoice, v.pilotVoiceOld, v.pilotVoiceWider);
  return l;
}

bool AccessController::markWider(const QString& kind, bool wider) {
  if (!mayManagePeople(role_) || !b_.setWider) return refuse(tr("Only the instructor may mark a consent text."));
  return done(b_.setWider(kind == QLatin1String("pilot"), wider));
}

bool AccessController::logout() {
  if (busy_ || transmitting()) return false;
  role_ = Role::None;
  id_.clear();
  message_.clear();
  emit changed();
  return true;
}

bool AccessController::done(const std::string& why) {
  message_ = QString::fromStdString(why);
  emit changed();
  return why.empty();
}

bool AccessController::addPerson(const QString& id, const QString& name, const QString& rank, bool consent, const QString& date) {
  if (!mayManagePeople(role_) || !b_.add) return refuse(tr("Only the instructor may add a record."));
  return done(b_.add({id.trimmed().toStdString(), name.trimmed().toStdString(), rank.trimmed().toStdString(), consent, date.trimmed().toStdString()},
                     today().toStdString()));
}

bool AccessController::setPilotVoice(const QString& id, bool consent, const QString& date) {
  if (!mayManagePeople(role_) || !b_.setPilotVoice) return refuse(tr("Only the instructor may change consent."));
  return done(b_.setPilotVoice(id.toStdString(), consent, date.trimmed().toStdString(), today().toStdString()));
}

bool AccessController::setConsent(const QString& id, bool consent, const QString& date) {
  if (!mayManagePeople(role_) || !b_.setConsent) return refuse(tr("Only the instructor may change consent."));
  return done(b_.setConsent(id.toStdString(), consent, date.trimmed().toStdString(), today().toStdString()));
}
