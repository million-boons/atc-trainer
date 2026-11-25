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
    if (QString::fromStdString(p.id) == id_) return p.consent;
  return false;
}

QString AccessController::shortText() const { return instructor() ? tr("Instructor") : trainee() ? id_ : tr("Log in"); }
QString AccessController::longText() const { return instructor() ? tr("Instructor") : trainee() ? tr("%1  trainee").arg(id_) : tr("Not logged in"); }
QString AccessController::today() const { return QDate::currentDate().toString(Qt::ISODate); }
QString AccessController::consentText() const {
  return tr("Voice recordings stay on this machine. Consent covers copies to project machines on the local network (offline, never cloud).");
}

QString AccessController::pilotVoiceText() const {
  return tr("Separate answer: recorded pilot phrases are used as the pilot voice and heard by other trainees. If the answer becomes no, they are taken out of use at once.");
}

QVariantList AccessController::people() const {
  QVariantList l;
  if (!b_.people) return l;
  for (const Person& p : b_.people()) {
    QVariantMap m{{"id", QString::fromStdString(p.id)}, {"consent", p.consent}, {"date", QString::fromStdString(p.consentDate)},
                  {"pilot", p.pilotVoice}, {"pilotDate", QString::fromStdString(p.pilotVoiceDate)}};
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
  message_.clear();
  emit changed();
  emit loggedIn();
  return true;
}

void AccessController::loginInstructor(const QString& pin) {
  if (!busy_ && !transmitting()) checkPin(pin, true);
}

void AccessController::confirmInstructor(const QString& pin) {
  if (!busy_) checkPin(pin, false);
}

void AccessController::checkPin(const QString& pin, bool login) {
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
  worker_ = std::thread([this, login, check = b_.checkPin, p = pin.toStdString()] {
    const bool ok = check(p);
    QMetaObject::invokeMethod(this, [this, ok, login] { pinChecked(ok, login); }, Qt::QueuedConnection);
  });
}

void AccessController::pinChecked(bool ok, bool login) {
  busy_ = false;
  if (!ok) {
    const Ns now = clock_();
    guard_.failed(now);
    refuse(guard_.allowed(now) ? tr("Wrong PIN. %1 tries left.").arg(guard_.triesLeft()) : tr("Wrong PIN. Wait %1 s.").arg(guard_.left(now) / ms(1000)));
    return;
  }
  guard_.succeeded();
  if (!login) {  // who is logged in does not change
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
