#include "app/SetupController.h"

#include <QMetaObject>
#include <algorithm>
#include <utility>

using L = LabController;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString withUnit(int v, const std::string& unit) { return unit.empty() ? QString::number(v) : QStringLiteral("%1 %2").arg(v).arg(q(unit)); }
void add(QVariantMap& m, const std::string& page, QVariantMap row) {
  QVariantList l = m.value(q(page)).toList();
  l.append(row);
  m[q(page)] = l;
}
bool validNewPin(const QString& p) {
  return p.size() >= 6 && p.size() <= 12 && std::all_of(p.begin(), p.end(), [](QChar c) { return c >= '0' && c <= '9'; });
}
}  // namespace

SetupController::SetupController(AccessController* access, QObject* parent) : QObject(parent) { setAccess(access); }

void SetupController::setAccess(AccessController* a) {
  if (access_) access_->disconnect(this);
  access_ = a;
  if (a) connect(a, &AccessController::changed, this, &SetupController::accessChanged);
  accessChanged();
}

void SetupController::accessChanged() {
  if (std::exchange(wasInstructor_, instructor()) && !instructor()) {
    stopLearn();
    clearMessage();
  }
  emit changed();
}

void SetupController::setTrainerBusy(bool on) {
  if (trainerBusy_ == on) return;
  trainerBusy_ = on;
  if (on) stopLearn();
  emit changed();
}

void SetupController::queued(std::function<void()> f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }

bool SetupController::say(int level, const QString& text) {
  messageLevel_ = text.isEmpty() ? int(L::None) : level;
  message_ = text;
  emit messageChanged();
  return false;
}

SetupBackend::Done SetupController::answer(std::function<void()> ok, const QString& doneText) {
  ++waiting_;
  emit changed();
  return [this, ok = std::move(ok), doneText](const std::string& why) {
    queued([this, ok, doneText, why] {
      --waiting_;
      if (!why.empty()) say(L::Caution, q(why));
      else {
        say(L::Ok, doneText);
        if (ok) ok();
      }
      emit changed();
    });
  };
}

bool SetupController::may(bool everybody) {
  if (!everybody && !instructor()) return say(L::Caution, tr("Only the instructor may change this."));
  if (trainerBusy_) return say(L::Caution, tr("Not during an exercise."));
  if (busy()) return say(L::Caution, tr("Wait for the last request."));
  return true;
}

void SetupController::enter() {
  clearMessage();
  reload();
}

void SetupController::leave() { stopLearn(); }

void SetupController::reload() {
  if (!b_.load) return;
  const unsigned n = ++loadNo_;
  b_.load([this, n](SetupContent c, const std::string& why) {
    queued([this, n, c = std::move(c), why] {
      if (n != loadNo_) return;
      loaded_ = why.empty();
      if (loaded_) content_ = c;
      else say(L::Fault, q(why));
      emit changed();
    });
  });
}

const SetupChoice* SetupController::choiceOf(const QString& key) const {
  for (const SetupChoice& c : content_.choices)
    if (q(c.key) == key) return &c;
  return nullptr;
}

QString SetupController::value(const char* key) const {
  const SetupChoice* c = choiceOf(QLatin1String(key));
  return c ? q(c->value) : QString();
}

QVariantMap SetupController::numbers() const {
  QVariantMap m;
  for (const SetupNumber& n : content_.numbers)
    add(m, n.page, {{"key", q(n.key)}, {"title", q(n.title)}, {"text", withUnit(n.value, n.unit)}, {"byUser", n.byUser}, {"applies", q(n.applies)},
                    {"range", tr("%1 to %2").arg(withUnit(n.low, {}), withUnit(n.high, n.unit))}});
  return m;
}

QVariantMap SetupController::choices() const {
  QVariantMap m;
  for (const SetupChoice& c : content_.choices) {
    QVariantList options;
    QString text = q(c.value);
    for (const SetupOption& o : c.options) {
      options.append(QVariantMap{{"id", q(o.id)}, {"text", q(o.text)}, {"note", q(o.note)}, {"off", o.off}});
      if (o.id == c.value) text = q(o.text);
    }
    add(m, c.page, {{"key", q(c.key)}, {"title", q(c.title)}, {"text", text}, {"value", q(c.value)}, {"byUser", c.byUser}, {"applies", q(c.applies)},
                    {"may", c.everybody || instructor()}, {"options", options}});
  }
  return m;
}

QVariantMap SetupController::facts() const {
  QVariantMap m;
  for (const SetupFact& f : content_.facts) add(m, f.page, {{"name", q(f.name)}, {"value", q(f.value)}});
  return m;
}

void SetupController::stepNumber(const QString& key, int steps) {
  if (!may(false) || !b_.setNumber) return;
  for (const SetupNumber& n : content_.numbers) {
    if (q(n.key) != key) continue;
    const int v = n.value + steps * n.step;
    if (v < n.low || v > n.high) return void(say(L::Caution, tr("%1: %2 to %3.").arg(q(n.title), withUnit(n.low, {}), withUnit(n.high, n.unit))));
    return b_.setNumber(n.key, v, answer([this] { reload(); }, n.applies.empty() ? tr("Stored.") : tr("Stored. Applies: %1.").arg(q(n.applies))));
  }
}

