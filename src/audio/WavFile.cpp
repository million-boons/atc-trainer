#include "audio/WavFile.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace atc {
namespace {
static_assert(std::endian::native == std::endian::little);
template <class T>
void put(std::ostream& o, T v) { o.write(reinterpret_cast<const char*>(&v), sizeof v); }
template <class T>
bool get(std::istream& i, T& v) { return bool(i.read(reinterpret_cast<char*>(&v), sizeof v)); }
}  // namespace

bool writeWav(const std::string& path, int rate, std::span<const std::int16_t> s) {
  const std::string tmp = path + ".part";
  {
    std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
    const auto bytes = static_cast<std::uint32_t>(s.size_bytes());
    o.write("RIFF", 4);
    put<std::uint32_t>(o, 36 + bytes);
    o.write("WAVEfmt ", 8);
    put<std::uint32_t>(o, 16);
    put<std::uint16_t>(o, 1);
    put<std::uint16_t>(o, 1);
    put<std::uint32_t>(o, static_cast<std::uint32_t>(rate));
    put<std::uint32_t>(o, static_cast<std::uint32_t>(rate) * 2);
    put<std::uint16_t>(o, 2);
    put<std::uint16_t>(o, 16);
    o.write("data", 4);
    put<std::uint32_t>(o, bytes);
    o.write(reinterpret_cast<const char*>(s.data()), static_cast<std::streamsize>(bytes));
    o.flush();
    if (!o) {
      std::remove(tmp.c_str());
      return false;
    }
  }
  return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::optional<WavData> readWav(const std::string& path) {
  std::error_code ec;
  const auto fileSize = std::filesystem::file_size(path, ec);
  std::ifstream i(path, std::ios::binary);
  char id[4], wave[4];
  std::uint32_t size = 0;
  if (ec || !i.read(id, 4) || !get(i, size) || !i.read(wave, 4) || std::memcmp(id, "RIFF", 4) ||
      std::memcmp(wave, "WAVE", 4))
    return std::nullopt;
  WavData w;
  while (i.read(id, 4) && get(i, size)) {
    if (!std::memcmp(id, "fmt ", 4)) {
      std::uint16_t fmt = 0, ch = 0, align = 0, bits = 0;
      std::uint32_t rate = 0, bps = 0;
      if (size < 16 || !get(i, fmt) || !get(i, ch) || !get(i, rate) || !get(i, bps) || !get(i, align) || !get(i, bits))
        return std::nullopt;
      if (fmt != 1 || ch != 1 || bits != 16) return std::nullopt;
      w.rate = static_cast<int>(rate);
      i.seekg(static_cast<std::streamoff>(size - 16 + (size & 1)), std::ios::cur);
    } else if (!std::memcmp(id, "data", 4)) {
      if (w.rate == 0 || size > fileSize) return std::nullopt;  // never trust the header's size
      w.samples.resize(size / 2);
      if (!i.read(reinterpret_cast<char*>(w.samples.data()), static_cast<std::streamsize>(size / 2 * 2)))
        return std::nullopt;
      return w;
    } else {
      i.seekg(static_cast<std::streamoff>(size + (size & 1)), std::ios::cur);
    }
  }
  return std::nullopt;
}

}  // namespace atc
