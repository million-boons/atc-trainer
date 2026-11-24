#include "stt/SttCatalog.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace atc {

bool loadSttCatalog(const std::string& file, const std::string& root, std::vector<SttModel>& out, std::string& error,
                    const std::string& modelsRoot) {
  std::ifstream f(file);
  if (!f) {
    error = "cannot read " + file;
    return false;
  }
  const auto j = nlohmann::json::parse(f, nullptr, false);
  if (!j.is_object() || !j.contains("models") || !j["models"].is_array()) {
    error = file + ": not a valid model list";
    return false;
  }
  const auto under = [](const std::string& base, const std::string& p) {
    return p.empty() ? p : (std::filesystem::path(base) / p).lexically_normal().string();
  };
  const auto abs = [&](const std::string& p) { return under(root, p); };
  std::vector<SttModel> models;
  std::set<std::string> ids;
  for (const auto& m : j["models"]) {
    SttModel s;
    try {
      s.id = m.at("id").get<std::string>();
      s.name = m.at("name").get<std::string>();
      s.engine = m.at("engine").get<std::string>();
      s.path = under(modelsRoot.empty() ? root : modelsRoot, m.at("path").get<std::string>());
      s.grammarFile = abs(m.value("grammarFile", ""));
      s.promptFile = abs(m.value("promptFile", ""));
      s.note = m.value("note", "");
      s.threads = m.value("threads", 4);
      s.audioCtx = m.value("audioCtx", 0);
      s.peakMb = m.value("peakMb", 0);
    } catch (const nlohmann::json::exception& e) {
      error = "model " + (s.id.empty() ? std::to_string(models.size() + 1) : s.id) + ": " + e.what();
      return false;
    }
    const auto bad = [&](const std::string& why) {
      error = "model " + s.id + ": " + why;
      return false;
    };
    if (s.id.empty() || s.name.empty() || m["path"].get<std::string>().empty()) return bad("id, name and path must not be empty");
    if (s.engine != "vosk" && s.engine != "whisper") return bad("unknown engine " + s.engine);
    if (s.threads < 1 || s.threads > 16) return bad("threads must be 1 to 16");
    if (s.audioCtx < 0 || s.audioCtx > 1500) return bad("audioCtx must be 0 to 1500");
    if (s.peakMb < 0) return bad("peakMb must not be negative");
    if (!ids.insert(s.id).second) return bad("id used twice");
    std::error_code ec;
    const auto there = [&](const std::string& p) { return p.empty() || std::filesystem::exists(p, ec); };
    s.installed = there(s.path) && there(s.grammarFile) && there(s.promptFile);
    models.push_back(std::move(s));
  }
  if (models.empty()) {
    error = file + ": no models";
    return false;
  }
  out = std::move(models);
  return true;
}

namespace {
// FNV-1a: detects changed files, not tampering.
bool hashFile(const std::string& path, std::uint64_t& h) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::vector<char> b(1 << 16);
  while (f.read(b.data(), static_cast<std::streamsize>(b.size())), f.gcount() > 0)
    for (std::streamsize i = 0; i < f.gcount(); ++i)
      h = (h ^ static_cast<unsigned char>(b[static_cast<std::size_t>(i)])) * 1099511628211ULL;
  return f.eof();
}
std::string hashPath(const std::string& path) {
  if (path.empty()) return "";
  namespace fs = std::filesystem;
  std::error_code ec;
  std::uint64_t h = 1469598103934665603ULL;
  if (fs::is_directory(path, ec)) {
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator i(path, ec), end; i != end; i.increment(ec)) {
      if (ec) return "";
      if (i->is_regular_file(ec)) files.push_back(i->path());
    }
    if (ec || files.empty()) return "";
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
      for (unsigned char c : fs::relative(f, path, ec).string()) h = (h ^ c) * 1099511628211ULL;
      if (!hashFile(f.string(), h)) return "";
    }
  } else if (!hashFile(path, h)) {
    return "";
  }
  char b[17];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
  return b;
}
}  // namespace

std::string describeModel(const SttModel& m) {
  nlohmann::json j{{"id", m.id}, {"engine", m.engine}, {"model", std::filesystem::path(m.path).filename().string()}};
  if (m.engine == "whisper") j["threads"] = m.threads, j["audioCtx"] = m.audioCtx;
  const auto add = [&](const char* name, const std::string& path) {
    if (path.empty()) return true;
    const std::string h = hashPath(path);
    j[name] = h;
    return !h.empty();
  };
  if (!add("modelHash", m.path) || !add("grammarHash", m.grammarFile) || !add("promptHash", m.promptFile)) return "";
  return j.dump();
}

}  // namespace atc
