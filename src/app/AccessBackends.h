#pragma once
#include <string>

#include "app/AccessController.h"
#include "app/RecordingsController.h"

// Real and mock backends of the login, the trainee records and the recording store.
AccessBackend realAccess(const std::string& writable);  // people.json and instructor.pin
AccessBackend mockAccess();                             // S01 consent, S02 none; PIN 123456
RecordingBackend realRecordings(const std::string& writable, long minFreeMb, RecordingsController& c);
RecordingBackend mockRecordings(RecordingsController& c);  // in memory, descriptions only
