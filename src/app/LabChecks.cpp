#include "app/LabChecks.h"

#include <QCoreApplication>

#include "audio/UtteranceChecks.h"

using atc::TxEnd;
using L = LabController;

namespace {
QString tr(const char* s) { return QCoreApplication::translate("LabChecks", s); }
QVariantMap check(L::Level l, const QString& t, const QString& d) {
  return {{"level", int(l)}, {"title", t}, {"detail", d}};
}
}  // namespace

LabChecks labChecks(std::span<const std::int16_t> audio, int preRollSamples, TxEnd end, bool gap, double micLow,
                    double micHigh) {
  const atc::UtteranceReport r = atc::checkUtterance(audio, preRollSamples);
  const QString peak = tr("peak %1 dBFS").arg(r.peakDbfs, 0, 'f', 1);
  LabChecks c;
  c.noSpeech = r.empty;
  c.list << (r.empty ? check(L::Caution, tr("No speech"), {})
             : r.spokeBeforeKeying ? check(L::Caution, tr("Spoke early"), tr("before keying"))
                                   : check(L::Ok, tr("Start clean"), tr("nothing cut")));
  // A transmission closed by the limit or by unplugging has no meaningful release.
  c.list << (gap ? check(L::Fault, tr("Audio lost"), tr("gap in recording"))
             : end != TxEnd::Normal ? check(L::Caution, tr("Not checked"), tr("no release"))
             : r.empty ? check(L::Caution, tr("No speech"), {})
             : r.releasedEarly ? check(L::Caution, tr("Released early"), tr("speech at cut end"))
                               : check(L::Ok, tr("End clean"), tr("nothing cut")));
  c.list << (r.overload ? check(L::Fault, tr("Too loud"), peak)
             : r.peakDbfs > micHigh ? check(L::Caution, tr("Level high"), peak)
             : r.peakDbfs < micLow ? check(L::Caution, tr("Level low"), peak)
                                   : check(L::Ok, tr("Level good"), peak));
  c.list << (r.keyToSpeechMs ? check(L::Ok, tr("%1 s").arg(*r.keyToSpeechMs / 1e3, 0, 'f', 2), tr("key to speech"))
                             : check(L::Caution, tr("Not measured"), tr("key to speech")));

  if (gap) c.issue = {L::Fault, tr("Audio lost"), tr("Part of the recording is missing. Repeat the transmission.")};
  else if (end != TxEnd::Normal) return c;
  else if (r.overload) c.issue = {L::Fault, tr("Too loud"), tr("Microphone too loud. Speech is distorted; lower the headset volume and repeat.")};
  else if (r.empty) c.issue = {L::Caution, tr("No speech"), tr("Nothing was said during the transmission.")};
  else if (r.spokeBeforeKeying) c.issue = {L::Caution, tr("Spoke early"), tr("Speech began before the pedal was pressed.")};
  else if (r.releasedEarly) c.issue = {L::Caution, tr("Released early"), tr("Pedal released while still speaking. The end of the message may be lost.")};
  return c;
}
