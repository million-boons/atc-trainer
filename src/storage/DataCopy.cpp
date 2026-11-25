#include "storage/DataCopy.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>

#include "phraseology/PhraseologyEdit.h"
#include "storage/PrivateFile.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
// Not *.json: loader and parser read every JSON file in the folder.
constexpr const char* kOrigin = "ORIGIN";

std::optional<std::string> slurp(const fs::path& file) {
  std::ifstream f(file, std::ios::binary);
  if (!f) return std::nullopt;
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// One line for each file: hash and name.
std::string fingerprint(const std::string& dir) {
  std::string out;
  for (const auto& name : phraseologyFiles(dir)) {
    std::uint64_t h = 14695981039346656037ull;
    for (const unsigned char c : slurp(fs::path(dir) / name).value_or("")) h = (h ^ c) * 1099511628211ull;
    char hex[20];
    std::snprintf(hex, sizeof hex, "%016llx ", static_cast<unsigned long long>(h));
    out += hex + name + "\n";
  }
  return out;
}

struct Scrap {  // a folder that is removed again
  fs::path dir;
  ~Scrap() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
};
}  // namespace

bool DataCopy::exists() const {
  std::error_code ec;
  return fs::is_directory(dir_, ec);
}

bool DataCopy::installedIsNewer() const {
  return exists() && slurp(fs::path(dir_) / kOrigin).value_or("") != fingerprint(installed_);
}

std::string DataCopy::copyInto(const std::string& from, const std::string& to) const {
  std::error_code ec;
  const auto files = phraseologyFiles(from);
  if (files.empty()) return from + ": no phraseology files";
  for (const auto& name : files) {
    const auto bytes = slurp(fs::path(from) / name);
    if (!bytes) return (fs::path(from) / name).string() + ": cannot read";
    if (!writePrivateFile((fs::path(to) / name).string(), *bytes)) return (fs::path(to) / name).string() + ": cannot write";
  }
  return "";
}

std::string DataCopy::write(const std::string& file, const std::string& bytes, const Check& check) {
  std::error_code ec;
  // The whole new content first: checked there, and for the first save renamed to the copy in one step.
  const Scrap next{dir_ + ".next"};
  fs::remove_all(next.dir, ec);
  if (std::string why = copyInto(dir(), next.dir.string()); !why.empty()) return why;
  if (!fs::exists(next.dir / file, ec)) return file + ": no such file";
  if (!writePrivateFile((next.dir / file).string(), bytes)) return (next.dir / file).string() + ": cannot write";
  if (check)
    if (std::string why = check(next.dir.string()); !why.empty()) return why;
  if (exists()) return writePrivateFile((fs::path(dir_) / file).string(), bytes) ? "" : (fs::path(dir_) / file).string() + ": cannot write";
  if (!writePrivateFile((next.dir / kOrigin).string(), fingerprint(installed_))) return (next.dir / kOrigin).string() + ": cannot write";
  fs::rename(next.dir, dir_, ec);
  return ec ? dir_ + ": " + ec.message() : "";
}

}  // namespace atc
