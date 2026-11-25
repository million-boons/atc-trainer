#pragma once
#include "app/HistoryBackend.h"

// Invented results in memory, around the time given: PLACEHOLDER. Also two guest results, which no screen may show.
HistoryBackend mockHistory(std::int64_t nowMs);
