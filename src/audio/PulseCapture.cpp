#include "audio/PulseCapture.h"

#include <pulse/pulseaudio.h>
#include <sys/time.h>
#include <time.h>

namespace atc {
namespace {
Ns now() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return Ns{ts.tv_sec} * 1'000'000'000 + ts.tv_nsec;
}
}  // namespace

struct PulseCapture::Cb {
  static PulseCapture& self(void* u) { return *static_cast<PulseCapture*>(u); }

  static void context(pa_context* c, void* u) {
    auto& s = self(u);
    if (c != s.ctx_) return;
    switch (pa_context_get_state(c)) {
      case PA_CONTEXT_READY:
        pa_context_set_subscribe_callback(c, subscribed, u);
        unref(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SOURCE, nullptr, nullptr));
        if (s.cfg_.source.empty())
          unref(pa_context_get_server_info(c, serverInfo, u));
        else
          s.openStream(s.cfg_.source);
        break;
      case PA_CONTEXT_FAILED:
      case PA_CONTEXT_TERMINATED:
        s.fail(std::string("sound server: ") + pa_strerror(pa_context_errno(c)));
        break;
      default: break;
    }
  }
  static void unref(pa_operation* o) {
    if (o) pa_operation_unref(o);
  }
  static void serverInfo(pa_context* c, const pa_server_info* i, void* u) {
    auto& s = self(u);
    if (c != s.ctx_) return;
    const std::string name = i && i->default_source_name ? i->default_source_name : "";
    const std::string mon = ".monitor";
    if (name.empty())
      s.fail("no default microphone");
    else if (name.size() >= mon.size() && name.compare(name.size() - mon.size(), mon.size(), mon) == 0)
      s.fail("default source is a playback monitor, not a microphone: " + name);
    else
      s.openStream(name);
  }
  static void sourceInfo(pa_context* c, const pa_source_info* i, int eol, void* u) {
    auto& s = self(u);
    if (c != s.ctx_ || eol || !i) return;
    const std::string desc = i->description ? i->description : "";
    const bool muted = i->mute != 0;
    const int vol = static_cast<int>((100ull * pa_cvolume_avg(&i->volume) + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
    bool changed;
    {
      std::lock_guard l(s.m_);
      changed = !s.announced_ || desc != s.st_.description || muted != s.st_.muted || vol != s.st_.volumePercent;
      s.st_.description = desc;
      s.st_.muted = muted;
      s.st_.volumePercent = vol;
    }
    s.announced_ = true;
    if (changed) s.publish();
  }
  static void subscribed(pa_context* c, pa_subscription_event_type_t, uint32_t, void* u) {
    auto& s = self(u);
    if (c == s.ctx_ && s.stream_ && pa_stream_get_state(s.stream_) == PA_STREAM_READY)
      unref(pa_context_get_source_info_by_name(c, pa_stream_get_device_name(s.stream_), sourceInfo, u));
  }
  static void streamState(pa_stream* st, void* u) {
    auto& s = self(u);
    if (st != s.stream_) return;
    switch (pa_stream_get_state(st)) {
      case PA_STREAM_READY: {
        {
          std::lock_guard l(s.m_);
          s.st_.state = CaptureState::Running;
          s.st_.source = pa_stream_get_device_name(st);
          s.st_.message.clear();
        }
        // Published once the mute state and name are known.
        s.announced_ = false;
        unref(pa_context_get_source_info_by_name(s.ctx_, pa_stream_get_device_name(st), sourceInfo, u));
        break;
      }
      case PA_STREAM_FAILED:
      case PA_STREAM_TERMINATED:
        s.fail(std::string("microphone: ") + pa_strerror(pa_context_errno(s.ctx_)));
        break;
      default: break;
    }
  }
  static void read(pa_stream* st, size_t, void* u) {
    auto& s = self(u);
    while (pa_stream_readable_size(st) > 0) {
      const void* data = nullptr;
      size_t bytes = 0;
      if (pa_stream_peek(st, &data, &bytes) < 0 || bytes == 0) return;
      if (data) {
        const std::span<const std::int16_t> block(static_cast<const std::int16_t*>(data), bytes / 2);
        const Ns t = now();
        s.ring_.push(t, block);
        s.meter_.add(block);
        s.lastBlock_.store(t, std::memory_order_relaxed);
      } else {
        s.ring_.pushGap(bytes / 2);  // a hole reported by the server
      }
      pa_stream_drop(st);
    }
  }
  static void retry(pa_mainloop_api* api, pa_time_event* e, const timeval*, void* u) {
    auto& s = self(u);
    api->time_free(e);
    s.retryPending_ = false;
    s.teardown();
    s.connect();
  }
};

PulseCapture::PulseCapture(CaptureConfig cfg, PrerollRing& ring, LevelMeter& meter,
                           std::function<void(const CaptureStatus&)> onStatus)
    : cfg_(std::move(cfg)), ring_(ring), meter_(meter), onStatus_(std::move(onStatus)) {}

PulseCapture::~PulseCapture() { stop(); }

bool PulseCapture::start() {
  if (loop_) return true;
  loop_ = pa_threaded_mainloop_new();
  if (!loop_) return false;
  pa_threaded_mainloop_lock(loop_);
  const bool ok = pa_threaded_mainloop_start(loop_) == 0;
  if (ok) connect();
  pa_threaded_mainloop_unlock(loop_);
  if (!ok) {
    pa_threaded_mainloop_free(loop_);
    loop_ = nullptr;
  }
  return ok;
}

void PulseCapture::stop() {
  if (!loop_) return;
  pa_threaded_mainloop_stop(loop_);  // no callbacks run after this
  teardown();
  pa_threaded_mainloop_free(loop_);
  loop_ = nullptr;
  retryPending_ = false;
  std::lock_guard l(m_);
  st_.state = CaptureState::Stopped;
}

CaptureStatus PulseCapture::status() const {
  std::lock_guard l(m_);
  CaptureStatus s = st_;
  s.lastBlock = lastBlock_.load(std::memory_order_relaxed);
  return s;
}

void PulseCapture::publish() {
  if (onStatus_) onStatus_(status());
}

void PulseCapture::connect() {
  {
    std::lock_guard l(m_);
    if (st_.state != CaptureState::Lost) st_.state = CaptureState::Connecting;
  }
  ctx_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "ATC Trainer");
  pa_context_set_state_callback(ctx_, Cb::context, this);
  if (pa_context_connect(ctx_, cfg_.server.empty() ? nullptr : cfg_.server.c_str(), PA_CONTEXT_NOAUTOSPAWN,
                         nullptr) < 0)
    fail(std::string("sound server: ") + pa_strerror(pa_context_errno(ctx_)));
}

