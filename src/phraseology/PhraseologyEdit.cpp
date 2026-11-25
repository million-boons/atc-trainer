#include "phraseology/PhraseologyEdit.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>

namespace fs = std::filesystem;

namespace atc {
namespace {
using json = nlohmann::ordered_json;

std::optional<std::string> slurp(const std::string& file) {
  std::ifstream f(file, std::ios::binary);
  if (!f) return std::nullopt;
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string text(const json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool validatedIn(const json& j) {
  const auto it = j.find("validation");
  return it != j.end() && it->is_object() && text(*it, "status") == "validated";
}

const char* listOf(bool reply) { return reply ? "replies" : "intents"; }

std::vector<std::string> textsOf(const json& x, bool reply) {
  if (reply) return {text(x, "text")};
  std::vector<std::string> out;
  if (const auto e = x.find("examples"); e != x.end() && e->is_array())
    for (const auto& t : *e)
      if (t.is_string()) out.push_back(t.get<std::string>());
  return out;
}

PhraseEntry entryOf(const json& x, bool reply, const json& file, const std::string& name) {
  PhraseEntry e;
  e.id = text(x, "id");
  e.reply = reply;
  e.texts = textsOf(x, reply);
  e.validated = validatedIn(x);
  e.placeholderFile = !validatedIn(file);
  if (const auto v = x.find("validation"); v != x.end() && v->is_object()) {
    e.sopRef = text(*v, "sopRef");
    e.validatedBy = text(*v, "validated_by");
    e.validatedAt = text(*v, "validated_at");
  }
  e.file = name;
  return e;
}

// Spaces before the first key.
int indentOf(const std::string& bytes) {
  const auto nl = bytes.find('\n');
  if (nl == std::string::npos) return -1;
  int n = 0;
  while (nl + 1 + std::size_t(n) < bytes.size() && bytes[nl + 1 + std::size_t(n)] == ' ') ++n;
  return n ? n : -1;
}

std::string dump(const json& j, const std::string& like) {
  return j.dump(indentOf(like), ' ', false) + (!like.empty() && like.back() == '\n' ? "\n" : "");
}

// Empty, or what is wrong with the words themselves.
std::string badText(const std::string& t) {
  if (t.empty() || t.find_first_not_of(' ') == std::string::npos) return "the text is empty";
  if (t.size() > 300) return "the text is too long (300 characters)";
  if (std::any_of(t.begin(), t.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })) return "the text has a character that cannot be printed";
  try {
    (void)nlohmann::json(t).dump();
  } catch (const nlohmann::json::exception&) {
    return "the text is not valid UTF-8";
  }
  return "";
}

bool open(const std::string& bytes, bool reply, json& j, std::string& why) {
  j = json::parse(bytes, nullptr, false);
  if (!j.is_object() || !j.contains(listOf(reply)) || !j[listOf(reply)].is_array()) {
    why = std::string("the file has no list of ") + listOf(reply);
    return false;
  }
  return true;
}
}  // namespace

const PhraseEntry* PhraseSet::find(const std::string& id, bool reply) const {
  for (const auto& e : entries)
    if (e.reply == reply && e.id == id) return &e;
  return nullptr;
}

std::vector<std::string> phraseologyFiles(const std::string& dir) {
  std::vector<std::string> out;
  for (const char* sub : {"", "replies"}) {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(dir) / sub, ec), end; !ec && it != end; it.increment(ec))
      if (it->path().extension() == ".json") names.push_back((fs::path(sub) / it->path().filename()).string());
    std::sort(names.begin(), names.end());
    out.insert(out.end(), names.begin(), names.end());
  }
  return out;
}

bool readPhraseology(const std::string& dir, PhraseSet& out, std::string& error) {
  PhraseSet s;
  std::vector<PhraseEntry> intents;
  std::uint64_t hash = 14695981039346656037ull;
  const auto add = [&hash](const std::string& b) {
    for (const unsigned char c : b) hash = (hash ^ c) * 1099511628211ull;
    hash = (hash ^ 0xff) * 1099511628211ull;  // "a" + "bc" is not "ab" + "c"
  };
  const auto files = phraseologyFiles(dir);
  if (files.empty()) {
    error = dir + ": no phraseology files";
    return false;
  }
  for (const auto& name : files) {
    const std::string path = (fs::path(dir) / name).string();
    const auto bytes = slurp(path);
    if (!bytes) {
      error = path + ": cannot read";
      return false;
    }
    add(name);
    add(*bytes);
    const bool reply = name.starts_with("replies/");
    json j;
    std::string why;
    if (!open(*bytes, reply, j, why)) {
      error = path + ": " + why;
      return false;
    }
    for (const auto& x : j[listOf(reply)])
      if (x.is_object()) (reply ? s.entries : intents).push_back(entryOf(x, reply, j, name));
  }
  s.entries.insert(s.entries.end(), intents.begin(), intents.end());
  char hex[17];
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(hash));
  s.revision = hex;
  out = std::move(s);
  return true;
}

