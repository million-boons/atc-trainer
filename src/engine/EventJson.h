#pragma once
#include <string>

#include "core/IEventLog.h"

namespace atc {
// One line of the JSON Lines log (D-401), without the line end. Same event, same text.
std::string headerJson(const EventLogHeader& h);
std::string eventJson(const EngineEvent& e);
}  // namespace atc
