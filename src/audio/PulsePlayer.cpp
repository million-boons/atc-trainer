#include "audio/PulsePlayer.h"

#include <pulse/pulseaudio.h>

#include <algorithm>

namespace atc {
namespace {
constexpr pa_usec_t kStopWait = 100'000, kRetry = 1'000'000;
}

struct PulsePlayer::Cb {
  static void context(pa_context* c, void* u) {
    auto& s = *static_cast<PulsePlayer*>(u);
    const auto st = pa_context_get_state(c);
    if (st == PA_CONTEXT_READY) {
      pa_context_set_subscribe_callback(c, event, u);
      const auto mask = pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER);
      if (auto* o = pa_context_subscribe(c, mask, nullptr, nullptr)) pa_operation_unref(o);
      s.query();
    } else if (!PA_CONTEXT_IS_GOOD(st)) {
      s.ended(false);
      s.report({});
      s.retryLater();
      pa_threaded_mainloop_signal(s.loop_, 0);
    }
  }
  static void event(pa_context*, pa_subscription_event_type_t, std::uint32_t, void* u) { static_cast<PulsePlayer*>(u)->query(); }
  static void server(pa_context* c, const pa_server_info* i, void* u) {
    // auto_null: the server's silent stand-in when there is no output at all.
    if (!i || !i->default_sink_name || !*i->default_sink_name || std::string(i->default_sink_name) == "auto_null") return static_cast<PulsePlayer*>(u)->report({});
    if (auto* o = pa_context_get_sink_info_by_name(c, i->default_sink_name, sink, u)) pa_operation_unref(o);
  }
  static void sink(pa_context*, const pa_sink_info* i, int eol, void* u) {
    auto& s = *static_cast<PulsePlayer*>(u);
    if (eol < 0) s.report({});
    if (eol || !i) return;
    const auto v = std::uint64_t(pa_cvolume_max(&i->volume));
    s.report({true, i->name, i->description ? i->description : i->name, i->mute != 0, int((v * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM)});
  }
  static void retry(pa_mainloop_api*, pa_time_event*, const timeval*, void* u) {
    auto& s = *static_cast<PulsePlayer*>(u);
    pa_threaded_mainloop_get_api(s.loop_)->time_free(s.retry_);
    s.retry_ = nullptr;
    s.connect();
  }
  static void write(pa_stream* st, std::size_t room, void* u) {
    auto& s = *static_cast<PulsePlayer*>(u);
    if (s.stopping_) return;
    const std::size_t n = std::min(room / 2, s.buf_.size() - s.pos_);
    if (n > 0) {
      if (pa_stream_write(st, s.buf_.data() + s.pos_, n * 2, nullptr, 0, PA_SEEK_RELATIVE) < 0) return;
      s.pos_ += n;
    }
    if (s.pos_ == s.buf_.size() && !s.drain_) s.drain_ = pa_stream_drain(st, drained, u);
  }
  static void drained(pa_stream*, int ok, void* u) { static_cast<PulsePlayer*>(u)->ended(ok != 0); }
  static void stream(pa_stream* st, void* u) {
    auto& s = *static_cast<PulsePlayer*>(u);
    if (PA_STREAM_IS_GOOD(pa_stream_get_state(st))) return;
    s.ended(false);
    pa_threaded_mainloop_signal(s.loop_, 0);
  }
  static void ack(pa_stream*, int ok, void* u) {
    auto& s = *static_cast<PulsePlayer*>(u);
    if (ok) ++s.acks_;
    pa_threaded_mainloop_signal(s.loop_, 0);
  }
  static void timeout(pa_mainloop_api*, pa_time_event*, const timeval*, void* u) {
    auto& s = *static_cast<PulsePlayer*>(u);
    s.timedOut_ = true;
    pa_threaded_mainloop_signal(s.loop_, 0);
  }
};

PulsePlayer::PulsePlayer(PlayerCallbacks cb, std::string server, std::string sink)
    : cb_(std::move(cb)), server_(std::move(server)), sink_(std::move(sink)) {}

PulsePlayer::~PulsePlayer() {
  if (!loop_) return;
  pa_threaded_mainloop_lock(loop_);
  cb_ = {};
  closing_ = true;
  if (retry_) pa_threaded_mainloop_get_api(loop_)->time_free(retry_);
  dropContext();
  pa_threaded_mainloop_unlock(loop_);
  pa_threaded_mainloop_stop(loop_);
  pa_threaded_mainloop_free(loop_);
}

bool PulsePlayer::start() {
  if (loop_) return true;
  loop_ = pa_threaded_mainloop_new();
  if (!loop_) return false;
  if (pa_threaded_mainloop_start(loop_) < 0) {
    pa_threaded_mainloop_free(loop_);
    loop_ = nullptr;
    return false;
  }
  pa_threaded_mainloop_lock(loop_);
  connect();
  pa_threaded_mainloop_unlock(loop_);
  return true;
}

void PulsePlayer::connect() {
  dropContext();
  ctx_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "ATC trainer radio");
  if (!ctx_) return retryLater();
  pa_context_set_state_callback(ctx_, Cb::context, this);
  if (pa_context_connect(ctx_, server_.empty() ? nullptr : server_.c_str(), PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) retryLater();
}

void PulsePlayer::retryLater() {
  if (retry_ || closing_) return;
  timeval tv;
  pa_gettimeofday(&tv);
  pa_timeval_add(&tv, kRetry);
  auto* api = pa_threaded_mainloop_get_api(loop_);
  retry_ = api->time_new(api, &tv, Cb::retry, this);
}

void PulsePlayer::dropContext() {
  dropStream();
  if (!ctx_) return;
  pa_context_set_state_callback(ctx_, nullptr, nullptr);
  pa_context_set_subscribe_callback(ctx_, nullptr, nullptr);
  pa_context_disconnect(ctx_);
  pa_context_unref(ctx_);
  ctx_ = nullptr;
}

void PulsePlayer::query() {
  if (!ctx_ || pa_context_get_state(ctx_) != PA_CONTEXT_READY) return;
  pa_operation* o = sink_.empty() ? pa_context_get_server_info(ctx_, Cb::server, this)
                                  : pa_context_get_sink_info_by_name(ctx_, sink_.c_str(), Cb::sink, this);
  if (o) pa_operation_unref(o);
}

void PulsePlayer::report(const OutputStatus& s) {
  const bool same = reported_ && s.available == last_.available && s.name == last_.name && s.description == last_.description &&
                    s.muted == last_.muted && s.volumePercent == last_.volumePercent;
  if (same) return;
  reported_ = true;
  last_ = s;
  if (cb_.status) cb_.status(s);
}

void PulsePlayer::dropStream() {
  if (drain_) {
    pa_operation_cancel(drain_);
    pa_operation_unref(drain_);
    drain_ = nullptr;
  }
  if (!st_) return;
  pa_stream_set_state_callback(st_, nullptr, nullptr);
  pa_stream_set_write_callback(st_, nullptr, nullptr);
  pa_stream_disconnect(st_);
  pa_stream_unref(st_);
  st_ = nullptr;
  buf_.clear();
}

// From a callback. While stop() waits, it decides alone.
void PulsePlayer::ended(bool completed) {
  if (!st_ || stopping_) return;
  dropStream();
  if (cb_.finished) cb_.finished(id_, completed);
}

bool PulsePlayer::play(PlayId id, std::vector<std::int16_t> samples, int rate) {
  if (!loop_ || samples.empty() || rate <= 0) return false;
  stop();
  pa_threaded_mainloop_lock(loop_);
  bool ok = false;
  if (ctx_ && pa_context_get_state(ctx_) == PA_CONTEXT_READY) {
    const pa_sample_spec spec{PA_SAMPLE_S16LE, static_cast<std::uint32_t>(rate), 1};
    st_ = pa_stream_new(ctx_, "radio", &spec, nullptr);
    if (st_) {
      id_ = id;
      buf_ = std::move(samples);
      pos_ = 0;
      pa_stream_set_state_callback(st_, Cb::stream, this);
      pa_stream_set_write_callback(st_, Cb::write, this);
      // About 100 ms buffered, so that a stop has little to discard.
      const pa_buffer_attr attr{static_cast<std::uint32_t>(-1), static_cast<std::uint32_t>(rate / 10 * 2),
                                static_cast<std::uint32_t>(-1), static_cast<std::uint32_t>(-1),
                                static_cast<std::uint32_t>(-1)};
      // DONT_MOVE: speech must not jump to another output when the headset is unplugged.
      const auto flags = pa_stream_flags_t(PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE);
      ok = pa_stream_connect_playback(st_, sink_.empty() ? nullptr : sink_.c_str(), &attr, flags, nullptr, nullptr) >= 0;
      if (!ok) dropStream();
    }
  }
  pa_threaded_mainloop_unlock(loop_);
  return ok;
}

bool PulsePlayer::stop() {
  if (!loop_) return true;
  pa_threaded_mainloop_lock(loop_);
  bool silent = true;
  // A stream that is not ready has not been given to the sound card; disconnecting is enough.
  if (st_ && pa_stream_get_state(st_) == PA_STREAM_READY) {
    stopping_ = true;
    timedOut_ = false;
    acks_ = 0;
    pa_operation* ops[2] = {pa_stream_cork(st_, 1, Cb::ack, this), pa_stream_flush(st_, Cb::ack, this)};
    pa_time_event* timer = pa_context_rttime_new(ctx_, pa_rtclock_now() + kStopWait, Cb::timeout, this);
    const auto running = [&] {
      return std::any_of(std::begin(ops), std::end(ops), [](pa_operation* o) { return o && pa_operation_get_state(o) == PA_OPERATION_RUNNING; });
    };
    if (timer)
      while (!timedOut_ && PA_STREAM_IS_GOOD(pa_stream_get_state(st_)) && running()) pa_threaded_mainloop_wait(loop_);
    silent = acks_ == 2;
    for (auto* o : ops)
      if (o) {
        pa_operation_cancel(o);
        pa_operation_unref(o);
      }
    if (timer) pa_threaded_mainloop_get_api(loop_)->time_free(timer);
    stopping_ = false;
  }
  dropStream();
  pa_threaded_mainloop_unlock(loop_);
  return silent;
}

}  // namespace atc
