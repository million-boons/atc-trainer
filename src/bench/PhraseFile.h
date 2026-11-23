#pragma once
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace atc {

using Fields = std::map<std::string, std::string>;

struct Phrase {
  std::string text;
  bool negative = false;  // must NOT be accepted as an instruction
  Fields expect;
};

// One phrase per line:   text | callsign=EAGLE-421 runway=09
//                        text | negative
//                        text                      (no scoring information)
// Blank lines and lines starting with # are ignored.
// Returns false and sets error ("line N: ...") on a bad line.
bool readPhrases(std::istream& in, std::vector<Phrase>& out, std::string& error);

}  // namespace atc
