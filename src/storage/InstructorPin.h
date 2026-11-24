#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace atc {

// The instructor PIN (D-111), stored only as a salted yescrypt hash. Hashing is slow on purpose:
// call hashPin() and checkPin() on a worker thread. Limit: this keeps trainees out of the screens.
// Somebody with a terminal on the same account can still change the files.
bool validPin(std::string_view pin);  // 6 to 12 digits
std::optional<std::string> hashPin(const std::string& pin);
bool checkPin(const std::string& pin, const std::string& hash);
std::optional<std::string> loadPinHash(const std::string& file);  // empty: no PIN set, nobody is an instructor
bool savePinHash(const std::string& file, const std::string& hash);

}  // namespace atc
