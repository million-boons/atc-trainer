// Text to instruction, without speech or scenario (package 2e). Placeholder phraseology.
//   parse_cli [--rules FILE] TEXT...           one result as JSON
//   parse_cli [--rules FILE] < LINES           one JSON result per line
//   parse_cli [--rules FILE] --score RUN.csv   a stt_bench run: the text heard against the text read.
//     The text read is parsed too. WRONG = accepted with anything different from it.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/PathResolver.h"
#include "parser/Parser.h"
using namespace atc;

namespace {
nlohmann::json toJson(const ParsedInstruction& p) {
  nlohmann::json f = nlohmann::json::object();
  for (const auto& [k, v] : p.fields) f[k] = {{"value", v.value}, {"heard", v.heard}, {"uncertain", v.uncertain}, {"confidence", v.confidence}};
  return {{"class", toString(p.cls)}, {"intent", p.intent}, {"fields", f}, {"missing", p.missing}, {"reasons", p.reasons}, {"normalised", p.normalisedText}};
}
std::vector<std::string> csvRow(const std::string& line) {
  std::vector<std::string> out{""};
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"' && quoted && i + 1 < line.size() && line[i + 1] == '"') out.back() += line[++i];
    else if (line[i] == '"') quoted = !quoted;
    else if (line[i] == ',' && !quoted) out.emplace_back();
    else out.back() += line[i];
  }
  return out;
}
SttResult text(const std::string& s) { return {s, {}}; }
bool same(const ParsedInstruction& a, const ParsedInstruction& b) {
  if (a.intent != b.intent || a.fields.size() != b.fields.size()) return false;
  for (const auto& [k, v] : a.fields)
    if (const auto it = b.fields.find(k); it == b.fields.end() || it->second.value != v.value) return false;
  return true;
}
int score(const std::string& file, const ParserRules& rules) {
  std::ifstream f(file);
  std::string line;
  if (!std::getline(f, line)) return std::fprintf(stderr, "cannot read %s\n", file.c_str()), 1;
  std::map<std::string, std::size_t> col;
  const auto head = csvRow(line);
  for (std::size_t i = 0; i < head.size(); ++i) col[head[i]] = i;
  if (!col.count("reference") || !col.count("heard")) return std::fprintf(stderr, "no reference and heard columns\n"), 1;
  int n = 0, right = 0, wrong = 0, other = 0, falseAccept = 0;
  std::map<std::string, int> again;
  while (std::getline(f, line)) {
    const auto r = csvRow(line);
    if (r.size() < head.size()) return std::fprintf(stderr, "bad row: %s\n", line.c_str()), 1;
    const auto want = parseInstruction(text(r[col["reference"]]), rules), got = parseInstruction(text(r[col["heard"]]), rules);
    const bool taken = got.cls == RecognitionClass::Recognized;
    const char* verdict = "";
    if (want.cls != RecognitionClass::Recognized) {
      ++other;
      if (taken) ++falseAccept, verdict = "FALSE ACCEPT";
    } else {
      ++n;
      if (!taken) ++again[std::string(toString(got.cls))];
      else if (same(want, got)) ++right;
      else ++wrong, verdict = "WRONG";
    }
    if (*verdict || (want.cls == RecognitionClass::Recognized && !taken)) {
      std::string why;
      for (const auto& x : got.reasons) why += (why.empty() ? "" : "; ") + x;
      std::printf("%-12s read:  %s\n             heard: %s\n             %s: %s\n", *verdict ? verdict : "say again", r[col["reference"]].c_str(),
                  r[col["heard"]].c_str(), std::string(toString(got.cls)).c_str(), why.c_str());
    }
  }
  std::printf("\n%d instructions in the three intents: %d right, %d WRONG, say again %d", n, right, wrong, n - right - wrong);
  for (const auto& [k, v] : again) std::printf(" (%s %d)", k.c_str(), v);
  std::printf("\n%d other transmissions: %d falsely accepted\n", other, falseAccept);
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  std::string file, csv, words, error;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--rules" && i + 1 < argc) file = argv[++i];
    else if (k == "--score" && i + 1 < argc) csv = argv[++i];
    else if (k.rfind("--", 0) == 0) return std::fprintf(stderr, "usage: parse_cli [--rules FILE] [--score RUN.csv | TEXT...]\n"), 2;
    else words += (words.empty() ? "" : " ") + k;
  }
  if (file.empty()) {
    Paths p;
    if (!resolvePaths(systemPathInputs(), p, error)) return std::fprintf(stderr, "%s\n", error.c_str()), 1;
    file = p.data + "/phraseology/placeholder.json";
  }
  ParserRules rules;
  if (!loadParserRules(file, rules, error)) return std::fprintf(stderr, "%s\n", error.c_str()), 1;
  if (rules.placeholder) std::fprintf(stderr, "note: PLACEHOLDER phraseology, not approved\n");
  if (!csv.empty()) return score(csv, rules);
  if (!words.empty()) return std::puts(toJson(parseInstruction(text(words), rules)).dump().c_str()), 0;
  for (std::string l; std::getline(std::cin, l);) std::puts(toJson(parseInstruction(text(l), rules)).dump().c_str());
  return 0;
}
