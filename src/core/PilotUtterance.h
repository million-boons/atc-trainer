#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/Outcome.h"
#include "core/Time.h"

namespace atc {

// One thing the pilot is to say. The engine fills it; the voice bank assembles the clips.
// It becomes a Reply for the TransmissionController, which decides when it may start.
struct PilotUtterance {
  std::uint64_t id = 0;              // unique in the exercise, from 1
  std::string replyId;               // data/phraseology/replies id (D-402)
  std::string text;                  // transcript, slots filled
  std::vector<std::string> clipIds;  // in order, "," for a pause; derived from the reply text, never hand-written
  Ns delay = 0;                      // from the seeded RNG, so a replay gives the same
  bool unprompted = false;           // onEnter call, or the pilot calling again
  Outcome cause = Outcome::Correct;  // what it answers; Timeout when calling again; unused for onEnter
  std::uint64_t tx = 0;              // transmission it answers; 0 if unprompted
};

}  // namespace atc
