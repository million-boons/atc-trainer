#pragma once
#include <memory>
#include <string>
#include <vector>

#include "stt/SpeechRecognizer.h"

namespace atc {

// grammar empty: full vocabulary. Otherwise only these words, in any order, plus [unk].
std::unique_ptr<ISpeechRecognizer> makeVosk(const std::string& modelDir, const std::vector<std::string>& grammar,
                                            std::string& error);
// Grammar from example sentences: Vosk estimates a bigram model from them, so word order counts.
// Other orders stay possible but cost more. Sentences with a word the model does not know are
// left out and counted in dropped. "[unk]" is always added as an entry of its own.
std::unique_ptr<ISpeechRecognizer> makeVoskSentences(const std::string& modelDir, const std::vector<std::string>& sentences,
                                                     std::size_t& dropped, std::string& error);
// Words the model does not know. Vosk drops them from a grammar without an error.
bool voskMissingWords(const std::string& modelDir, const std::vector<std::string>& words,
                      std::vector<std::string>& missing, std::string& error);

}  // namespace atc
