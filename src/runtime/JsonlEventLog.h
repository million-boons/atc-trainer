#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/IEventLog.h"

namespace atc {

// The event log as JSON Lines (D-401): DIR/<sessionId>.jsonl, readable by its owner only, never overwritten.
// append() never waits for the disk; a writer thread does the file work.
class JsonlEventLog : public IEventLog {
 public:
  explicit JsonlEventLog(std::string dir, std::size_t maxQueued = 10'000) : dir_(std::move(dir)), max_(maxQueued) {}
  ~JsonlEventLog() override;
  bool begin(const EventLogHeader& header, std::string& error) override;
  void append(const EngineEvent& event) override;
  void end() override;  // returns at once; the file is complete when the object is destroyed
  std::uint64_t lost() const override;
  std::string path() const { return path_; }

 private:
  void run();
  bool write(const std::string& text);

  const std::string dir_;
  const std::size_t max_;
  std::string path_;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::deque<EngineEvent> queue_;
  std::uint64_t lost_ = 0;
  bool open_ = false, ending_ = false;
  int fd_ = -1;  // the writer thread's after begin()
  std::thread thread_;
};

}  // namespace atc
