#include "core/PathResolver.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace atc {
namespace {

std::string trim(const std::string& s) {
  const auto a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return "";
  return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

// key=value lines, # comments. Values must be absolute folders.
bool readFile(const std::string& file, std::map<std::string, std::string>& out, std::string& error) {
  std::ifstream f(file);
  if (!f) {
    error = "cannot read " + file;
    return false;
  }
  std::string line;
  for (int n = 1; std::getline(f, line); ++n) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const std::string at = file + ":" + std::to_string(n) + ": ";
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      error = at + "expected name=folder";
      return false;
    }
    const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
    if (key != "root" && key != "models" && key != "voices") {
      error = at + "unknown name " + key;
      return false;
    }
    if (!fs::path(value).is_absolute()) {
      error = at + key + " must be an absolute folder";
      return false;
    }
    if (!out.emplace(key, value).second) {
      error = at + key + " given twice";
      return false;
    }
  }
  return true;
}

bool isDir(const std::string& p) {
  std::error_code ec;
  return fs::is_directory(p, ec);
}

bool inside(const std::string& path, const std::string& folder) {
  std::error_code ec;
  const fs::path p = fs::weakly_canonical(path, ec), f = fs::weakly_canonical(folder, ec);
  auto a = p.begin();
  for (auto b = f.begin(); b != f.end(); ++b, ++a)
    if (a == p.end() || *a != *b) return false;
  return true;
}

fs::path prefixOf(const std::string& exe) { return fs::path(exe).lexically_normal().parent_path().parent_path(); }

}  // namespace

PathInputs systemPathInputs() {
  PathInputs in;
  std::error_code ec;
  in.exe = fs::read_symlink("/proc/self/exe", ec).string();
  if (const char* h = std::getenv("HOME")) in.home = h;
  return in;
}

std::string engineFolder(const std::string& exe) { return (prefixOf(exe) / "lib" / "ggml").string(); }

bool resolvePaths(const PathInputs& in, Paths& out, std::string& error) {
  if (in.home.empty() || !fs::path(in.home).is_absolute()) {
    error = "HOME is not set to an absolute folder";
    return false;
  }
  if (!in.root.empty()) {
    std::error_code ec;
    const std::string given = fs::absolute(in.root, ec).lexically_normal().string();
    if (ec || !isDir(given)) {
      error = "--root: folder not found: " + in.root;
      return false;
    }
    if (!isDir(given + "/config")) {
      error = "--root: no config folder in " + given;
      return false;
    }
  }
  struct Found {
    std::string value, file;
  };
  std::map<std::string, Found> set;
  std::string tried;
  // --root replaces the configuration files altogether, so that a test run depends on nothing else.
  for (const std::string& file : {in.home + "/.config/atc-trainer/paths.conf", in.etc + "/atc-trainer/paths.conf"}) {
    if (!in.root.empty()) break;
    std::error_code ec;
    const bool there = fs::exists(file, ec);
    if (ec) {
      error = "cannot read " + file + ": " + ec.message();
      return false;
    }
    if (!there) {
      tried += "\n  " + file + " (not there)";
      continue;
    }
    std::map<std::string, std::string> values;
    if (!readFile(file, values, error)) return false;
    if (!values.count("root")) tried += "\n  " + file + " (no root=)";
    for (const auto& [k, v] : values) set.emplace(k, Found{v, file});  // the first file wins
  }
  for (const auto& [k, f] : set)
    if (!isDir(f.value)) {
      error = f.file + ": " + k + " folder not found: " + f.value;
      return false;
    }

  Paths p;
  if (!in.root.empty()) {
    p.root = fs::absolute(in.root).lexically_normal().string();
    if (p.root.size() > 1 && p.root.back() == '/') p.root.pop_back();
  } else if (const auto r = set.find("root"); r != set.end()) {
    if (!isDir(r->second.value + "/config")) {
      error = r->second.file + ": no config folder in root " + r->second.value;
      return false;
    }
    p.root = r->second.value;
  } else {
    const std::string share = (prefixOf(in.exe) / "share" / "atc-trainer").string();
    if (in.exe.empty() || !isDir(share + "/config")) {
      error = "program files not found; tried:" + tried + "\n  " +
              (in.exe.empty() ? "the folder of this program (unknown)" : share + " (no config folder)");
      return false;
    }
    p.root = share;
  }
  p.config = p.root + "/config";
  p.data = p.root + "/data";
  p.models = set.count("models") ? set["models"].value : p.root;
  p.voices = set.count("voices") ? set["voices"].value : p.root + "/voices";
  p.engines = engineFolder(in.exe);
  p.writable = in.home + "/.local/state/atc-trainer";
  if (inside(p.writable, p.root)) {
    error = "the writable folder " + p.writable + " would be inside the program files " + p.root;
    return false;
  }
  out = std::move(p);
  return true;
}

}  // namespace atc
