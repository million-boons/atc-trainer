#include "stt/SttService.h"

#include <algorithm>
#include <fstream>

namespace atc {

const char* toString(SttState s) {
  switch (s) {
    case SttState::NotLoaded: return "not loaded";
    case SttState::Loading: return "loading";
    case SttState::Ready: return "ready";
    case SttState::Busy: return "busy";
    case SttState::Failed: return "failed";
  }
  return "?";
}

long availableMemoryMb() {
  std::ifstream f("/proc/meminfo");
  for (std::string key, unit; f >> key;) {
    long kb = 0;
    if (!(f >> kb)) break;
    std::getline(f, unit);
    if (key == "MemAvailable:") return kb / 1024;
  }
  return -1;
}

SttService::SttService(std::vector<SttModel> models, Factory factory, Describe describe, Callbacks cb,
                       std::function<long()> freeMb)
    : models_(std::move(models)), factory_(std::move(factory)), describe_(std::move(describe)), cb_(std::move(cb)),
      freeMb_(std::move(freeMb)),
      thread_([this] { run(); }) {}

SttService::~SttService() {
  {
    std::lock_guard l(m_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

SttStatus SttService::status() const {
  std::lock_guard l(m_);
  return st_;
}

void SttService::post(std::function<void()> f) {
  {
    std::lock_guard l(m_);
    jobs_.push_back(std::move(f));
  }
  cv_.notify_one();
}

void SttService::run() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock l(m_);
      cv_.wait(l, [this] { return stop_ || !jobs_.empty(); });
      if (stop_) break;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();
  }
  engine_.reset();
}

void SttService::setStatus(SttState s, const std::string& id, const std::string& msg, double loadMs) {
  SttStatus copy;
  {
    std::lock_guard l(m_);
    st_.state = s;
    st_.modelId = id;
    st_.message = msg;
    if (loadMs >= 0) st_.loadMs = loadMs;
    copy = st_;
  }
  if (cb_.status) cb_.status(copy);
}

bool SttService::refuse(const std::string& why) {
  SttStatus copy;
  {
    std::lock_guard l(m_);
    st_.message = why;
    copy = st_;
  }
  if (cb_.status) cb_.status(copy);
  return false;
}

bool SttService::load(const std::string& id) {
  const auto it = std::find_if(models_.begin(), models_.end(), [&](const SttModel& m) { return m.id == id; });
  if (it == models_.end()) return refuse("unknown model " + id);
  if (!it->installed) return refuse(it->name + " is not installed");
  std::string why;
  {
    std::lock_guard l(m_);
    if (st_.locked) why = "model is locked for the exercise";
    else if (st_.state == SttState::Loading) why = "another model is loading";
    else if (txOpen_ || st_.state == SttState::Busy) why = "finish the transmission first";
    else {
      st_.state = SttState::Loading;  // claimed here, so a second request is refused at once
      st_.modelId = id;
      st_.message.clear();
    }
  }
  if (!why.empty()) return refuse(why);
  post([this, m = *it] {
    setStatus(SttState::Loading, m.id, "");
    engine_.reset();  // never two models in memory
    {
      std::lock_guard l(m_);
      st_.modelConfig.clear();
    }
    if (freeMb_ && m.peakMb > 0) {
      const long free = freeMb_();
      constexpr long kMarginMb = 200;  // left for the rest of the panel
      if (free >= 0 && free < m.peakMb + kMarginMb) {
        setStatus(SttState::Failed, m.id, "not enough memory: " + std::to_string(free) + " MB free, " +
                                             std::to_string(m.peakMb + kMarginMb) + " MB needed");
        return;
      }
    }
    const Ns t0 = monotonicNow();
    std::string error;
    const std::string config = describe_(m);
    if (config.empty()) error = "cannot identify the files of " + m.name;
    else engine_ = factory_(m, error);
    if (engine_) {
      std::lock_guard l(m_);
      st_.modelConfig = config;
    }
    if (engine_) setStatus(SttState::Ready, m.id, "", double(monotonicNow() - t0) / 1e6);
    else setStatus(SttState::Failed, m.id, error.empty() ? "cannot load " + m.name : error);
  });
  return true;
}

bool SttService::unload() {
  std::string why;
  {
    std::lock_guard l(m_);
    if (st_.locked) why = "model is locked for the exercise";
    else if (st_.state == SttState::Loading) why = "a model is loading";
    else if (txOpen_ || st_.state == SttState::Busy) why = "finish the transmission first";
    else st_.state = SttState::Loading;  // blocks begin() until the worker is done
  }
  if (!why.empty()) return refuse(why);
  post([this] {
    engine_.reset();
    {
      std::lock_guard l(m_);
      st_.modelConfig.clear();
    }
    setStatus(SttState::NotLoaded, "", "");
  });
  return true;
}

bool SttService::lock() {
  SttStatus copy;
  bool ok;
  {
    std::lock_guard l(m_);
    ok = st_.state == SttState::Ready && !txOpen_;
    if (ok) st_.locked = true;
    copy = st_;
  }
  if (!ok) return refuse("load a model before starting the exercise");
  if (cb_.status) cb_.status(copy);
  return true;
}

void SttService::unlock() {
  SttStatus copy;
  {
    std::lock_guard l(m_);
    st_.locked = false;
    copy = st_;
  }
  if (cb_.status) cb_.status(copy);
}

bool SttService::begin(std::uint64_t tx) {
  {
    std::lock_guard l(m_);
    if ((st_.state != SttState::Ready && st_.state != SttState::Busy) || txOpen_) return false;
    txOpen_ = true;
    tx_ = tx;
  }
  post([this] { engine_->begin(); });
  return true;
}

void SttService::accept(std::vector<std::int16_t> audio) {
  {
    std::lock_guard l(m_);
    if (!txOpen_) return;
  }
  post([this, a = std::move(audio)] { engine_->accept(a); });
}

void SttService::finish(Ns releaseTime) {
  std::uint64_t tx;
  SttStatus copy;
  {
    std::lock_guard l(m_);
    if (!txOpen_) return;
    txOpen_ = false;
    ++pending_;
    tx = tx_;
    st_.state = SttState::Busy;
    copy = st_;
  }
  if (cb_.status) cb_.status(copy);
  post([this, tx, releaseTime, id = copy.modelId, config = copy.modelConfig] {
    const SttResult r = engine_->finish();
    const Ns delay = monotonicNow() - releaseTime;
    if (cb_.result) cb_.result(tx, id, config, r, delay);
    bool last;
    {
      std::lock_guard l(m_);
      last = --pending_ == 0;
    }
    if (last) setStatus(SttState::Ready, id, "");
  });
}

void SttService::cancel() {
  {
    std::lock_guard l(m_);
    if (!txOpen_) return;
    txOpen_ = false;
  }
  // The next begin() clears the engine.
}

}  // namespace atc
