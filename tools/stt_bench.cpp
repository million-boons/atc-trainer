// Speech engine benchmark on recorded transmissions (docs/architecture/02-speech-ptt.md 3.1).
//   stt_bench --engine vosk|whisper --model PATH --manifest FILE [--manifest FILE ...] --out PREFIX
//     vosk:    [--grammar WORDS.txt | --sentences SENTENCES.txt]   (phrase_gen makes the second)
//     [--shape]  a result with impossible field lengths is not accepted
//     whisper: [--threads N] [--audio-ctx N] [--prompt TEXT] [--backends DIR]
//     [--fast]  feed without real-time pacing (latency and CPU are then not valid)
//   stt_bench --lexicon WORDS.txt --model VOSK_DIR     words the model does not know
//   stt_bench --check PHRASES.txt                      check a phrase file
// One engine per run, so that memory is that engine's alone.
#include <sys/resource.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

#include "audio/WavFile.h"
#include "bench/PhraseFile.h"
#include "bench/Scoring.h"
#include "core/Time.h"
#include "stt/VoskStt.h"
#include "stt/WhisperStt.h"
using namespace atc;
using nlohmann::json;

namespace {
bool gShape = false;
const double kThresholds[] = {0.0, 0.5, 0.7, 0.8, 0.9, 0.95};

long procKb(const char* key) {
  std::ifstream f("/proc/self/status");
  for (std::string l; std::getline(f, l);)
    if (l.rfind(key, 0) == 0) return std::strtol(l.c_str() + std::string(key).size() + 1, nullptr, 10);
  return 0;
}
double cpuSeconds() {
  rusage u{};
  getrusage(RUSAGE_SELF, &u);
  return double(u.ru_utime.tv_sec + u.ru_stime.tv_sec) + 1e-6 * double(u.ru_utime.tv_usec + u.ru_stime.tv_usec);
}
std::vector<std::string> readWords(const std::string& path, bool& ok) {
  std::vector<std::string> v;
  std::ifstream f(path);
  ok = bool(f);
  for (std::string l; std::getline(f, l);) {
    while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
    if (!l.empty() && l[0] != '#') v.push_back(l);
  }
  return v;
}
std::string csv(const std::string& s) {
  std::string o = "\"";
  for (char c : s) o += c == '"' ? std::string("\"\"") : std::string(1, c);
  return o + "\"";
}
std::string str(const Fields& f) {
  std::string o;
  for (const auto& [k, v] : f) o += (o.empty() ? "" : " ") + k + "=" + v;
  return o;
}

struct Item {
  std::string wav, speaker;
  Phrase phrase;
  int pre = 0, tail = 0;  // samples
};
struct Row {
  Item item;
  SttResult hyp;
  Extracted ex;
  EditCounts edits;
  double latencyMs = 0, audioS = 0, cpuS = 0, backlogMs = 0;
};
struct Stats {
  int scripted = 0, negatives = 0, fieldsRight = 0, fieldsTotal = 0;
  EditCounts edits;
  std::map<std::string, std::pair<int, int>> byField;  // right, total
  std::map<double, std::map<Outcome, int>> out;
  std::vector<double> latency;
};

bool loadManifest(const std::string& path, std::vector<Item>& items, std::map<std::string, int>& skipped) {
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "cannot read %s\n", path.c_str());
    return false;
  }
  const auto dir = std::filesystem::path(path).parent_path();
  int n = 0;
  for (std::string l; std::getline(f, l);) {
    ++n;
    if (l.empty()) continue;
    const json j = json::parse(l, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("file")) {
      std::fprintf(stderr, "%s line %d: not a valid entry\n", path.c_str(), n);
      return false;
    }
    if (!j.value("usable", false)) {
      ++skipped["not usable"];
      continue;
    }
    if (!j.contains("transcript") || !j["transcript"].is_string()) {
      ++skipped["no phrase"];
      continue;
    }
    Item it;
    it.wav = (dir / j["file"].get<std::string>()).string();
    it.speaker = j.value("speaker", "?");
    it.phrase.text = j["transcript"];
    it.phrase.negative = j.value("kind", "") == "negative";
    if (j.contains("expect") && j["expect"].is_object()) it.phrase.expect = j["expect"].get<Fields>();
    if (!it.phrase.negative && it.phrase.expect.empty()) {
      ++skipped["no expected fields"];
      continue;
    }
    it.pre = j.value("preRollSamples", 0);
    it.tail = j.value("tailSamples", 3200);
    items.push_back(std::move(it));
  }
  return true;
}

