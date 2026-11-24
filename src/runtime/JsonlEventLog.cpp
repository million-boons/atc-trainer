#include "runtime/JsonlEventLog.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>

#include "engine/EventJson.h"

namespace atc {

JsonlEventLog::~JsonlEventLog() {
  end();
  if (thread_.joinable()) thread_.join();
}

bool JsonlEventLog::begin(const EventLogHeader& header, std::string& error) {
  if (thread_.joinable()) {
    error = "the log was already begun";
    return false;
  }
  if (error = headerError(header); !error.empty()) return false;
  if (!isIdCode(header.sessionId)) {  // it is a file name
    error = "sessionId must be a code: letters, digits, - and _";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  path_ = dir_ + "/" + header.sessionId + ".jsonl";
  fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd_ < 0) {
    error = path_ + ": " + std::strerror(errno);
    return false;
  }
  if (!write(headerJson(header) + "\n")) {
    error = path_ + ": " + std::strerror(errno);
    ::close(fd_);
    fd_ = -1;
    ::unlink(path_.c_str());
    return false;
  }
  open_ = true;
  thread_ = std::thread([this] { run(); });
  return true;
}

void JsonlEventLog::append(const EngineEvent& event) {
  {
    std::lock_guard l(m_);
    if (!open_ || ending_ || queue_.size() >= max_) {
      ++lost_;
      return;
    }
    queue_.push_back(event);
  }
  cv_.notify_one();
}

void JsonlEventLog::end() {
  {
    std::lock_guard l(m_);
    ending_ = true;
  }
  cv_.notify_one();
}

std::uint64_t JsonlEventLog::lost() const {
  std::lock_guard l(m_);
  return lost_;
}

bool JsonlEventLog::write(const std::string& text) {
  for (std::size_t done = 0; done < text.size();) {
    const auto n = ::write(fd_, text.data() + done, text.size() - done);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    done += std::size_t(n);
  }
  return true;
}

void JsonlEventLog::run() {
  bool failed = false;
  for (;;) {
    std::deque<EngineEvent> todo;
    {
      std::unique_lock l(m_);
      cv_.wait(l, [this] { return ending_ || !queue_.empty(); });
      if (queue_.empty()) break;
      todo.swap(queue_);
    }
    std::string text;
    for (const auto& e : todo) text += eventJson(e) + "\n";
    // After a failed write the file may end in half a line; nothing more is added to it.
    if (failed || !write(text)) {
      failed = true;
      std::lock_guard l(m_);
      lost_ += todo.size();
    }
  }
  ::fsync(fd_);
  ::close(fd_);
  fd_ = -1;
}

}  // namespace atc
