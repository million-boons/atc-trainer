#include "storage/InstructorPin.h"

#include <crypt.h>

#include <cstdlib>
#include <fstream>

#include "storage/PrivateFile.h"

namespace atc {
namespace {
std::optional<std::string> crypted(const std::string& pin, const std::string& setting) {
  void* data = nullptr;
  int size = 0;
  const char* r = crypt_ra(pin.c_str(), setting.c_str(), &data, &size);
  std::optional<std::string> out;
  if (r && r[0] == '$') out = r;
  std::free(data);
  return out;
}
}  // namespace

bool validPin(std::string_view pin) {
  if (pin.size() < 6 || pin.size() > 12) return false;
  for (char c : pin)
    if (c < '0' || c > '9') return false;
  return true;
}

std::optional<std::string> hashPin(const std::string& pin) {
  if (!validPin(pin)) return std::nullopt;
  char* s = crypt_gensalt_ra("$y$", 0, nullptr, 0);
  if (!s) return std::nullopt;
  const std::string salt(s);
  std::free(s);
  return crypted(pin, salt);
}

bool checkPin(const std::string& pin, const std::string& hash) {
  if (!validPin(pin) || hash.rfind("$y$", 0) != 0) return false;
  const auto got = crypted(pin, hash);
  if (!got || got->size() != hash.size()) return false;
  unsigned char diff = 0;  // same time wherever the difference is
  for (std::size_t i = 0; i < hash.size(); ++i) diff = static_cast<unsigned char>(diff | ((*got)[i] ^ hash[i]));
  return diff == 0;
}

std::optional<std::string> loadPinHash(const std::string& file) {
  std::ifstream i(file);
  std::string h;
  if (!std::getline(i, h) || h.rfind("$y$", 0) != 0) return std::nullopt;
  return h;
}

bool savePinHash(const std::string& file, const std::string& hash) { return writePrivateFile(file, hash + "\n"); }

}  // namespace atc
