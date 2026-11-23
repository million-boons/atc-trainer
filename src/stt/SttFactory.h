#pragma once
#include <memory>
#include <string>

#include "stt/SpeechRecognizer.h"
#include "stt/SttCatalog.h"

namespace atc {
// backendDir: folder with whisper's CPU variants.
std::unique_ptr<ISpeechRecognizer> makeRecognizer(const SttModel& m, const std::string& backendDir, std::string& error);
}  // namespace atc