// Feeds one transmission as the trainer would receive it: pre-roll at key-down, the rest as it
// is spoken. Latency counts from pedal release to text, so it includes the tail.
Row runOne(ISpeechRecognizer& stt, const Item& item, const WavData& w, bool fast) {
  Row r;
  r.item = item;
  const std::size_t n = w.samples.size(), chunk = 1600;  // 100 ms
  const auto at = [&](std::size_t samples) { return Ns(samples) * 1'000'000'000 / w.rate; };
  const double cpu0 = cpuSeconds();
  const Ns t0 = monotonicNow();
  stt.begin();
  for (std::size_t i = 0; i < n; i += chunk) {
    const std::size_t end = std::min(n, i + chunk);
    const Ns due = t0 + (end > std::size_t(item.pre) ? at(end - item.pre) : 0);
    const Ns now = monotonicNow();
    if (!fast && now < due) std::this_thread::sleep_for(std::chrono::nanoseconds(due - now));
    if (!fast) r.backlogMs = std::max(r.backlogMs, double(monotonicNow() - due) / 1e6);
    stt.accept({w.samples.data() + i, end - i});
  }
  r.hyp = stt.finish();
  const Ns done = monotonicNow();
  const auto held = Ns(n) - item.pre - item.tail;
  r.latencyMs = double(done - t0 - (held > 0 ? at(std::size_t(held)) : 0)) / 1e6;
  r.cpuS = cpuSeconds() - cpu0;
  r.audioS = double(n) / w.rate;
  return r;
}

void addStats(Stats& s, const Row& r) {
  for (double t : kThresholds) ++s.out[t][judge(r.item.phrase, r.ex, t, gShape)];
  s.latency.push_back(r.latencyMs);
  if (r.item.phrase.negative) {
    ++s.negatives;
    return;
  }
  ++s.scripted;
  s.edits.sub += r.edits.sub, s.edits.del += r.edits.del, s.edits.ins += r.edits.ins, s.edits.ref += r.edits.ref;
  for (const auto& [k, v] : r.item.phrase.expect) {
    const auto it = r.ex.fields.find(k);
    const bool right = it != r.ex.fields.end() && it->second == v;
    ++s.fieldsTotal, ++s.byField[k].second;
    if (right) ++s.fieldsRight, ++s.byField[k].first;
  }
}
double pct(int a, int b) { return b ? 100.0 * a / b : 0.0; }
json toJson(const Stats& s, bool fast) {
  json j{{"scripted", s.scripted}, {"negatives", s.negatives}, {"werPct", pct(s.edits.errors(), s.edits.ref)},
         {"fieldAccuracyPct", pct(s.fieldsRight, s.fieldsTotal)}};
  if (!fast) j["latencyMs"] = {{"p50", percentile(s.latency, 50)}, {"p95", percentile(s.latency, 95)}, {"max", percentile(s.latency, 100)}};
  for (const auto& [k, v] : s.byField) j["fields"][k] = {{"right", v.first}, {"total", v.second}};
  for (const auto& [t, o] : s.out) {
    const auto c = [&](Outcome x) { const auto i = o.find(x); return i == o.end() ? 0 : i->second; };
    j["byThreshold"].push_back({{"threshold", t}, {"correct", c(Outcome::Correct)}, {"incomplete", c(Outcome::Incomplete)},
                                {"wrongAccepted", c(Outcome::Wrong)}, {"rejected", c(Outcome::Rejected)},
                                {"falseAccept", c(Outcome::FalseAccept)}});
  }
  return j;
}
void print(const std::string& title, const Stats& s, bool fast) {
  std::printf("\n%s: %d scripted, %d negative\n", title.c_str(), s.scripted, s.negatives);
  std::printf("  word error rate %.1f %%   critical fields right %.1f %% (%d/%d)\n", pct(s.edits.errors(), s.edits.ref),
              pct(s.fieldsRight, s.fieldsTotal), s.fieldsRight, s.fieldsTotal);
  for (const auto& [k, v] : s.byField) std::printf("    %-10s %d/%d\n", k.c_str(), v.first, v.second);
  if (!fast)
    std::printf("  release to text: median %.0f ms, p95 %.0f ms, max %.0f ms\n", percentile(s.latency, 50),
                percentile(s.latency, 95), percentile(s.latency, 100));
  std::printf("  threshold  correct  incomplete  WRONG ACCEPTED  rejected  FALSE ACCEPT\n");
  for (const auto& [t, o] : s.out) {
    const auto c = [&](Outcome x) { const auto i = o.find(x); return i == o.end() ? 0 : i->second; };
    std::printf("  %9.2f  %7d  %10d  %14d  %8d  %12d\n", t, c(Outcome::Correct), c(Outcome::Incomplete), c(Outcome::Wrong),
                c(Outcome::Rejected), c(Outcome::FalseAccept));
  }
}
}  // namespace

