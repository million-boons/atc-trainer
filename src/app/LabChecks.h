#pragma once
#include <QString>
#include <QVariantList>
#include <cstdint>
#include <optional>
#include <span>

#include "app/LabController.h"
#include "audio/UtteranceChecks.h"
#include "core/TransmissionController.h"

// The four checks shown for a recording: start, end, level, key to speech.
struct LabChecks {
  QVariantList list;
  bool noSpeech = false;
  atc::UtteranceReport report;
  struct Issue { LabController::Level level; QString title, notice; };
  std::optional<Issue> issue;  // the most important one
};
// audio = pre-roll + keyed speech + tail. preRollSamples 0: a queued transmission, which has
// no audio from before TX opened (D-302). gap: audio was lost inside the cut.
LabChecks labChecks(std::span<const std::int16_t> audio, int preRollSamples, atc::TxEnd end, bool gap, double micLow,
                    double micHigh);
