#pragma once
#include <QCoreApplication>
#include <QString>

#include "app/LabController.h"
#include "core/Readiness.h"

// One set of state words and levels for the status-bar chips and the Dashboard tiles, so that
// they cannot disagree. A backend that sets a chip takes the word from here.
namespace status_words {
inline QString tr(const char* s) { return QCoreApplication::translate("StatusWords", s); }

inline QString word(atc::MicReady s) {
  switch (s) {
    case atc::MicReady::Ok: return tr("OK");
    case atc::MicReady::NotTested: return tr("NOT TESTED");
    case atc::MicReady::OutsideBand: return tr("LEVEL");
    case atc::MicReady::NoSignal: return tr("NO SIGNAL");
    case atc::MicReady::Muted: return tr("MUTED");
    case atc::MicReady::Lost: return tr("LOST");
  }
  return {};
}
inline QString word(atc::SpeakerReady s) { return s == atc::SpeakerReady::Ok ? tr("OK") : tr("NOT CHECKED"); }
inline QString word(atc::PttReady s) {
  switch (s) {
    case atc::PttReady::Ok: return tr("OK");
    case atc::PttReady::StandIn: return tr("STAND-IN");
    case atc::PttReady::NoHold: return tr("NO HOLD");
    case atc::PttReady::Missing: return tr("MISSING");
  }
  return {};
}
inline LabController::Level level(atc::StartEffect e) {
  return e == atc::StartEffect::Ready ? LabController::Ok
         : e == atc::StartEffect::Warns ? LabController::Caution : LabController::Fault;
}
}  // namespace status_words
