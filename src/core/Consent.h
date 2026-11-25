#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace atc {

// Versions of the two consent texts (D-135), stored with every answer. A text that changes gets a new version, and
// tests/app/test_login.cpp holds text and version together. "draft..." until the unit has approved the text (D-134).
struct ConsentVersions {
  std::string recording = "draft-1";
  std::string pilotVoice = "draft-1";
  // The approved texts that the present one has replaced, oldest first (D-137). A draft is never in here.
  std::vector<std::string> recordingOld{}, pilotVoiceOld{};
  // The instructor's mark of a version, "wider use: yes/no" (D-137); kept with the records. Without a mark: wider.
  std::map<std::string, bool> recordingWider{}, pilotVoiceWider{};
};
// An answer without a version is from before versions were stored.
constexpr bool isDraftConsent(std::string_view version) { return version.empty() || version.starts_with("draft"); }
// What an answer is worth under the present text.
enum class ConsentState {
  Valid,
  Ask,    // wording only: the old answer stays valid, the person is asked again at the next login
  Again   // counts as no until it is given again
};
// A draft answer is valid only until an approved text is in place (D-135). Between approved texts the marks decide
// (D-137): a wider version after the answered one, up to the present one, makes the answer count as no. A version that
// is not known, or has no mark, counts as wider: the safe side.
inline ConsentState consentState(std::string_view answered, std::string_view current, const std::vector<std::string>& old, const std::map<std::string, bool>& wider) {
  if (isDraftConsent(current) || answered == current) return ConsentState::Valid;
  if (isDraftConsent(answered)) return ConsentState::Again;
  auto at = std::find(old.begin(), old.end(), answered);
  if (at == old.end()) return ConsentState::Again;
  const auto isWider = [&](const std::string& v) {
    const auto m = wider.find(v);
    return m == wider.end() || m->second;
  };
  if (isWider(std::string(current)) || std::any_of(at + 1, old.end(), isWider)) return ConsentState::Again;
  return ConsentState::Ask;
}

}  // namespace atc
