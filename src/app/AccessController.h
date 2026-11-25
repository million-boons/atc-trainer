#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "app/LabController.h"
#include "core/Access.h"
#include "storage/PeopleStore.h"

// What the login needs from outside. Texts: empty, or the reason of a refusal.
struct AccessBackend {
  std::function<std::string()> load;
  std::function<std::vector<atc::Person>()> people;
  std::function<std::string(atc::Person, const std::string& today)> add;
  std::function<std::string(const std::string& id, bool consent, const std::string& date, const std::string& today)> setConsent;
  std::function<bool()> pinSet;
  std::function<bool(const std::string& pin)> checkPin;  // slow: called on a worker thread
};

// Local login and trainee records (04 section 5b, D-111, D-112, D-118). Nobody is logged in at start.
// GUI thread only. Logging in or out never touches a transmission; it is refused while one is open.
class AccessController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(bool trainee READ trainee NOTIFY changed)
  Q_PROPERTY(bool instructor READ instructor NOTIFY changed)
  Q_PROPERTY(QString personId READ personId NOTIFY changed)
  Q_PROPERTY(QString shortText READ shortText NOTIFY changed)  // status bar
  Q_PROPERTY(QString longText READ longText NOTIFY changed)
  Q_PROPERTY(QVariantList people READ people NOTIFY changed)   // {id, name, rank, consent, date}; names only for the instructor
  Q_PROPERTY(bool pinSet READ pinSet NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)               // the PIN is being checked
  Q_PROPERTY(QString message READ message NOTIFY changed)      // why the last request was refused
  Q_PROPERTY(QString today READ today NOTIFY changed)
  Q_PROPERTY(QString consentText READ consentText CONSTANT)    // D-114
 public:
  explicit AccessController(AccessBackend b, std::function<atc::Ns()> clock = atc::monotonicNow, QObject* parent = nullptr);
  ~AccessController() override;
  void setLab(LabController* lab) { lab_ = lab; }

  atc::Role role() const { return role_; }
  bool consent() const;  // of the trainee who is logged in
  bool trainee() const { return role_ == atc::Role::Trainee; }
  bool instructor() const { return role_ == atc::Role::Instructor; }
  QString personId() const { return id_; }
  QString shortText() const;
  QString longText() const;
  QVariantList people() const;
  bool pinSet() const { return b_.pinSet && b_.pinSet(); }
  bool busy() const { return busy_; }
  QString message() const { return message_; }
  QString today() const;
  QString consentText() const;

  // An instructor without input for this long is logged out (D-120). Input is any key, touch or mouse event of
  // the application (install this object as its event filter) and any transmission.
  void setIdleLimit(atc::Ns limit) { idleLimit_ = limit; }
  void checkIdle();  // also every 5 s
  bool eventFilter(QObject* o, QEvent* e) override;

  Q_INVOKABLE bool loginTrainee(const QString& id);
  Q_INVOKABLE void loginInstructor(const QString& pin);  // answer: changed(), loggedIn()
  // The PIN without a login, e.g. to end an assessed exercise (D-125): confirmed(). Also during a transmission.
  Q_INVOKABLE void confirmInstructor(const QString& pin);
  Q_INVOKABLE bool logout();
  Q_INVOKABLE void clearMessage();
  // Instructor only. False: message says why.
  Q_INVOKABLE bool addPerson(const QString& id, const QString& name, const QString& rank, bool consent, const QString& date);
  Q_INVOKABLE bool setConsent(const QString& id, bool consent, const QString& date);

 signals:
  void changed();
  void loggedIn();
  void confirmed();

 private:
  bool refuse(const QString& why);
  bool onAir() const { return lab_ && (lab_->txTone() == LabController::Active || lab_->txTone() == LabController::Busy); }
  bool transmitting();  // true: refused, with the reason
  bool done(const std::string& why);
  void checkPin(const QString& pin, bool login);
  void pinChecked(bool ok, bool login);

  AccessBackend b_;
  std::function<atc::Ns()> clock_;
  QPointer<LabController> lab_;
  atc::Role role_ = atc::Role::None;
  QString id_, message_, loadError_;
  bool busy_ = false;
  atc::Ns idleLimit_ = atc::ms(600'000), lastInput_ = 0;
  QTimer idleTimer_;
  atc::LoginGuard guard_;
  std::thread worker_;
};