std::string changeEntry(const std::string& bytes, const PhraseEntry& w, const PhraseSave& s, std::string& out) {
  json j;
  std::string why;
  if (!open(bytes, w.reply, j, why)) return why;
  if (w.texts.empty()) return w.reply ? "the text is empty" : "at least one example is needed";
  if (w.reply && w.texts.size() != 1) return "a reply has one text";
  for (const auto& t : w.texts)
    if (std::string bad = badText(t); !bad.empty()) return bad;
  for (auto& x : j[listOf(w.reply)]) {
    if (!x.is_object() || text(x, "id") != w.id) continue;
    const PhraseEntry was = entryOf(x, w.reply, j, w.file);
    const bool changed = was.texts != w.texts;
    if (changed && w.reply) x["text"] = w.texts[0];
    if (changed && !w.reply) x["examples"] = w.texts;
    json v = json::object();
    if (w.validated && !changed) {
      if (was.placeholderFile) return "the file is marked PLACEHOLDER: nothing in it can be Validated";
      if (w.sopRef.empty()) return "Validated needs an SOP reference";
      const bool same = was.validated && was.sopRef == w.sopRef;
      v["status"] = "validated";
      v["sopRef"] = w.sopRef;
      v["validated_by"] = same ? was.validatedBy : s.by;
      v["validated_at"] = same ? was.validatedAt : s.today;
    } else {
      v["status"] = "draft";
      if (!w.sopRef.empty()) v["sopRef"] = w.sopRef;
    }
    if (!w.sopRef.empty())
      if (std::string bad = badText(w.sopRef); !bad.empty()) return "SOP reference: " + bad;
    x["validation"] = std::move(v);
    out = dump(j, bytes);
    return "";
  }
  return w.id + " is not in " + w.file;
}

std::string appendReply(const std::string& bytes, const PhraseEntry& a, std::string& out) {
  json j;
  std::string why;
  if (!open(bytes, true, j, why)) return why;
  if (a.id.empty() || a.id.size() > 60 || a.id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
    return "the name may have capital letters, digits, - and _ only";
  if (a.texts.size() != 1) return "a reply has one text";
  if (std::string bad = badText(a.texts[0]); !bad.empty()) return bad;
  for (const auto& x : j["replies"])
    if (x.is_object() && text(x, "id") == a.id) return a.id + " exists already";
  j["replies"].push_back(json{{"id", a.id}, {"text", a.texts[0]}, {"validation", json{{"status", "draft"}}}});
  out = dump(j, bytes);
  return "";
}

std::string describeChanges(const PhraseSet& installed, const PhraseSet& copy) {
  const auto side = [](const PhraseEntry& e) {
    json v{{"texts", e.texts}, {"status", e.validated ? "validated" : "draft"}};
    if (!e.sopRef.empty()) v["sopRef"] = e.sopRef;
    if (e.validated) {
      v["validated_by"] = e.validatedBy;
      v["validated_at"] = e.validatedAt;
    }
    return v;
  };
  json list = json::array();
  for (const auto& e : copy.entries) {
    const PhraseEntry* was = installed.find(e.id, e.reply);
    if (was && side(*was) == side(e)) continue;
    json x{{"file", e.file}, {"id", e.id}, {"kind", e.reply ? "reply" : "intent"}};
    if (was) x["installed"] = side(*was);
    x["changed"] = side(e);
    list.push_back(std::move(x));
  }
  for (const auto& e : installed.entries)
    if (!copy.find(e.id, e.reply)) list.push_back(json{{"file", e.file}, {"id", e.id}, {"kind", e.reply ? "reply" : "intent"}, {"installed", side(e)}, {"missingInCopy", true}});
  return json{{"format", 1}, {"kind", "atc-phraseology-changes"}, {"installedRevision", installed.revision}, {"changes", list}}.dump(1, ' ', false, json::error_handler_t::replace) + "\n";
}

}  // namespace atc
