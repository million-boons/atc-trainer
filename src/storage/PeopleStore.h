#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atc {

// Trainee record (D-112); a speaker of the recordings (D-101) is the same record.
struct Person {
  std::string id;            // code: 1 to 16 letters, digits, - and _
  std::string name, rank;    // optional; never written into recordings or logs
  bool consent = false;      // to be recorded, on this machine
  std::string consentDate;   // YYYY-MM-DD, the day the answer was given, yes or no
  bool pilotVoice = false;       // separate answer (D-133): heard by other trainees as the pilot voice
  std::string pilotVoiceDate{};  // empty: never asked, which counts as no
};

// May this person be recorded for the pilot voice (D-132, D-133)? Both answers are needed.
inline bool mayRecordPilot(const Person& p) { return p.consent && p.pilotVoice && !p.pilotVoiceDate.empty(); }
// May takes of this person be used in an exercise? Withdrawn: at once no.
inline bool mayUsePilotVoice(const Person& p) { return p.pilotVoice && !p.pilotVoiceDate.empty(); }

bool validPersonId(std::string_view id);
bool validDate(std::string_view d);  // a real day, YYYY-MM-DD

// people.json in the writable folder, readable by the owner only. Not thread-safe. Who may call
// add(), setConsent() and setPilotVoice() is checked by the caller (mayManagePeople). A file that cannot be
// read is reported and never overwritten. Every call returns an empty text or the reason.
class PeopleStore {
 public:
  explicit PeopleStore(std::string file) : file_(std::move(file)) {}
  std::string load();  // a missing file is an empty list
  const std::vector<Person>& people() const { return people_; }
  const Person* find(std::string_view id) const;  // upper and lower case are the same person
  std::string add(Person p, std::string_view today);
  std::string setConsent(std::string_view id, bool consent, const std::string& date, std::string_view today);
  std::string setPilotVoice(std::string_view id, bool consent, const std::string& date, std::string_view today);

 private:
  std::string save(const std::vector<Person>& next);
  std::string file_;
  bool loaded_ = false;
  std::vector<Person> people_;
};

}  // namespace atc
