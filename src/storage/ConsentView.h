#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "storage/PeopleStore.h"

namespace atc {

// The answers of today for other threads than the one that owns PeopleStore (D-133, D-135).
class ConsentView {
 public:
  // True if an answer, or who may be recorded or used, is not what it was.
  bool set(std::vector<Person> people, ConsentVersions v) {
    std::lock_guard l(m_);
    const auto key = [](const std::vector<Person>& all, const ConsentVersions& versions) {
      std::vector<std::string> k;
      for (const auto& p : all)
        k.push_back(p.id + (mayRecordPilot(p, versions) ? " record" : "") + (mayUsePilotVoice(p, versions) ? " use " : " ") + p.consentDate + " " + p.pilotVoiceDate);
      return k;
    };
    const bool differs = key(people, v) != key(people_, versions_);
    people_ = std::move(people);
    versions_ = std::move(v);
    return differs;
  }
  std::optional<Person> find(const std::string& id) const {
    std::lock_guard l(m_);
    for (const auto& p : people_)
      if (p.id == id) return p;
    return std::nullopt;
  }
  bool mayUse(const std::string& id) const {
    const auto p = find(id);
    std::lock_guard l(m_);
    return p && mayUsePilotVoice(*p, versions_);
  }
  bool mayRecord(const std::string& id) const {
    const auto p = find(id);
    std::lock_guard l(m_);
    return p && mayRecordPilot(*p, versions_);
  }

 private:
  mutable std::mutex m_;
  std::vector<Person> people_;
  ConsentVersions versions_;
};

}  // namespace atc