int main(int argc, char** argv) {
  std::string engine, model, out, grammarFile, sentenceFile, lexicon, check;
  std::vector<std::string> manifests;
  WhisperConfig wc;
  wc.backendDir = ATC_GGML_DIR;
  bool fast = false;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (k == "--engine") engine = next();
    else if (k == "--model") model = next();
    else if (k == "--manifest") manifests.push_back(next());
    else if (k == "--out") out = next();
    else if (k == "--grammar") grammarFile = next();
    else if (k == "--sentences") sentenceFile = next();
    else if (k == "--shape") gShape = true;
    else if (k == "--lexicon") lexicon = next();
    else if (k == "--check") check = next();
    else if (k == "--threads") wc.threads = std::atoi(next().c_str());
    else if (k == "--audio-ctx") wc.audioCtx = std::atoi(next().c_str());
    else if (k == "--prompt") wc.prompt = next();
    else if (k == "--backends") wc.backendDir = next();
    else if (k == "--fast") fast = true;
    else {
      std::fprintf(stderr, "unknown option %s\n", k.c_str());
      return 2;
    }
  }
  std::string error;
  if (!check.empty()) {
    std::ifstream f(check);
    std::vector<Phrase> p;
    if (!f || !readPhrases(f, p, error)) {
      std::fprintf(stderr, "%s: %s\n", check.c_str(), f ? error.c_str() : "cannot read");
      return 1;
    }
    // Each scripted phrase, read perfectly, must score as correct.
    std::set<std::string> names;
    for (const auto& x : p)
      if (x.expect.count("callsign")) names.insert(normaliseText(x.expect.at("callsign"))[0]);
    int bad = 0, neg = 0;
    for (const auto& x : p) {
      std::vector<SttWord> w;
      for (auto& s : normaliseText(x.text)) w.push_back({s, 1.0});
      const auto o = judge(x, extractFields(w, names), 0.0);
      neg += x.negative;
      if (x.expect.empty() && !x.negative) continue;
      if (o != Outcome::Correct && o != Outcome::NegativeOk) {
        ++bad;
        std::printf("MISMATCH: %s\n  expected %s\n  scored   %s\n", x.text.c_str(), str(x.expect).c_str(), str(extractFields(w, names).fields).c_str());
      }
    }
    std::printf("%zu phrases, %d negative, %d mismatched\n", p.size(), neg, bad);
    return bad ? 1 : 0;
  }
  bool ok = true;
  if (!lexicon.empty()) {
    const auto words = readWords(lexicon, ok);
    std::vector<std::string> missing;
    if (!ok || !voskMissingWords(model, words, missing, error)) {
      std::fprintf(stderr, "%s\n", ok ? error.c_str() : "cannot read word list");
      return 1;
    }
    std::printf("%zu words, %zu missing from the model\n", words.size(), missing.size());
    for (const auto& w : missing) std::printf("  %s\n", w.c_str());
    return 0;
  }
  if ((engine != "vosk" && engine != "whisper") || model.empty() || manifests.empty() || out.empty()) {
    std::fprintf(stderr, "need --engine vosk|whisper --model PATH --manifest FILE --out PREFIX (or --lexicon / --check)\n");
    return 2;
  }

  std::vector<Item> items;
  std::map<std::string, int> skipped;
  for (const auto& m : manifests)
    if (!loadManifest(m, items, skipped)) return 1;
  for (const auto& [why, c] : skipped) std::printf("skipped %d recordings: %s\n", c, why.c_str());
  if (items.empty()) {
    std::fprintf(stderr, "nothing to test\n");
    return 1;
  }
  std::set<std::string> names;
  for (const auto& it : items)
    if (it.phrase.expect.count("callsign")) names.insert(normaliseText(it.phrase.expect.at("callsign"))[0]);

  std::vector<std::string> grammar;
  if (!grammarFile.empty()) {
    grammar = readWords(grammarFile, ok);
    if (!ok || grammar.empty()) {
      std::fprintf(stderr, "cannot use %s\n", grammarFile.c_str());
      return 1;
    }
  }
  const long rss0 = procKb("VmRSS:");
  const Ns l0 = monotonicNow();
  wc.model = model;
  std::vector<std::string> sentences;
  if (!sentenceFile.empty()) {
    sentences = readWords(sentenceFile, ok);
    if (!ok || sentences.empty() || engine != "vosk" || !grammarFile.empty()) {
      std::fprintf(stderr, "cannot use --sentences %s\n", sentenceFile.c_str());
      return 1;
    }
  }
  std::size_t dropped = 0;
  auto stt = engine != "vosk"      ? makeWhisper(wc, error)
             : !sentences.empty() ? makeVoskSentences(model, sentences, dropped, error)
                                  : makeVosk(model, grammar, error);
  if (dropped) std::printf("grammar: %zu of %zu sentences left out (word unknown to the model)\n", dropped, sentences.size());
  if (!stt) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  const double loadMs = double(monotonicNow() - l0) / 1e6;
  const long rssLoaded = procKb("VmRSS:");

  Stats all;
  std::map<std::string, Stats> bySpeaker;
  std::ofstream rows(out + ".csv");
  rows << "file,speaker,kind,reference,heard,expected,found,minConf,conflict,outcomeNoThreshold,subs,dels,ins,refWords,latencyMs,backlogMs,audioS,cpuS\n";
  static const char* const oname[] = {"correct", "incomplete", "WRONG", "rejected", "FALSE_ACCEPT", "negative_ok"};
  double audio = 0, cpu = 0, maxBacklog = 0;
  std::size_t done = 0;
  long rssPeakSteady = 0;
  for (const auto& it : items) {
    const auto w = readWav(it.wav);
    if (!w || w->rate != 16000) {
      std::fprintf(stderr, "cannot use %s\n", it.wav.c_str());
      return 1;
    }
    Row r = runOne(*stt, it, *w, fast);
    const auto norm = normaliseWords(r.hyp.words);
    std::vector<std::string> hyp;
    for (const auto& x : norm) hyp.push_back(x.text);
    r.ex = extractFields(norm, names);
    r.edits = editCounts(normaliseText(it.phrase.text), hyp);
    addStats(all, r);
    addStats(bySpeaker[it.speaker], r);
    audio += r.audioS, cpu += r.cpuS, maxBacklog = std::max(maxBacklog, r.backlogMs);
    rssPeakSteady = std::max(rssPeakSteady, procKb("VmRSS:"));
    const auto o = judge(it.phrase, r.ex, 0.0, gShape);
    rows << csv(it.wav) << ',' << csv(it.speaker) << ',' << (it.phrase.negative ? "negative" : "scripted") << ','
         << csv(it.phrase.text) << ',' << csv(r.hyp.text) << ',' << csv(str(it.phrase.expect)) << ',' << csv(str(r.ex.fields)) << ','
         << r.ex.minConf << ',' << r.ex.conflict << ',' << oname[int(o)] << ',' << r.edits.sub << ',' << r.edits.del << ','
         << r.edits.ins << ',' << r.edits.ref << ',' << (fast ? std::string() : std::to_string(r.latencyMs)) << ','
         << (fast ? std::string() : std::to_string(r.backlogMs)) << ',' << r.audioS << ',' << r.cpuS << '\n';
    std::fprintf(stderr, "\r%s %zu/%zu", stt->name().c_str(), ++done, items.size());
  }
  const std::string cpuInfo = engine == "whisper" ? whisperCpuFeatures() : "";
  const std::string label = stt->name();
  std::printf("\n%s  %s\n", label.c_str(), model.c_str());
  if (!cpuInfo.empty()) std::printf("CPU variant: %s\n", cpuInfo.c_str());
  std::printf("load %.0f ms; memory: start %ld, loaded %ld, highest %ld MB; CPU %.2f s per s of audio%s\n", loadMs, rss0 / 1024,
              rssLoaded / 1024, procKb("VmHWM:") / 1024, cpu / audio, fast ? "  (--fast: latency not measured)" : "");
  if (!fast) std::printf("largest feed backlog %.0f ms\n", maxBacklog);
  print("ALL", all, fast);
  if (bySpeaker.size() > 1)
    for (const auto& [k, s] : bySpeaker) print("speaker " + k, s, fast);

  json j{{"engine", label}, {"model", model}, {"grammar", grammarFile}, {"sentences", sentenceFile}, {"sentencesDropped", dropped}, {"shape", gShape}, {"prompt", wc.prompt}, {"threads", wc.threads}, {"audioCtx", wc.audioCtx},
         {"cpuVariant", cpuInfo}, {"realTime", !fast}, {"loadMs", loadMs}, {"rssStartKb", rss0}, {"rssLoadedKb", rssLoaded},
         {"rssHighestBetweenKb", rssPeakSteady}, {"rssPeakKb", procKb("VmHWM:")}, {"audioSeconds", audio}, {"cpuPerAudioSecond", cpu / audio},
         {"maxBacklogMs", maxBacklog}, {"skipped", skipped}, {"all", toJson(all, fast)}};
  for (const auto& [k, s] : bySpeaker) j["bySpeaker"][k] = toJson(s, fast);
  std::ofstream jf(out + ".json");
  jf << j.dump(2) << '\n';
  rows.flush();
  jf.flush();
  if (!rows || !jf) {
    std::fprintf(stderr, "cannot write %s.csv / .json\n", out.c_str());
    return 1;
  }
  std::printf("\nwritten %s.csv and %s.json\n", out.c_str(), out.c_str());
  return 0;
}
