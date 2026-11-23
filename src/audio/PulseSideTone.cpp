#include "audio/PulseSideTone.h"

#include <pulse/pulseaudio.h>
#include <unistd.h>

#include <algorithm>

namespace atc {

struct PulseSideTone::Cb {
  static void context(pa_context* c, void* u) {
    auto& s = *static_cast<PulseSideTone*>(u);
    if (pa_context_get_state(c) != PA_CONTEXT_READY) {
      s.ready_ = false;
      return;
    }
    const pa_sample_spec spec{PA_SAMPLE_S16LE, static_cast<std::uint32_t>(s.rate_), 1};
    s.written_ = 0;
    s.up_ = pa_stream_new(c, s.name_.c_str(), &spec, nullptr);
    if (!s.up_) return;
    pa_stream_set_state_callback(s.up_, stream, u);
    pa_stream_set_write_callback(s.up_, write, u);
    pa_stream_connect_upload(s.up_, s.pcm_.size() * 2);
  }
  static void write(pa_stream* st, std::size_t room, void* u) {
    auto& s = *static_cast<PulseSideTone*>(u);
    const std::size_t total = s.pcm_.size() * 2;
    const std::size_t n = std::min(room, total - s.written_);
    if (n == 0) return;
    if (pa_stream_write(st, reinterpret_cast<const char*>(s.pcm_.data()) + s.written_, n, nullptr, 0, PA_SEEK_RELATIVE) < 0)
      return;
    s.written_ += n;
    if (s.written_ == total) pa_stream_finish_upload(st);
  }
  static void stream(pa_stream* st, void* u) {
    auto& s = *static_cast<PulseSideTone*>(u);
    if (pa_stream_get_state(st) == PA_STREAM_TERMINATED) s.ready_ = true;  // upload finished
  }
};

PulseSideTone::PulseSideTone(SideToneConfig cfg, std::string server)
    : pcm_(makeSideTone(cfg)), rate_(cfg.rate), server_(std::move(server)),
      name_("atc-sidetone-" + std::to_string(getpid())) {}

PulseSideTone::~PulseSideTone() {
  if (!loop_) return;
  pa_threaded_mainloop_lock(loop_);
  if (ready_) {
    if (pa_operation* o = pa_context_remove_sample(ctx_, name_.c_str(), nullptr, nullptr)) pa_operation_unref(o);
  }
  drop();
  pa_threaded_mainloop_unlock(loop_);
  pa_threaded_mainloop_stop(loop_);
  pa_threaded_mainloop_free(loop_);
}

bool PulseSideTone::start() {
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

bool PulseSideTone::connect() {
  drop();
  ctx_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "ATC trainer side-tone");
  if (!ctx_) return false;
  pa_context_set_state_callback(ctx_, Cb::context, this);
  return pa_context_connect(ctx_, server_.empty() ? nullptr : server_.c_str(), PA_CONTEXT_NOAUTOSPAWN, nullptr) >= 0;
}

void PulseSideTone::drop() {
  ready_ = false;
  if (up_) {
    pa_stream_set_state_callback(up_, nullptr, nullptr);
    pa_stream_set_write_callback(up_, nullptr, nullptr);
    pa_stream_unref(up_);
    up_ = nullptr;
  }
  if (ctx_) {
    pa_context_set_state_callback(ctx_, nullptr, nullptr);
    pa_context_disconnect(ctx_);
    pa_context_unref(ctx_);
    ctx_ = nullptr;
  }
}

bool PulseSideTone::play() {
  if (!loop_) return false;
  pa_threaded_mainloop_lock(loop_);
  bool ok = false;
  if (ready_) {
    if (pa_operation* o = pa_context_play_sample(ctx_, name_.c_str(), nullptr, PA_VOLUME_NORM, nullptr, nullptr)) {
      pa_operation_unref(o);
      ok = true;
    }
  } else if (!ctx_ || !PA_CONTEXT_IS_GOOD(pa_context_get_state(ctx_))) {
    connect();
  }
  pa_threaded_mainloop_unlock(loop_);
  return ok;
}

}  // namespace atc
