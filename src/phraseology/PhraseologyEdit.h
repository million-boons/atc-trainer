#pragma once
#include <string>
#include <vector>

namespace atc {

// Reading and changing data/phraseology for the Phraseology Studio (D-25, D-331). Only a reply's text, an intent's
// examples and their validation are ever changed: never pools, digits, slots or "parse", so recognition stays as it is.
struct PhraseEntry {
  std::string id;
  bool reply = true;                // a pilot reply (one text) or an ATC intent (its examples)
  std::vector<std::string> texts;
  bool validated = false;
  bool placeholderFile = true;      // the whole file is marked placeholder
  std::string sopRef, validatedBy, validatedAt;
  std::string file;                 // relative to the folder: "placeholder.json", "replies/placeholder.json"
};
struct PhraseSet {
  std::vector<PhraseEntry> entries;  // replies first, each in the order of the files
  std::string revision;              // of every file's name and bytes
  const PhraseEntry* find(const std::string& id, bool reply) const;
};
// *.json and replies/*.json, sorted by name: the files that loader and parser read.
std::vector<std::string> phraseologyFiles(const std::string& dir);
bool readPhraseology(const std::string& dir, PhraseSet& out, std::string& error);

struct PhraseSave {
  std::string by, today;  // stored when an entry becomes Validated: who, YYYY-MM-DD
};
// The new bytes of the entry's file. Empty, or why not. A changed text is always saved as Draft (D-25): it is
// validated afterwards, in a save of its own. Key order and indentation are kept; a short list may be broken into lines.
std::string changeEntry(const std::string& bytes, const PhraseEntry& wanted, const PhraseSave& s, std::string& out);
std::string appendReply(const std::string& bytes, const PhraseEntry& added, std::string& out);

// What differs, for review by a person (D-318). JSON.
std::string describeChanges(const PhraseSet& installed, const PhraseSet& copy);

}  // namespace atc
