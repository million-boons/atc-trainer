#pragma once
#include <string>

#include "app/AccessController.h"
#include "app/RecordingsController.h"

// Real and mock backends of the login, the trainee records and the recording store.
AccessBackend realAccess(const std::string& writable);  // people.json and instructor.pin
// Consent texts of the newest mockAccess(): drafts as now; a first approved one (S01 must answer again); a second one
// that changes only the wording (S01 answered the first).
enum class MockConsent { Draft, Approved, Wording };
void setMockConsent(MockConsent c);
AccessBackend mockAccess();                             // S01 both consents, S02 none; PIN 123456
RecordingBackend realRecordings(const std::string& writable, long minFreeMb, RecordingsController& c);
RecordingBackend mockRecordings(RecordingsController& c);  // in memory, descriptions only
