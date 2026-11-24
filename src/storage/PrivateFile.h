#pragma once
#include <string>

namespace atc {
// Folder 0700, file 0600, written under another name and renamed: never half-written.
bool writePrivateFile(const std::string& path, const std::string& text);
// One line at the end of a file (created 0600), on disk when this returns.
bool appendPrivateLine(const std::string& path, const std::string& line);
}  // namespace atc
