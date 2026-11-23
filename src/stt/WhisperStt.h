#pragma once
#include <memory>
#include <string>

#include "stt/SpeechRecognizer.h"

namespace atc {

struct WhisperConfig {
  std::string model;
  std::string backendDir;  // where the CPU variants are
  int threads = 2;
  int audioCtx = 0;        // 0 = full 30 s window; 1500 = 30 s, so 50 per second
  std::string prompt;      // vocabulary bias
};
std::unique_ptr<ISpeechRecognizer> makeWhisper(const WhisperConfig& cfg, std::string& error);
std::string whisperCpuFeatures();  // what the loaded CPU variant uses

}  // namespace atc
