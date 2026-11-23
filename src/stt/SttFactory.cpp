#include "stt/SttFactory.h"

#include <cctype>
#include <fstream>
#include <sstream>

#include "stt/VoskStt.h"
#include "stt/WhisperStt.h"

namespace atc {

std::unique_ptr<ISpeechRecognizer> makeRecognizer(const SttModel& m, const std::string& backendDir, std::string& error) {
  if (m.engine == "vosk") {
    std::vector<std::string> words;
    if (!m.grammarFile.empty()) {
      std::ifstream f(m.grammarFile);
      for (std::string l; std::getline(f, l);) {
        while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
        if (!l.empty() && l[0] != '#') words.push_back(l);
      }
      if (words.empty()) {  // must not silently fall back to the full vocabulary
        error = "no words in " + m.grammarFile;
        return nullptr;
      }
    }
    return makeVosk(m.path, words, error);
  }
  WhisperConfig w;
  w.model = m.path;
  w.backendDir = backendDir;
  w.threads = m.threads;
  w.audioCtx = m.audioCtx;
  if (!m.promptFile.empty()) {
    std::ifstream f(m.promptFile);
    std::stringstream s;
    s << f.rdbuf();
    w.prompt = s.str();
    while (!w.prompt.empty() && std::isspace(static_cast<unsigned char>(w.prompt.back()))) w.prompt.pop_back();
    if (w.prompt.empty()) {
      error = "no prompt in " + m.promptFile;
      return nullptr;
    }
  }
  return makeWhisper(w, error);
}

}  // namespace atc
