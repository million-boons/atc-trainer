#pragma once
#include <string>

namespace atc {

// The only source of file locations (D-410): nothing else builds a path from a compiled-in constant.
struct Paths {
  std::string root;      // holds config/ and data/
  std::string config;    // root/config
  std::string data;      // root/data
  std::string models;    // root, unless the config file names another folder
  std::string voices;    // root/voices, unless the config file names another folder
  std::string engines;   // <prefix>/lib/ggml: the whisper CPU variants, always beside the executable
  std::string writable;  // logs, results, recordings; never inside root
};

// What the lookup depends on; tests fill it with temporary folders.
struct PathInputs {
  std::string exe;   // full path of the running program
  std::string home;  // the user's home folder
  std::string etc = "/etc";
};

PathInputs systemPathInputs();                        // /proc/self/exe and HOME
std::string engineFolder(const std::string& exe);     // <prefix>/lib/ggml for <prefix>/bin/<program>

// Root, first match wins: root= in HOME/.config/atc-trainer/paths.conf, then in etc/atc-trainer/paths.conf,
// then <prefix>/bin -> <prefix>/share/atc-trainer. A location named in a file but missing is an error
// that names it and never falls through. If nothing matches, the error lists every place tried.
bool resolvePaths(const PathInputs& in, Paths& out, std::string& error);

}  // namespace atc