void SetupController::choose(const QString& key, const QString& id) {
  const SetupChoice* c = choiceOf(key);
  if (!c || q(c->value) == id || !may(c->everybody) || !b_.setChoice) return;
  const auto o = std::find_if(c->options.begin(), c->options.end(), [&](const SetupOption& x) { return q(x.id) == id; });
  if (o == c->options.end() || o->off) return;
  b_.setChoice(c->key, o->id, answer([this, key, id] {
                 if (key == QLatin1String("theme")) emit themeChosen(id);
                 if (key == QLatin1String("motion")) emit motionChosen(id == QLatin1String("reduce"));
                 reload();
               }, c->applies.empty() ? tr("Stored.") : tr("Stored. Applies: %1.").arg(q(c->applies))));
}

void SetupController::restore(const QString& page) {
  if (!may(false) || !b_.restore) return;
  b_.restore(page.toStdString(), answer([this] { reload(); }, tr("The starting values are back.")));
}

// ---- learning a pedal

void SetupController::startLearn() {
  if (learning_ || !may(false) || !b_.startLearn) return;
  const unsigned n = ++learnNo_;
  learning_ = true;
  presses_.clear();
  longest_.clear();
  clearMessage();
  emit learnChanged();
  b_.startLearn(
      [this, n](std::vector<SetupPress> p) {
        queued([this, n, p = std::move(p)] {
          if (n != learnNo_ || !learning_) return;
          presses_ = p;
          for (const SetupPress& x : p) {
            const auto at = std::find_if(longest_.begin(), longest_.end(), [&](const auto& y) { return y.first == x.id; });
            if (at == longest_.end()) longest_.emplace_back(x.id, x.heldMs);
            else at->second = std::max(at->second, x.heldMs);
          }
          emit learnChanged();
        });
      },
      [this, n](const std::string& why) {
        queued([this, n, why] {
          if (n != learnNo_ || why.empty()) return;
          learning_ = false;
          say(L::Caution, q(why));
          emit learnChanged();
        });
      });
}

void SetupController::stopLearn() {
  if (!learning_) return;
  ++learnNo_;
  learning_ = false;
  if (b_.stopLearn) b_.stopLearn();
  emit learnChanged();
}

QVariantList SetupController::presses() const {
  QVariantList l;
  for (const SetupPress& p : presses_) {
    const auto at = std::find_if(longest_.begin(), longest_.end(), [&](const auto& y) { return y.first == p.id; });
    const int held = at == longest_.end() ? p.heldMs : at->second;
    const bool held2 = held >= kHold, usable = p.refused.empty() && held2 && !p.down;
    const QString state = !p.refused.empty() ? q(p.refused) : usable ? tr("held and released: can be taken") : held2 ? tr("release it now")
                          : p.down ? tr("DOWN, hold it for 2 s") : tr("pressed too short: hold it for 2 s");
    l.append(QVariantMap{{"id", q(p.id)}, {"name", q(p.name)}, {"info", q(p.info)}, {"state", state}, {"usable", usable},
                         {"level", int(!p.refused.empty() ? L::Fault : usable ? L::Ok : L::Caution)}});
  }
  return l;
}

void SetupController::takePedal(const QString& id) {
  if (!learning_) return;
  for (const QVariant& v : presses())
    if (v.toMap()["id"].toString() == id && v.toMap()["usable"].toBool()) {
      stopLearn();
      if (!may(false) || !b_.setChoice) return;
      return b_.setChoice("ptt", id.toStdString(), answer([this] { reload(); }, tr("PTT device stored. Applies: next start of the program.")));
    }
  say(L::Caution, tr("Hold the pedal for 2 s and release it first."));
}

// ---- the rest

void SetupController::changePin(const QString& oldPin, const QString& newPin, const QString& again) {
  if (!may(false) || !b_.changePin) return;
  if (!validNewPin(newPin)) return void(say(L::Caution, tr("The new PIN must be 6 to 12 digits.")));
  if (newPin != again) return void(say(L::Caution, tr("The two entries of the new PIN differ.")));
  b_.changePin(oldPin.toStdString(), newPin.toStdString(), answer([this] { emit pinChanged(); }, tr("The PIN was changed.")));
}

void SetupController::testSentence() {
  if (may(false) && b_.testSentence) b_.testSentence(answer({}, tr("Test sentence played.")));
}

void SetupController::deleteGuests(int expected) {
  if (!may(false) || !b_.deleteGuests) return;
  if (expected <= 0 || expected != content_.guests) return void(say(L::Caution, tr("The number of guest results has changed: %1. Nothing was deleted.").arg(content_.guests)));
  ++waiting_;
  emit changed();
  b_.deleteGuests(expected, [this, expected](int count, const std::string& why) {
    queued([this, expected, count, why] {
      --waiting_;
      content_.guests = count;
      if (why.empty()) say(L::Ok, tr("%1 guest results deleted. Each deletion is in the log.").arg(expected));
      else say(L::Caution, q(why));
      emit changed();
    });
  });
}
