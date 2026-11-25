#include "storage/PeopleStore.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "storage/PrivateFile.h"

using nlohmann::json;

namespace atc {
namespace {
constexpr std::size_t kMaxText = 60;
bool asciiAlnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
bool sameId(std::string_view a, std::string_view b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) { return lower(x) == lower(y); });
}
std::string badDate(const std::string& d, std::string_view today) {
  if (!validDate(d)) return "the consent date is missing or not a date";
  return d > today ? "the consent date is in the future" : "";
}
}  // namespace

bool validPersonId(std::string_view id) {
  return !id.empty() && id.size() <= 16 && std::all_of(id.begin(), id.end(), [](char c) { return asciiAlnum(c) || c == '-' || c == '_'; });
}

bool validDate(std::string_view d) {
  if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
  const auto num = [&](std::size_t at, std::size_t n, int& out) {
    const char* end = d.data() + at + n;
    return d[at] >= '0' && d[at] <= '9' && std::from_chars(d.data() + at, end, out).ptr == end;
  };
  int y = 0, m = 0, day = 0;
  if (!num(0, 4, y) || !num(5, 2, m) || !num(8, 2, day)) return false;
  return std::chrono::year_month_day(std::chrono::year(y), std::chrono::month(unsigned(m)), std::chrono::day(unsigned(day))).ok();
}

const Person* PeopleStore::find(std::string_view id) const {
  const auto it = std::find_if(people_.begin(), people_.end(), [&](const Person& p) { return sameId(p.id, id); });
  return it == people_.end() ? nullptr : &*it;
}

std::string PeopleStore::load() {
  loaded_ = false;
  people_.clear();
  std::error_code ec;
  if (!std::filesystem::exists(file_, ec) && !ec) {
    loaded_ = true;
    return "";
  }
  std::ifstream i(file_);
  try {
    const json j = json::parse(i);
    if (j.at("format").get<int>() != 1) throw std::runtime_error("format");
    std::vector<Person> next;
    for (const json& x : j.at("people")) {
      Person p;
      x.at("id").get_to(p.id);
      p.name = x.value("name", "");
      p.rank = x.value("rank", "");
      x.at("consent").get_to(p.consent);
      x.at("consent_date").get_to(p.consentDate);
      p.consentVersion = x.value("consent_version", "");
      if (x.contains("pilot_voice") || x.contains("pilot_voice_date")) {
        x.at("pilot_voice").get_to(p.pilotVoice);
        x.at("pilot_voice_date").get_to(p.pilotVoiceDate);
        p.pilotVoiceVersion = x.value("pilot_voice_version", "");
        if (!validDate(p.pilotVoiceDate)) throw std::runtime_error("record");
      }
      if (!validPersonId(p.id) || !validDate(p.consentDate)) throw std::runtime_error("record");
      if (std::any_of(next.begin(), next.end(), [&](const Person& q) { return sameId(q.id, p.id); })) throw std::runtime_error("duplicate");
      next.push_back(std::move(p));
    }
    people_ = std::move(next);
  } catch (const std::exception&) {
    return "the trainee records cannot be read";
  }
  loaded_ = true;
  return "";
}

std::string PeopleStore::save(const std::vector<Person>& next) {
  json list = json::array();
  for (const Person& p : next) {
    json x{{"id", p.id}, {"consent", p.consent}, {"consent_date", p.consentDate}};
    if (!p.consentVersion.empty()) x["consent_version"] = p.consentVersion;
    if (!p.name.empty()) x["name"] = p.name;
    if (!p.rank.empty()) x["rank"] = p.rank;
    if (!p.pilotVoiceDate.empty()) {
      x["pilot_voice"] = p.pilotVoice;
      x["pilot_voice_date"] = p.pilotVoiceDate;
      if (!p.pilotVoiceVersion.empty()) x["pilot_voice_version"] = p.pilotVoiceVersion;
    }
    list.push_back(std::move(x));
  }
  const json j{{"format", 1}, {"people", list}};
  if (!writePrivateFile(file_, j.dump(1, ' ', false, json::error_handler_t::replace) + "\n")) return "the trainee records cannot be written";
  people_ = next;
  return "";
}

std::string PeopleStore::add(Person p, std::string_view today) {
  if (!loaded_) return "the trainee records cannot be read";
  if (!validPersonId(p.id)) return "the ID must be 1 to 16 letters, digits, - or _";
  if (find(p.id)) return "this ID exists already";
  if (p.name.size() > kMaxText || p.rank.size() > kMaxText) return "the name or rank is too long";
  if (auto why = badDate(p.consentDate, today); !why.empty()) return why;
  p.pilotVoice = false;  // its own question, with setPilotVoice()
  p.pilotVoiceDate.clear();
  p.pilotVoiceVersion.clear();
  p.consentVersion = versions_.recording;
  auto next = people_;
  next.push_back(std::move(p));
  return save(next);
}

std::string PeopleStore::setConsent(std::string_view id, bool consent, const std::string& date, std::string_view today) {
  if (!loaded_) return "the trainee records cannot be read";
  if (!find(id)) return "no such ID";
  if (auto why = badDate(date, today); !why.empty()) return why;
  auto next = people_;
  for (Person& p : next)
    if (sameId(p.id, id)) {
      p.consent = consent;
      p.consentDate = date;
      p.consentVersion = versions_.recording;
    }
  return save(next);
}

std::string PeopleStore::setPilotVoice(std::string_view id, bool consent, const std::string& date, std::string_view today) {
  if (!loaded_) return "the trainee records cannot be read";
  if (!find(id)) return "no such ID";
  if (auto why = badDate(date, today); !why.empty()) return why;
  auto next = people_;
  for (Person& p : next)
    if (sameId(p.id, id)) {
      p.pilotVoice = consent;
      p.pilotVoiceDate = date;
      p.pilotVoiceVersion = versions_.pilotVoice;
    }
  return save(next);
}

}  // namespace atc
