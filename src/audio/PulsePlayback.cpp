#include "audio/PulsePlayback.h"

#include <pulse/pulseaudio.h>

#include <algorithm>

namespace atc {

struct PulsePlayback::Cb {
  static void context(pa_context* c, void* u) {
    auto& s = *static_cast<PulsePlayback*>(u);
    if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(c))) s.end(false);
  }
  static void write(pa_stream* st, std::size_t room, void* u) {
    auto& s = *static_cast<PulsePlayback*>(u);
    const std::size_t n = std::min(room / 2, s.buf_.size() - s.pos_);
    if (n > 0) {
      if (pa_stream_write(st, s.buf_.data() + s.pos_, n * 2, nullptr, 0, PA_SEEK_RELATIVE) < 0) return;
      s.pos_ += n;
    }
    if (s.pos_ == s.buf_.size() && !s.drain_) s.drain_ = pa_stream_drain(st, drained, u);
  }
  static void drained(pa_stream*, int ok, void* u) { static_cast<PulsePlayback*>(u)->end(ok != 0); }
  static void stream(pa_stream* st, void* u) {
    if (pa_stream_get_state(st) == PA_STREAM_FAILED) static_cast<PulsePlayback*>(u)->end(false);
  }
};

PulsePlayback::PulsePlayback(Finished f, std::string server) : done_(std::move(f)), server_(std::move(server)) {}

PulsePlayback::~PulsePlayback() {
  if (!loop_) return;
  pa_threaded_mainloop_lock(loop_);
  done_ = nullptr;
  dropContext();
  pa_threaded_mainloop_unlock(loop_);
  pa_threaded_mainloop_stop(loop_);
  pa_threaded_mainloop_free(loop_);
}

bool PulsePlayback::start() {
  if (loop_) return true;
  loop_ = pa_threaded_mainloop_new();
  if (!loop_) return false;
  if (pa_threaded_mainloop_start(loop_) < 0) {
    pa_threaded_mainloop_free(loop_);
    loop_ = nullptr;
    return false;
  }
  pa_threaded_mainloop_lock(loop_);
  const bool ok = connect();
  pa_threaded_mainloop_unlock(loop_);
  return ok;
}

bool PulsePlayback::connect() {
  dropContext();
  ctx_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "ATC trainer playback");
  if (!ctx_) return false;
  pa_context_set_state_callback(ctx_, Cb::context, this);
  return pa_context_connect(ctx_, server_.empty() ? nullptr : server_.c_str(), PA_CONTEXT_NOAUTOSPAWN, nullptr) >= 0;
}

void PulsePlayback::dropContext() {
  end(false);
  if (!ctx_) return;
  pa_context_set_state_callback(ctx_, nullptr, nullptr);
  pa_context_disconnect(ctx_);
  pa_context_unref(ctx_);
  ctx_ = nullptr;
}

void PulsePlayback::end(bool completed) {
  if (!st_) return;
  pa_stream* st = st_;
  st_ = nullptr;
  if (drain_) {
    pa_operation_cancel(drain_);
    pa_operation_unref(drain_);
    drain_ = nullptr;
  }
  pa_stream_set_state_callback(st, nullptr, nullptr);
  pa_stream_set_write_callback(st, nullptr, nullptr);
  pa_stream_disconnect(st);
  pa_stream_unref(st);
  buf_.clear();
  if (done_) done_(id_, completed);
}

bool PulsePlayback::play(std::uint64_t id, std::vector<std::int16_t> samples, int rate) {
  if (!loop_ || samples.empty()) return false;
  pa_threaded_mainloop_lock(loop_);
  end(false);
  bool ok = false;
  if (ctx_ && pa_context_get_state(ctx_) == PA_CONTEXT_READY) {
    const pa_sample_spec spec{PA_SAMPLE_S16LE, static_cast<std::uint32_t>(rate), 1};
    st_ = pa_stream_new(ctx_, "recording", &spec, nullptr);
    if (st_) {
      id_ = id;
      buf_ = std::move(samples);
      pos_ = 0;
      pa_stream_set_state_callback(st_, Cb::stream, this);
      pa_stream_set_write_callback(st_, Cb::write, this);
      // About 100 ms buffered, so stop() is prompt.
      const pa_buffer_attr attr{static_cast<std::uint32_t>(-1), static_cast<std::uint32_t>(rate / 10 * 2),
                                static_cast<std::uint32_t>(-1), static_cast<std::uint32_t>(-1),
                                static_cast<std::uint32_t>(-1)};
      ok = pa_stream_connect_playback(st_, nullptr, &attr, PA_STREAM_ADJUST_LATENCY, nullptr, nullptr) >= 0;
      if (!ok) {
        auto keep = std::move(done_);  // the caller sees false; no callback
        done_ = nullptr;
        end(false);
        done_ = std::move(keep);
      }
    }
  } else if (!ctx_ || !PA_CONTEXT_IS_GOOD(pa_context_get_state(ctx_))) {
    connect();
  }
  pa_threaded_mainloop_unlock(loop_);
  return ok;
}

void PulsePlayback::stop() {
  if (!loop_) return;
  pa_threaded_mainloop_lock(loop_);
  end(false);
  pa_threaded_mainloop_unlock(loop_);
}

}  // namespace atc
