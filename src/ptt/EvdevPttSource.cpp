#include "ptt/EvdevPttSource.h"

#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

#include "ptt/PttDeviceCheck.h"

namespace atc {

void EvdevPttSource::start() {
  stop_ = false;
  thread_ = std::thread([this] { run(); });
}

void EvdevPttSource::stop() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();
}

void EvdevPttSource::run() {
  std::string reported;
  while (!stop_) {
    std::string error;
    if (session(error)) {
      reported.clear();
    } else if (error != reported) {
      reported = error;
      if (cb_.error) cb_.error(cfg_.path + ": " + error);
    }
    for (int i = 0; i < 10 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool EvdevPttSource::session(std::string& error) {
  PttDeviceRule rule;
  rule.grab = cfg_.grab;
  if (!cfg_.vidPid.empty() && !(rule.id = vidPidFromString(cfg_.vidPid))) {
    error = "pttVidPid \"" + cfg_.vidPid + "\" is not like 04d8:0055";
    return false;
  }
  const int fd = open(cfg_.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    error = std::strerror(errno);
    return false;
  }
  libevdev* dev = nullptr;
  int rc = libevdev_new_from_fd(fd, &dev);
  if (rc < 0) {
    close(fd);
    error = std::strerror(-rc);
    return false;
  }
  const auto finish = [&] {
    libevdev_free(dev);
    close(fd);
  };
  const auto code = static_cast<unsigned>(cfg_.code);
  if (!libevdev_has_event_code(dev, EV_KEY, code)) {
    finish();
    error = "the device does not have the PTT button";
    return false;
  }
  // Before anything is read from it (D-310), and again at every replug.
  error = claimPttDevice(dev, rule, [](libevdev* d) { return libevdev_grab(d, LIBEVDEV_GRAB); });
  if (!error.empty()) {
    finish();
    return false;
  }
  libevdev_set_clock_id(dev, CLOCK_MONOTONIC);
  if (cb_.opened) cb_.opened(libevdev_get_event_value(dev, EV_KEY, code) != 0);

  bool lost = false;
  while (!stop_ && !lost) {
    pollfd p{fd, POLLIN, 0};
    if (poll(&p, 1, 200) < 0 && errno != EINTR) break;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) lost = true;
    unsigned flag = LIBEVDEV_READ_FLAG_NORMAL;
    for (;;) {
      input_event ev{};
      rc = libevdev_next_event(dev, flag, &ev);
      if (rc == -EAGAIN) {
        if (flag == LIBEVDEV_READ_FLAG_NORMAL) break;
        flag = LIBEVDEV_READ_FLAG_NORMAL;  // resync finished
        continue;
      }
      if (rc < 0) {
        lost = true;
        break;
      }
      if (rc == LIBEVDEV_READ_STATUS_SYNC) flag = LIBEVDEV_READ_FLAG_SYNC;  // SYN_DROPPED
      if (ev.type == EV_KEY && ev.code == code && ev.value != 2 && cb_.edge)
        cb_.edge(Ns{ev.input_event_sec} * 1'000'000'000 + Ns{ev.input_event_usec} * 1000, ev.value != 0);
    }
  }
  finish();
  if (lost && cb_.lost) cb_.lost(monotonicNow());
  return true;
}

}  // namespace atc