void PulseCapture::openStream(const std::string& source) {
  pa_sample_spec ss{};
  ss.format = PA_SAMPLE_S16LE;
  ss.rate = static_cast<uint32_t>(cfg_.rate);
  ss.channels = 1;
  pa_buffer_attr a{};
  a.maxlength = a.tlength = a.prebuf = a.minreq = static_cast<uint32_t>(-1);
  a.fragsize = static_cast<uint32_t>(pa_usec_to_bytes(static_cast<pa_usec_t>(cfg_.fragmentMs) * 1000, &ss));
  stream_ = pa_stream_new(ctx_, "trainee microphone", &ss, nullptr);
  if (!stream_) return fail("cannot create capture stream");
  pa_stream_set_state_callback(stream_, Cb::streamState, this);
  pa_stream_set_read_callback(stream_, Cb::read, this);
  const auto flags = static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE);
  ring_.restart();  // no effect before the first block
  if (pa_stream_connect_record(stream_, source.c_str(), &a, flags) < 0)
    fail(std::string("microphone: ") + pa_strerror(pa_context_errno(ctx_)));
}

void PulseCapture::teardown() {
  if (stream_) {
    pa_stream_set_state_callback(stream_, nullptr, nullptr);
    pa_stream_set_read_callback(stream_, nullptr, nullptr);
    pa_stream_disconnect(stream_);
    pa_stream_unref(stream_);
    stream_ = nullptr;
  }
  if (ctx_) {
    pa_context_set_state_callback(ctx_, nullptr, nullptr);
    pa_context_set_subscribe_callback(ctx_, nullptr, nullptr);
    pa_context_disconnect(ctx_);
    pa_context_unref(ctx_);
    ctx_ = nullptr;
  }
}

// Runs on the loop thread. Objects are freed later, from the retry timer.
void PulseCapture::fail(const std::string& why) {
  if (retryPending_) return;
  retryPending_ = true;
  {
    std::lock_guard l(m_);
    st_.state = CaptureState::Lost;
    st_.message = why;
  }
  publish();
  auto* api = pa_threaded_mainloop_get_api(loop_);
  timeval tv{};
  pa_gettimeofday(&tv);
  pa_timeval_add(&tv, PA_USEC_PER_SEC);
  api->time_new(api, &tv, Cb::retry, this);
}

}  // namespace atc
