#pragma once
#include <string>
#include <string_view>

namespace atc {

// Versions of the two consent texts (D-135), stored with every answer. A text that changes gets a new version, and
// tests/app/test_login.cpp holds text and version together. "draft..." until the unit has approved the text (D-134).
struct ConsentVersions {
  std::string recording = "draft-1";
  std::string pilotVoice = "draft-1";
};
// An answer without a version is from before versions were stored.
constexpr bool isDraftConsent(std::string_view version) { return version.empty() || version.starts_with("draft"); }
// A draft answer is valid only until an approved text is in place; then the person is asked again once. An answer to
// another approved text counts as no as well, the safe side: the instructor's "wider use: yes/no" (D-137) is not built.
constexpr bool reconsentNeeded(std::string_view answered, std::string_view current) {
  return !isDraftConsent(current) && answered != current;
}

}  // namespace atc
