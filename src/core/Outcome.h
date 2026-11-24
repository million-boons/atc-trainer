#pragma once
#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace atc {

// The validator's verdict on one instruction, or Timeout when none came.
// Names are the scenario JSON values (03 section 2).
enum class Outcome {
  Correct, MissingInfo, Ambiguous, Unintelligible, WrongCallsign, Unsafe, Conflict, Unexpected, Incorrect, Timeout
};

inline constexpr std::array<std::pair<Outcome, std::string_view>, 10> kOutcomeNames{{
    {Outcome::Correct, "CORRECT"},
    {Outcome::MissingInfo, "MISSING_INFO"},
    {Outcome::Ambiguous, "AMBIGUOUS"},
    {Outcome::Unintelligible, "UNINTELLIGIBLE"},
    {Outcome::WrongCallsign, "WRONG_CALLSIGN"},
    {Outcome::Unsafe, "UNSAFE"},
    {Outcome::Conflict, "CONFLICT"},
    {Outcome::Unexpected, "UNEXPECTED"},
    {Outcome::Incorrect, "INCORRECT"},
    {Outcome::Timeout, "TIMEOUT"},
}};

constexpr std::string_view toString(Outcome o) {
  for (const auto& [value, name] : kOutcomeNames)
    if (value == o) return name;
  return {};
}

// Empty for anything unknown, so the loader can reject it.
constexpr std::optional<Outcome> outcomeFromString(std::string_view s) {
  for (const auto& [value, name] : kOutcomeNames)
    if (name == s) return value;
  return std::nullopt;
}

}  // namespace atc
