#include "stt/VoskStt.h"

#include <vosk_api.h>

#include <nlohmann/json.hpp>
#include <sstream>

namespace atc {
namespace {
struct ModelFree {
  void operator()(VoskModel* m) const { vosk_model_free(m); }
};
struct RecFree {
  void operator()(VoskRecognizer* r) const { vosk_recognizer_free(r); }
};
using ModelPtr = std::unique_ptr<VoskModel, ModelFree>;

class VoskStt : public ISpeechRecognizer {
 public:
  VoskStt(ModelPtr m, VoskRecognizer* r, bool grammar) : model_(std::move(m)), rec_(r), grammar_(grammar) {}
  std::string name() const override { return grammar_ ? "vosk-grammar" : "vosk-free"; }
  void begin() override {
    vosk_recognizer_reset(rec_.get());
    r_ = {};
  }
  void accept(std::span<const std::int16_t> a) override {
    // 1 = Vosk decided a sentence ended. A pause inside one transmission is still one
    // utterance, so collect and go on.
    if (vosk_recognizer_accept_waveform_s(rec_.get(), a.data(), static_cast<int>(a.size())) == 1)
      add(vosk_recognizer_result(rec_.get()));
  }
  SttResult finish() override {
    add(vosk_recognizer_final_result(rec_.get()));
    return r_;
  }

 private:
  void add(const char* text) {
    const auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return;
    const std::string t = j.value("text", "");
    if (t.empty()) return;
    if (!r_.text.empty()) r_.text += ' ';
    r_.text += t;
    if (j.contains("result"))
      for (const auto& w : j["result"]) r_.words.push_back({w.value("word", ""), w.value("conf", 0.0)});
  }
  ModelPtr model_;
  std::unique_ptr<VoskRecognizer, RecFree> rec_;  // freed before the model
  bool grammar_;
  SttResult r_;
};

ModelPtr load(const std::string& dir, std::string& error) {
  vosk_set_log_level(-1);
  ModelPtr m(vosk_model_new(dir.c_str()));
  if (!m) error = "cannot load Vosk model " + dir;
  return m;
}
}  // namespace

std::unique_ptr<ISpeechRecognizer> makeVosk(const std::string& dir, const std::vector<std::string>& grammar,
                                            std::string& error) {
  auto m = load(dir, error);
  if (!m) return nullptr;
  VoskRecognizer* r = nullptr;
  if (grammar.empty()) {
    r = vosk_recognizer_new(m.get(), 16000.0f);
  } else {
    // All words in one entry lets them follow each other in any order.
    std::string all;
    for (const auto& w : grammar) {
      if (vosk_model_find_word(m.get(), w.c_str()) < 0) continue;  // reported by --lexicon
      if (!all.empty()) all += ' ';
      all += w;
    }
    r = vosk_recognizer_new_grm(m.get(), 16000.0f, nlohmann::json({all, "[unk]"}).dump().c_str());
  }
  if (!r) {
    error = "cannot create Vosk recogniser";
    return nullptr;
  }
  vosk_recognizer_set_words(r, 1);
  return std::make_unique<VoskStt>(std::move(m), r, !grammar.empty());
}

std::unique_ptr<ISpeechRecognizer> makeVoskSentences(const std::string& dir, const std::vector<std::string>& sentences,
                                                     std::size_t& dropped, std::string& error) {
  auto m = load(dir, error);
  if (!m) return nullptr;
  dropped = 0;
  auto list = nlohmann::json::array();
  for (const auto& s : sentences) {
    std::istringstream in(s);
    bool known = true;
    for (std::string w; in >> w;) known = known && (w == "[unk]" || vosk_model_find_word(m.get(), w.c_str()) >= 0);
    if (known) list.push_back(s);
    else ++dropped;
  }
  if (list.empty()) {
    error = "no usable sentence for the grammar";
    return nullptr;
  }
  list.push_back("[unk]");
  VoskRecognizer* r = vosk_recognizer_new_grm(m.get(), 16000.0f, list.dump().c_str());
  if (!r) {
    error = "cannot create Vosk recogniser";
    return nullptr;
  }
  vosk_recognizer_set_words(r, 1);
  return std::make_unique<VoskStt>(std::move(m), r, true);
}

bool voskMissingWords(const std::string& dir, const std::vector<std::string>& words,
                      std::vector<std::string>& missing, std::string& error) {
  auto m = load(dir, error);
  if (!m) return false;
  for (const auto& w : words)
    if (vosk_model_find_word(m.get(), w.c_str()) < 0) missing.push_back(w);
  return true;
}

}  // namespace atc
