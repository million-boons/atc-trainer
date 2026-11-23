#include "stt/WhisperStt.h"

#include <ggml-backend.h>
#include <whisper.h>

#include <algorithm>

namespace atc {
namespace {
struct CtxFree {
  void operator()(whisper_context* c) const { whisper_free(c); }
};
class WhisperStt : public ISpeechRecognizer {
 public:
  WhisperStt(whisper_context* c, WhisperConfig cfg) : ctx_(c), cfg_(std::move(cfg)) {}
  std::string name() const override { return "whisper"; }
  void begin() override { pcm_.clear(); }
  void accept(std::span<const std::int16_t> a) override {
    for (std::int16_t s : a) pcm_.push_back(static_cast<float>(s) / 32768.0f);
  }
  SttResult finish() override {
    SttResult r;
    auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = cfg_.threads;
    p.language = "en";
    p.no_context = true;
    p.no_timestamps = true;
    p.single_segment = true;
    p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
    p.suppress_nst = true;
    p.temperature_inc = 0.0f;  // no second decode: one latency, deterministic
    // 50 positions per second. A transmission longer than the short window gets the full one:
    // slower, but nothing is cut off.
    const std::size_t needed = pcm_.size() / 320 + 1;
    p.audio_ctx = cfg_.audioCtx > 0 && needed > std::size_t(cfg_.audioCtx) ? 0 : cfg_.audioCtx;
    if (!cfg_.prompt.empty()) p.initial_prompt = cfg_.prompt.c_str();
    if (whisper_full(ctx_.get(), p, pcm_.data(), static_cast<int>(pcm_.size())) != 0) return r;
    for (int s = 0; s < whisper_full_n_segments(ctx_.get()); ++s) {
      r.text += whisper_full_get_segment_text(ctx_.get(), s);
      for (int i = 0; i < whisper_full_n_tokens(ctx_.get(), s); ++i) {
        if (whisper_full_get_token_id(ctx_.get(), s, i) >= whisper_token_eot(ctx_.get())) continue;
        const std::string t = whisper_full_get_token_text(ctx_.get(), s, i);
        const double conf = whisper_full_get_token_p(ctx_.get(), s, i);
        // A token starting with a space starts a word; a word is as sure as its weakest token.
        if (r.words.empty() || (!t.empty() && t[0] == ' ')) {
          r.words.push_back({t, conf});
        } else {
          r.words.back().text += t;
          r.words.back().conf = std::min(r.words.back().conf, conf);
        }
      }
    }
    return r;
  }

 private:
  std::unique_ptr<whisper_context, CtxFree> ctx_;
  WhisperConfig cfg_;
  std::vector<float> pcm_;
};
}  // namespace

std::unique_ptr<ISpeechRecognizer> makeWhisper(const WhisperConfig& cfg, std::string& error) {
  whisper_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
  ggml_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
  ggml_backend_load_all_from_path(cfg.backendDir.c_str());
  if (!ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU)) {
    error = "no CPU backend found in " + cfg.backendDir;
    return nullptr;
  }
  auto cp = whisper_context_default_params();
  cp.use_gpu = false;
  whisper_context* c = whisper_init_from_file_with_params(cfg.model.c_str(), cp);
  if (!c) {
    error = "cannot load whisper model " + cfg.model;
    return nullptr;
  }
  return std::make_unique<WhisperStt>(c, cfg);
}

std::string whisperCpuFeatures() {
  std::string out;
  auto* dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
  auto* reg = dev ? ggml_backend_dev_backend_reg(dev) : nullptr;
  auto* fn = reg ? reinterpret_cast<ggml_backend_get_features_t>(
                       ggml_backend_reg_get_proc_address(reg, "ggml_backend_get_features"))
                 : nullptr;
  if (!fn) return "unknown";
  for (auto* f = fn(reg); f->name; ++f) out += std::string(out.empty() ? "" : " ") + f->name + "=" + f->value;
  return out;
}

}  // namespace atc
