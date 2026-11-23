#include "ptt/EvdevPttSource.h"

#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

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
  bool reported = false;
  while (!stop_) {
    if (session()) {
      reported = false;
    } else if (!reported) {
      reported = true;
      if (cb_.error) cb_.error(cfg_.path + ": " + std::strerror(errno));
    }
    for (int i = 0; i < 10 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool EvdevPttSource::session() {
  const int fd = open(cfg_.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return false;
  libevdev* dev = nullptr;
  int rc = libevdev_new_from_fd(fd, &dev);
  if (rc < 0) {
    close(fd);
    errno = -rc;
    return false;
  }
  const auto finish = [&](int err) {
    libevdev_free(dev);
    close(fd);
    errno = err;
  };
  const auto code = static_cast<unsigned>(cfg_.code);
  if (!libevdev_has_event_code(dev, EV_KEY, code)) {
    finish(ENODEV);
    return false;
  }
  if (cfg_.grab && (rc = libevdev_grab(dev, LIBEVDEV_GRAB)) < 0) {
    finish(-rc);
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
  finish(0);
  if (lost && cb_.lost) cb_.lost(monotonicNow());
  return true;
}

}  // namespace atc
