#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "scenario/Scenario.h"

namespace atc {

enum class ClipPosition { Initial, Medial, Final };
constexpr std::string_view toString(ClipPosition p) {
  return p == ClipPosition::Initial ? "initial" : p == ClipPosition::Medial ? "medial" : "final";
}

struct PhraseSegment {
  bool dynamic = false;
  std::string text;  // words, or the slot name
  ClipPosition position = ClipPosition::Final;
  bool commaAfter = false;
};

// Splits a reply into literal phrases and slots. Punctuation ends a phrase and becomes a comma
// pause; it is never a segment. First = initial, last = final, a single segment = final.
bool parsePhraseTemplate(const std::string& text, std::vector<PhraseSegment>& out, std::string& error);

// Warnings: digits or pool values typed as words, which should be a {slot}.
std::vector<std::string> lintPhrase(const std::vector<PhraseSegment>& segments, const Airfield& airfield);

// Clip IDs come from the text only, never hand-written: PH_LINE_UP_RUNWAY@initial, RUNWAY_09@final.
// Equal words at an equal position share a clip.
std::string phraseClipId(const std::string& words, ClipPosition p);
std::string valueClipId(const std::string& pool, const std::string& value, ClipPosition p);

// In PilotUtterance::clipIds this stands for a comma pause.
inline constexpr std::string_view kCommaPause = ",";
// Empty, with error set, if the text or a slot cannot be resolved.
std::vector<std::string> clipSequence(const std::string& text, const Scenario& s, const ParamValues& values, std::string& error);

struct RequiredClip {
  std::string id;
  std::string words;        // for a literal phrase
  std::string pool, value;  // for a pool value
  std::string replyId;      // one reply that uses it
};
// Every clip the exercise can ask for, whatever values the seed picks.
bool requiredClips(const Exercise& e, std::map<std::string, RequiredClip>& out, std::vector<std::string>& errors);

}  // namespace atc
