#pragma once
#include <string>
#include <vector>

namespace atc {

struct SttModel {
  std::string id, name, engine;  // engine: "vosk" or "whisper"
  std::string path;              // absolute
  std::string grammarFile, promptFile;
  std::string note;
  int threads = 4, audioCtx = 0, peakMb = 0;
  bool installed = false;        // path and referenced files exist
};

// Reads config/stt_models.json. Relative paths are taken from root.
// On error returns false and names the model and field.
bool loadSttCatalog(const std::string& jsonFile, const std::string& root, std::vector<SttModel>& out, std::string& error);

// The exact model and settings, as JSON, for every result and log entry (D-39). Includes a
// content hash of the model, grammar and prompt files, so an edited file is visible later.
// Empty if a file cannot be read. Takes a moment for large models: not for the GUI thread.
std::string describeModel(const SttModel& m);

}  // namespace atc
