#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace atc {

struct SttWord {
  std::string text;
  double conf = 0;  // engine's own scale: not comparable between engines
};
struct SttResult {
  std::string text;
  std::vector<SttWord> words;
};

// One utterance at a time: begin, accept..., finish. 16 kHz mono.
class ISpeechRecognizer {
 public:
  virtual ~ISpeechRecognizer() = default;
  virtual std::string name() const = 0;
  virtual void begin() = 0;
  virtual void accept(std::span<const std::int16_t> audio) = 0;
  virtual SttResult finish() = 0;
};

}  // namespace atc
