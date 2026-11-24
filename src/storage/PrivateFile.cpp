#include "storage/PrivateFile.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>

namespace atc {
namespace {
bool writeAll(int fd, const std::string& text) {
  std::size_t done = 0;
  while (done < text.size()) {
    const auto n = ::write(fd, text.data() + done, text.size() - done);
    if (n <= 0) return false;
    done += std::size_t(n);
  }
  return ::fsync(fd) == 0;
}
}  // namespace

bool writePrivateFile(const std::string& path, const std::string& text) {
  std::error_code ec;
  const auto dir = std::filesystem::path(path).parent_path();
  if (!dir.empty() && !std::filesystem::is_directory(dir, ec)) {
    std::filesystem::create_directories(dir.parent_path(), ec);
    if (::mkdir(dir.c_str(), 0700) != 0) return false;
  }
  const std::string tmp = path + ".part";
  ::unlink(tmp.c_str());  // an older one could have other access rights
  const int fd = ::open(tmp.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  const bool ok = writeAll(fd, text);
  if ((::close(fd) == 0) && ok && std::rename(tmp.c_str(), path.c_str()) == 0) return true;
  ::unlink(tmp.c_str());
  return false;
}

bool appendPrivateLine(const std::string& path, const std::string& line) {
  const int fd = ::open(path.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, 0600);
  if (fd < 0) return false;
  const bool ok = writeAll(fd, line + "\n");
  return (::close(fd) == 0) && ok;
}

}  // namespace atc
