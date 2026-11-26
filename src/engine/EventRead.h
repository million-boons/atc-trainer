#pragma once
#include <string>
#include <vector>

#include "core/IEventLog.h"

namespace atc {

// The other way of EventJson: a JSON Lines log back into its header and events, so that a result of the past is
// built by the same code as a live one (5e, D-335). Never changes the file.
struct ReadLog {
  EventLogHeader header;
  std::vector<EngineEvent> events;
  bool ended = false;       // the log has its "ended" output; otherwise it stopped early (a fault)
  std::string error;        // the file or its header cannot be read
  int unreadable = 0;       // lines that are not events (e.g. a power cut in the last one)
};
ReadLog readEventLog(const std::string& file);
// The header alone (first line), for lists.
bool readEventLogHeader(const std::string& file, EventLogHeader& out, std::string& error);

}  // namespace atc
