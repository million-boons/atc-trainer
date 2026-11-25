#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Consent.h"

namespace atc {

// Trainee record (D-112); a speaker of the recordings (D-101) is the same record.
struct Person {
  std::string id;            // code: 1 to 16 letters, digits, - and _
  std::string name, rank;    // optional; never written into recordings or logs
  bool consent = false;      // to be recorded, on this machine
  std::string consentDate;   // YYYY-MM-DD, the day the answer was given, yes or no
  bool pilotVoice = false;       // separate answer (D-133): heard by other trainees as the pilot voice
  std::string pilotVoiceDate{};  // empty: never asked, which counts as no
  std::string consentVersion{}, pilotVoiceVersion{};  // of the text that was answered (D-135); set by the store
};

inline ConsentState recordingState(const Person& p, const ConsentVersions& v) { return consentState(p.consentVersion, v.recording, v.recordingOld, v.recordingWider); }
// Never asked is not "again": it counts as no anyway.
inline ConsentState pilotVoiceState(const Person& p, const ConsentVersions& v) {
  return p.pilotVoiceDate.empty() ? ConsentState::Valid : consentState(p.pilotVoiceVersion, v.pilotVoice, v.pilotVoiceOld, v.pilotVoiceWider);
}
inline bool recordingAgain(const Person& p, const ConsentVersions& v) { return recordingState(p, v) == ConsentState::Again; }
inline bool pilotVoiceAgain(const Person& p, const ConsentVersions& v) { return pilotVoiceState(p, v) == ConsentState::Again; }
inline bool mayRecord(const Person& p, const ConsentVersions& v) { return p.consent && !recordingAgain(p, v); }
// May takes of this person be used in an exercise? Withdrawn: at once no.
inline bool mayUsePilotVoice(const Person& p, const ConsentVersions& v) { return p.pilotVoice && !p.pilotVoiceDate.empty() && !pilotVoiceAgain(p, v); }
// For the pilot voice both answers are needed (D-132, D-133).
inline bool mayRecordPilot(const Person& p, const ConsentVersions& v) { return mayRecord(p, v) && mayUsePilotVoice(p, v); }

bool validPersonId(std::string_view id);
bool validDate(std::string_view d);  // a real day, YYYY-MM-DD

// people.json in the writable folder, readable by the owner only. Not thread-safe. Who may call
// add(), setConsent(), setPilotVoice() and setWider() is checked by the caller (mayManagePeople). A file that cannot be
// read is reported and never overwritten. Every call returns an empty text or the reason.
class PeopleStore {
 public:
  explicit PeopleStore(std::string file, ConsentVersions v = {}) : file_(std::move(file)), versions_(std::move(v)) {}
  std::string load();  // a missing file is an empty list
  const std::vector<Person>& people() const { return people_; }
  const Person* find(std::string_view id) const;  // upper and lower case are the same person
  std::string add(Person p, std::string_view today);
  std::string setConsent(std::string_view id, bool consent, const std::string& date, std::string_view today);
  std::string setPilotVoice(std::string_view id, bool consent, const std::string& date, std::string_view today);
  // "Wider use: yes/no" of the present text, which must have replaced an approved one. From "wider" there is no way
  // back: answers that count as no must not become valid again (assumed).
  std::string setWider(bool pilotVoice, bool wider);
  const ConsentVersions& versions() const { return versions_; }  // with the marks that were stored

 private:
  std::string save(const std::vector<Person>& next);
  std::string file_;
  ConsentVersions versions_;  // stored with each new answer; the marks come from the file
  const ConsentVersions given_{versions_};
  bool loaded_ = false;
  std::vector<Person> people_;
};

}  // namespace atc
