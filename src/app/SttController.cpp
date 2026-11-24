#include "app/SttController.h"

#include <QMetaObject>

#include "app/LabController.h"

using atc::SttState;

void SttController::start(std::vector<atc::SttModel> models, atc::SttService::Factory factory,
                          atc::SttService::Describe describe, std::function<long()> freeMb) {
  list_.clear();
  selected_ = -1;
  for (const auto& m : models) {
    if (selected_ < 0 && m.installed) selected_ = int(list_.size());
    list_.append(QVariantMap{{"id", QString::fromStdString(m.id)}, {"name", QString::fromStdString(m.name)},
                             {"note", QString::fromStdString(m.note)}, {"installed", m.installed}});
  }
  atc::SttService::Callbacks cb;
  cb.status = [this](const atc::SttStatus& s) {
    QMetaObject::invokeMethod(this, [this, s] { status_ = s; emit changed(); }, Qt::QueuedConnection);
  };
  cb.result = [this](std::uint64_t tx, const std::string& id, const std::string& config, const atc::SttResult& r, atc::Ns delay) {
    QStringList words;
    QList<double> conf;
    for (const auto& w : r.words) {
      words << QString::fromStdString(w.text);
      conf << w.conf;
    }
    QMetaObject::invokeMethod(this, [this, tx, id, config = QString::fromStdString(config), text = QString::fromStdString(r.text).trimmed(), words, conf, delay] {
      emit result(tx, nameOf(id), config, text, words, conf, double(delay) / 1e9);
    }, Qt::QueuedConnection);
  };
  service_ = std::make_unique<atc::SttService>(std::move(models), std::move(factory), std::move(describe), std::move(cb),
                                               std::move(freeMb));
  emit modelsChanged();
  emit changed();
}

void SttController::setCatalogError(const QString& why) {
  catalogError_ = why;
  emit changed();
}

void SttController::setTransmissionActive(bool on) {
  if (txActive_ == on) return;
  txActive_ = on;
  emit changed();
}

void SttController::showScripted(std::optional<atc::SttStatus> s) {
  scripted_ = std::move(s);
  emit changed();
}

QString SttController::nameOf(const std::string& id) const {
  const QString q = QString::fromStdString(id);
  for (const QVariant& v : list_)
    if (v.toMap()["id"] == q) return v.toMap()["name"].toString();
  return q;
}

QString SttController::selectedName() const {
  return selected_ < 0 ? tr("no model installed") : list_[selected_].toMap()["name"].toString();
}

QString SttController::loadedName() const {
  const auto& s = shown();
  return s.state == SttState::Ready || s.state == SttState::Busy ? nameOf(s.modelId) : QString();
}

QString SttController::stateText() const {
  if (!catalogError_.isEmpty()) return tr("FAILED");
  switch (shown().state) {
    case SttState::NotLoaded: return tr("NOT LOADED");
    case SttState::Loading: return tr("LOADING");
    case SttState::Ready: return tr("READY");
    case SttState::Busy: return tr("BUSY");
    case SttState::Failed: return tr("FAILED");
  }
  return {};
}

int SttController::level() const {
  if (!catalogError_.isEmpty()) return LabController::Fault;
  switch (shown().state) {
    case SttState::Ready:
    case SttState::Busy: return LabController::Ok;
    case SttState::Failed: return LabController::Fault;
    default: return LabController::Caution;
  }
}

QString SttController::message() const {
  return catalogError_.isEmpty() ? QString::fromStdString(shown().message) : catalogError_;
}

bool SttController::installed(const QString& id) const {
  for (const QVariant& v : list_)
    if (v.toMap()["id"].toString() == id) return v.toMap()["installed"].toBool();
  return false;
}

bool SttController::canLoad(const QString& id) const {
  const auto& s = shown();
  if (!service_ || txActive_ || s.locked || !installed(id)) return false;
  if (s.state == SttState::Loading || s.state == SttState::Busy) return false;
  return !(id.toStdString() == s.modelId && s.state == SttState::Ready);
}

bool SttController::loadEnabled() const { return selected_ >= 0 && canLoad(list_[selected_].toMap()["id"].toString()); }

void SttController::select(int row) {
  if (row < 0 || row >= list_.size() || locked() || !list_[row].toMap()["installed"].toBool() || row == selected_) return;
  selected_ = row;
  emit changed();
}

void SttController::load() {
  if (selected_ >= 0) loadModel(list_[selected_].toMap()["id"].toString());
}

void SttController::loadModel(const QString& id) {
  if (!canLoad(id)) return;
  scripted_.reset();
  service_->load(id.toStdString());
  status_ = service_->status();
  emit changed();
}
