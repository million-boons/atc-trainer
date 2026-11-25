#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/LabController.h"
#include "app/ScenarioBackend.h"

// Scenario Selection (04 section 5d, D-122): what is listed, filtered, opened and selected. Everybody may use it, not
// during an exercise. GUI thread only; nothing here touches PTT. The choice is kept for the session only (C-3).
class ScenarioController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(bool ready READ ready NOTIFY listChanged)
  Q_PROPERTY(QString countText READ countText NOTIFY listChanged)       // "3 of 9"
  Q_PROPERTY(QVariantList categories READ categories NOTIFY listChanged)  // {id, name, off, selected}
  Q_PROPERTY(QString difficulty READ difficulty NOTIFY listChanged)     // "all", basic, intermediate, advanced
  Q_PROPERTY(QVariantList rows READ rows NOTIFY listChanged)            // {id, name, sub, tag, level, state, selected}
  Q_PROPERTY(bool hasDetail READ hasDetail NOTIFY detailChanged)
  Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
  Q_PROPERTY(QVariantList steps READ steps NOTIFY detailChanged)        // {no, name, intent, wait}
  Q_PROPERTY(QStringList objectives READ objectives NOTIFY detailChanged)
  Q_PROPERTY(QStringList errors READ errors NOTIFY detailChanged)
  Q_PROPERTY(QStringList missing READ missing NOTIFY detailChanged)
  Q_PROPERTY(bool canSelect READ canSelect NOTIFY detailChanged)
  Q_PROPERTY(bool instructor MEMBER instructor_ NOTIFY detailChanged)  // set by the shell
  Q_PROPERTY(bool stepsHidden READ stepsHidden NOTIFY detailChanged)   // of a scenario that may be Assessed (C-1)
  Q_PROPERTY(bool trainerBusy MEMBER trainerBusy_ WRITE setTrainerBusy NOTIFY detailChanged)
  Q_PROPERTY(int messageLevel READ messageLevel NOTIFY messageChanged)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
 public:
  explicit ScenarioController(QObject* parent = nullptr) : QObject(parent) {}
  void setBackend(ScenarioBackend b) { b_ = std::move(b); }

  bool ready() const { return loaded_; }
  QString countText() const;
  QVariantList categories() const;
  QString difficulty() const { return difficulty_; }
  QVariantList rows() const;
  bool hasDetail() const { return find(open_) != nullptr; }
  QVariantMap detail() const;
  QVariantList steps() const;
  QStringList objectives() const;
  QStringList errors() const;
  QStringList missing() const;
  bool stepsHidden() const;
  bool canSelect() const;
  void setTrainerBusy(bool on);
  int messageLevel() const { return messageLevel_; }
  QString message() const { return message_; }

  Q_INVOKABLE void enter();                 // the screen opens: read the files again
  Q_INVOKABLE void setCategory(const QString& id);   // "all" or a category of the schema
  Q_INVOKABLE void setDifficulty(const QString& d);
  Q_INVOKABLE void search(const QString& words);
  Q_INVOKABLE void open(const QString& id);  // also starts the coverage check
  Q_INVOKABLE void select();

 signals:
  void listChanged();
  void detailChanged();
  void messageChanged();
  void selected();   // the scenario is on the Dashboard

 private:
  const ScenarioInfo* find(const std::string& id) const;
  bool shown(const ScenarioInfo& s) const;   // by category, difficulty and search
  void check(const std::string& id);
  void say(int level, const QString& text);
  void queued(std::function<void()> f);

  std::vector<ScenarioInfo> all_;
  std::map<std::string, ScenarioCoverage> coverage_;   // of the scenarios that were opened since the files were read
  std::string open_;
  QString category_ = QStringLiteral("all"), difficulty_ = QStringLiteral("all"), search_, message_;
  bool loaded_ = false, trainerBusy_ = false, instructor_ = false, selecting_ = false;
  int messageLevel_ = 0;
  unsigned loadNo_ = 0;
  ScenarioBackend b_;  // last
};
