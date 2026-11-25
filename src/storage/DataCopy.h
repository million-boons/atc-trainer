#pragma once
#include <functional>
#include <string>

namespace atc {

// The writable copy of data/phraseology (D-318): <writable>/phraseology, made at the first save. Once it is there,
// every reader uses it instead of the installed folder; it is never merged with newer installed data.
class DataCopy {
 public:
  DataCopy(std::string installed, std::string writable) : installed_(std::move(installed)), dir_(std::move(writable) + "/phraseology") {}
  const std::string& installed() const { return installed_; }
  bool exists() const;
  // The folder every program reads. A damaged copy is still the copy: its errors are shown, never hidden.
  std::string dir() const { return exists() ? dir_ : installed_; }
  // The installed files are not the ones the copy was made from.
  bool installedIsNewer() const;

  // Empty if it may be used; gets a folder with the whole new content. Nothing is changed when it says no.
  using Check = std::function<std::string(const std::string& dir)>;
  // Replaces one file. Empty, or why not.
  std::string write(const std::string& file, const std::string& bytes, const Check& check);

 private:
  std::string copyInto(const std::string& from, const std::string& to) const;
  const std::string installed_, dir_;
};

}  // namespace atc
