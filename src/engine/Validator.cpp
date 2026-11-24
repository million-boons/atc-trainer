#include "engine/Validator.h"

#include <algorithm>

namespace atc {
namespace {

std::string valueOf(const ParsedInstruction& p, const std::string& field) {
  const auto it = p.fields.find(field);
  return it == p.fields.end() ? std::string() : it->second.value;
}

std::string expected(const ScenarioContext& ctx, const Expectation& x, const std::string& field) {
  const auto param = x.mustMatch.find(field);
  if (param == x.mustMatch.end()) return {};
  const auto v = ctx.params.find(param->second);
  return v == ctx.params.end() ? std::string() : v->second;
}

}  // namespace

Verdict validate(const ScenarioStep& step, const ScenarioContext& ctx, const ParsedInstruction& p, const TxFacts& facts,
                 const std::string& sttFault, const ValidatorRules& rules) {
  // 1. Was there something usable to judge?
  if (facts.closed && facts.end == TxEnd::DeviceLost) return {Outcome::Unintelligible, {"device lost"}, true};
  if (!sttFault.empty()) return {Outcome::Unintelligible, {"speech recogniser fault"}, true};
  // Held too long: never recognised, so the recogniser is not the suspect.
  if (facts.closed && facts.end == TxEnd::Stuck) return {Outcome::Unintelligible, {"held too long"}, false};
  // A recogniser can invent words from silence.
  if (facts.closed && facts.report.empty) return {Outcome::Unintelligible, {"no speech"}, false};
  std::vector<std::string> clipped;
  if (facts.closed && facts.report.spokeBeforeKeying) clipped.push_back("clipped start");
  if (facts.closed && facts.report.releasedEarly) clipped.push_back("clipped end");
  if (!clipped.empty() && rules.clippedIsUnintelligible) return {Outcome::Unintelligible, clipped, false};
  const bool stt = clipped.empty();  // with clean audio, the recogniser is the suspect
  if (p.cls == RecognitionClass::Unrecognized || p.intent.empty()) return {Outcome::Unintelligible, p.reasons, stt};
  // Released early: the value the transmission ends with fails its shape rules (D-422). Say again, and
  // it is the trainee's release, not the recogniser.
  const bool early = facts.closed && facts.report.releasedEarly;
  std::vector<std::string> uncertain;
  for (const auto& [name, f] : p.fields) {
    if (!f.uncertain) continue;
    if (early && !f.heard.empty() && (p.normalisedText.ends_with(f.heard) || p.rawText.ends_with(f.heard)))
      return {Outcome::Unintelligible, {"clipped end", name}, false};
    uncertain.push_back(name);
  }
  if (p.cls == RecognitionClass::Ambiguous || !uncertain.empty())
    return {Outcome::Ambiguous, uncertain.empty() ? p.reasons : uncertain, stt};
  if (step.expects.empty()) return {Outcome::Unexpected, {p.intent}, false};

  // 2. Callsign. Every expectation of a step names it (checked by the loader).
  const std::string callsign = valueOf(p, "callsign");
  if (callsign.empty()) return {Outcome::MissingInfo, {"callsign"}, false};
  if (callsign != expected(ctx, step.expects.front(), "callsign")) return {Outcome::WrongCallsign, {"callsign"}, false};

  // 3. Safety: no rules yet.

  const Expectation* x = nullptr;
  for (const auto& e : step.expects)
    if (e.intent == p.intent) x = &e;

  // 4. Conflict: the same instruction was already cleared with other values.
  if (!x) {
    for (auto c = ctx.clearances.rbegin(); c != ctx.clearances.rend(); ++c) {
      if (c->intent != p.intent) continue;
      std::vector<std::string> differ;
      for (const auto& [name, value] : c->fields) {
        const std::string now = valueOf(p, name);
        if (!now.empty() && now != value) differ.push_back(name);
      }
      if (!differ.empty()) return {Outcome::Conflict, differ, false};
      break;  // only the latest counts
    }
  }

  // 5. Required parameters.
  std::vector<std::string> missing;
  if (x) {
    for (const auto& f : x->required)
      if (valueOf(p, f).empty()) missing.push_back(f);
  } else {
    missing = p.missing;
  }
  if (!missing.empty() && early) {  // D-422: the cut may have taken it
    missing.insert(missing.begin(), "clipped end");
    return {Outcome::Unintelligible, missing, false};
  }
  if (!missing.empty()) return {Outcome::MissingInfo, missing, false};

  // 6. Expected in this step? A value the step does not list is never accepted (D-417): the pilot
  // would read back less than was said, e.g. without "hold short".
  if (!x) return {Outcome::Unexpected, {p.intent}, false};
  std::vector<std::string> unlisted;
  for (const auto& [name, f] : p.fields)
    if (!f.value.empty() && !x->mustMatch.count(name) && std::find(x->required.begin(), x->required.end(), name) == x->required.end())
      unlisted.push_back(name);
  if (!unlisted.empty()) return {Outcome::Unexpected, unlisted, false};

  // 7. Values.
  std::vector<std::string> wrong;
  for (const auto& [field, param] : x->mustMatch)
    if (valueOf(p, field) != expected(ctx, *x, field)) wrong.push_back(field);
  if (!wrong.empty()) return {Outcome::Incorrect, wrong, false};
  return {Outcome::Correct, {}, false};
}

}  // namespace atc
