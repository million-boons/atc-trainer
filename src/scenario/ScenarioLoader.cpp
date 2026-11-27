#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <nlohmann/json.hpp>

#include "scenario/Scenario.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
using nlohmann::json;

struct Report {
  std::vector<LoadError>& errors;
  std::uint64_t hash = 14695981039346656037ull;  // FNV-1a of every file read
  void add(const std::string& bytes) {
    for (const unsigned char c : bytes) hash = (hash ^ c) * 1099511628211ull;
  }
};

// One JSON value with its place, so every error can name file and path.
struct Node {
  const json& j;
  std::string file, path;
  Report& r;

  void fail(const std::string& what) const { r.errors.push_back({file, path.empty() ? "/" : path, what}); }
  bool has(const char* key) const { return j.is_object() && j.contains(key); }
  Node child(const std::string& key) const { return {j.at(key), file, path + "/" + key, r}; }
  Node item(std::size_t i) const { return {j.at(i), file, path + "/" + std::to_string(i), r}; }

  // An object with all required and no unknown members.
  bool object(std::initializer_list<const char*> required, std::initializer_list<const char*> optional = {}) const {
    if (!j.is_object()) {
      fail("must be an object");
      return false;
    }
    for (const char* k : required)
      if (!j.contains(k)) fail(std::string("missing ") + k);
    for (const auto& [k, v] : j.items()) {
      const auto is = [&k = k](const char* x) { return k == x; };
      if (!std::any_of(required.begin(), required.end(), is) && !std::any_of(optional.begin(), optional.end(), is))
        child(k).fail("unknown name");
    }
    return true;
  }
  // The members below report a wrong type; a missing one is reported by object().
  std::string text(const char* key) const {
    if (!has(key)) return {};
    const Node n = child(key);
    if (n.j.is_string() && !n.j.get_ref<const std::string&>().empty()) return n.j.get<std::string>();
    n.fail("must be a text that is not empty");
    return {};
  }
  double number(const char* key, double min, double missing, bool whole) const {
    if (!has(key)) return missing;
    const Node n = child(key);
    if (n.j.is_number() && (!whole || n.j.is_number_integer()) && n.j.get<double>() >= min && n.j.get<double>() <= 86400)
      return n.j.get<double>();
    n.fail(std::string("must be a ") + (whole ? "whole " : "") + "number from " + std::to_string(static_cast<int>(min)) + " to 86400");
    return missing;
  }
  bool flag(const char* key) const {
    if (!has(key)) return false;
    if (child(key).j.is_boolean()) return child(key).j.get<bool>();
    child(key).fail("must be true or false");
    return false;
  }
  void each(const char* key, bool mayBeEmpty, const std::function<void(const Node&)>& f) const {
    if (!has(key)) return;
    const Node n = child(key);
    if (!n.j.is_array()) return n.fail("must be a list");
    if (n.j.empty() && !mayBeEmpty) return n.fail("must not be empty");
    for (std::size_t i = 0; i < n.j.size(); ++i) f(n.item(i));
  }
  std::vector<std::string> texts(const char* key, bool mayBeEmpty) const {
    std::vector<std::string> out;
    each(key, mayBeEmpty, [&](const Node& n) {
      if (!n.j.is_string() || n.j.get_ref<const std::string&>().empty()) return n.fail("must be a text that is not empty");
      out.push_back(n.j.get<std::string>());
    });
    return out;
  }
  void version() const {
    if (has("schemaVersion") && child("schemaVersion").j != json(1)) child("schemaVersion").fail("must be 1");
  }
};

bool chars(const std::string& s, const char* allowed) { return !s.empty() && s.find_first_not_of(allowed) == std::string::npos; }
constexpr const char* kIdChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
constexpr const char* kSlotChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

bool isRunway(const std::string& s) {
  if (s.size() != 2 && s.size() != 3) return false;
  if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') return false;
  const int n = (s[0] - '0') * 10 + (s[1] - '0');
  return n >= 1 && n <= 36 && (s.size() == 2 || s[2] == 'L' || s[2] == 'R' || s[2] == 'C');
}

std::optional<json> read(const std::string& file, Report& r) {
  std::ifstream f(file, std::ios::binary);
  if (!f) {
    r.errors.push_back({file, "/", "cannot read"});
    return std::nullopt;
  }
  const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  r.add(bytes);
  json j = json::parse(bytes, nullptr, false);
  if (j.is_discarded()) {
    r.errors.push_back({file, "/", "not valid JSON"});
    return std::nullopt;
  }
  return j;
}

std::vector<std::string> jsonFiles(const std::string& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".json") out.push_back(it->path().string());
  std::sort(out.begin(), out.end());
  return out;
}

// True unless validated with a complete record.
bool placeholderOf(const Node& parent, const char* open, std::initializer_list<const char*> needed) {
  if (!parent.has("validation")) return true;
  const Node n = parent.child("validation");
  if (n.has("validatedBy")) n.child("validatedBy").fail("is now validated_by, with validated_at (D-429)");
  if (!n.object({"status"}, {"sopRef", "validatedBy", "validated_by", "validated_at"})) return true;
  const std::string status = n.text("status");
  if (status == open || status.empty()) return true;
  if (status != "validated") {
    n.child("status").fail(std::string("must be ") + open + " or validated");
    return true;
  }
  for (const char* k : needed)
    if (n.text(k).empty() && !n.has(k)) n.fail(std::string("validated needs ") + k);
  return false;
}

void loadAirfield(const std::string& file, const std::string& id, Airfield& a, Report& r) {
  const auto j = read(file, r);
  if (!j) return;
  const Node n{*j, file, "", r};
  if (!n.object({"schemaVersion", "id", "validation", "runways", "taxiways", "aircraftTypes", "callsigns"}, {"comment"})) return;
  n.version();
  a.id = n.text("id");
  if (!a.id.empty() && a.id != id) n.child("id").fail("must be " + id + ", as the file name");
  a.placeholder = placeholderOf(n, "placeholder", {"sopRef", "validated_by", "validated_at"});
  std::set<std::string> seen;
  const auto once = [&seen](const Node& at, const std::string& v) {
    if (!v.empty() && !seen.insert(v).second) at.fail(v + " is listed twice");
  };
  const auto known = [](const Node& at, const std::vector<std::string>& values, const std::set<std::string>& in, const char* what) {
    for (const auto& v : values)
      if (!in.count(v)) at.fail(std::string("unknown ") + what + " " + v);
    return std::set<std::string>(values.begin(), values.end());
  };
  n.each("runways", false, [&](const Node& x) {
    const std::string v = x.j.is_string() ? x.j.get<std::string>() : "";
    if (!isRunway(v)) return x.fail("must be a runway such as 09 or 27L");
    once(x, v);
    a.runways.push_back(v);
  });
  const std::set<std::string> runways(a.runways.begin(), a.runways.end());
  seen.clear();
  n.each("taxiways", true, [&](const Node& x) {
    if (!x.object({"id", "serves"})) return;
    Airfield::Taxiway t;
    t.id = x.text("id");
    once(x, t.id);
    const auto serves = x.texts("serves", false);
    if (x.has("serves")) t.serves = known(x.child("serves"), serves, runways, "runway");
    a.taxiways.push_back(std::move(t));
  });
  seen.clear();
  std::set<std::string> types;
  n.each("aircraftTypes", false, [&](const Node& x) {
    if (!x.object({"id"}, {"categories"})) return;
    Airfield::AircraftType t;
    t.id = x.text("id");
    once(x, t.id);
    for (auto& c : x.texts("categories", true)) t.categories.insert(std::move(c));
    types.insert(t.id);
    a.aircraftTypes.push_back(std::move(t));
  });
  seen.clear();
  n.each("callsigns", false, [&](const Node& x) {
    if (!x.object({"id", "spoken"}, {"types"})) return;
    Airfield::Callsign c;
    c.id = x.text("id");
    c.spoken = x.text("spoken");
    once(x, c.id);
    if (x.has("types")) c.types = known(x.child("types"), x.texts("types", true), types, "aircraft type");
    a.callsigns.push_back(std::move(c));
  });
}

// Intents and their slots from data/phraseology. The speech session checks the rest of these files.
void loadIntents(const std::string& dir, std::map<std::string, IntentInfo>& out, Report& r) {
  const auto files = jsonFiles(dir);
  if (files.empty()) r.errors.push_back({dir, "/", "no phraseology files"});
  for (const auto& file : files) {
    const auto j = read(file, r);
    if (!j) continue;
    const Node n{*j, file, "", r};
    if (!j->is_object()) {
      n.fail("must be an object");
      continue;
    }
    if (!n.has("intents")) n.fail("missing intents");
    const bool filePlaceholder = placeholderOf(n, "placeholder", {});
    n.each("intents", false, [&](const Node& x) {
      const std::string id = x.text("id");
      if (id.empty()) return x.fail("missing id");
      IntentInfo info;
      info.placeholder = placeholderOf(x, "draft", {"sopRef", "validated_by", "validated_at"}) || filePlaceholder;
      for (const auto& e : x.texts("examples", false)) {
        std::vector<std::string> slots;
        if (!templateSlots(e, slots)) x.fail("bad {slot} in an example");
        info.slots.insert(slots.begin(), slots.end());
      }
      if (x.has("parse") && x.child("parse").j.is_object()) {
        if (x.child("parse").has("optional"))
          for (const auto& f : x.child("parse").texts("optional", true)) info.optional.insert(f);
        if (x.child("parse").has("required"))
          for (const auto& f : x.child("parse").texts("required", true)) info.required.insert(f);
      }
      info.shortName = x.text("shortName");
      if (info.shortName.size() > 24) x.child("shortName").fail("at most 24 characters");
      if (!out.emplace(id, std::move(info)).second) x.fail(id + " is defined twice");
    });
  }
}

void loadReplies(const std::string& dir, const std::map<std::string, IntentInfo>& intents, std::map<std::string, PilotReply>& out,
                 Report& r) {
  const auto files = jsonFiles(dir);
  if (files.empty()) r.errors.push_back({dir, "/", "no pilot reply files"});
  for (const auto& file : files) {
    const auto j = read(file, r);
    if (!j) continue;
    const Node n{*j, file, "", r};
    if (!n.object({"schemaVersion", "validation", "replies"}, {"comment"})) continue;
    n.version();
    const bool filePlaceholder = placeholderOf(n, "placeholder", {});
    n.each("replies", false, [&](const Node& x) {
      if (!x.object({"id", "text"}, {"validation", "readbackOf"})) return;
      PilotReply p;
      p.id = x.text("id");
      p.text = x.text("text");
      if (!p.id.empty() && !chars(p.id, kIdChars)) x.child("id").fail("capital letters, digits, - and _ only");
      if (!p.text.empty() && !templateSlots(p.text, p.slots)) x.child("text").fail("bad {slot}");
      p.placeholder = placeholderOf(x, "draft", {"sopRef", "validated_by", "validated_at"}) || filePlaceholder;
      p.readbackOf = x.text("readbackOf");
      if (!p.readbackOf.empty()) {
        const auto in = intents.find(p.readbackOf);
        if (in == intents.end()) {
          x.child("readbackOf").fail("no intent named " + p.readbackOf);
        } else {
          p.completeReadback = true;
          for (const auto& f : in->second.required)
            p.completeReadback = p.completeReadback && std::find(p.slots.begin(), p.slots.end(), f) != p.slots.end();
        }
      }
      const std::string id = p.id;
      if (!id.empty() && !out.emplace(id, std::move(p)).second) x.child("id").fail(id + " is defined twice");
    });
  }
}

struct Use { std::string reply; Node at; };

void loadResponse(const Node& n, StepResponse& out, std::vector<Use>& uses) {
  if (!n.object({"action"}, {"reply", "withField"})) return;
  const std::string action = n.text("action");
  if (action == "say") {
    out.say = true;
    out.reply = n.text("reply");
    if (!n.has("reply")) n.fail("say needs a reply");
    if (!out.reply.empty()) uses.push_back({out.reply, n.child("reply")});
    if (n.has("withField")) {
      const Node w = n.child("withField");
      if (!w.j.is_object()) return w.fail("must be an object");
      for (const auto& [field, v] : w.j.items()) {
        const std::string id = w.text(field.c_str());
        if (id.empty()) continue;
        out.withField[field] = id;
        uses.push_back({id, w.child(field)});
      }
    }
  } else if (action == "silent") {
    if (n.has("withField")) n.child("withField").fail("silent has no reply");
    if (n.has("reply")) n.child("reply").fail("silent has no reply");
  } else if (!action.empty()) {
    n.child("action").fail("must be say or silent");
  }
}

std::optional<Outcome> outcomeAt(const Node& n, const std::string& name) {
  const auto o = outcomeFromString(name);
  if (!o && !name.empty()) n.fail("unknown outcome " + name);
  return o;
}

void loadExpectation(const Node& n, const Exercise& e, const Scenario& s, Expectation& x) {
  if (!n.object({"intent", "required", "mustMatch"}, {"optional"})) return;
  x.intent = n.text("intent");
  x.required = n.texts("required", false);
  x.optional = n.texts("optional", true);
  if (x.optional.size() > 1) n.child("optional").fail("at most one optional field");
  if (n.has("mustMatch")) {
    const Node m = n.child("mustMatch");
    if (!m.j.is_object()) {
      m.fail("must be an object");
    } else {
      for (const auto& [field, v] : m.j.items()) {
        std::vector<std::string> slots;
        const std::string ref = v.is_string() ? v.get<std::string>() : "";
        if (!templateSlots(ref, slots) || slots.size() != 1 || ref != "{" + slots[0] + "}")
          m.child(field).fail("must be one {parameter}");
        else if (!s.generators.count(slots[0]))
          m.child(field).fail("no generator named " + slots[0]);
        else
          x.mustMatch[field] = slots[0];
      }
    }
  }
  if (x.intent.empty()) return;
  const auto it = e.intents.find(x.intent);
  if (it == e.intents.end()) return n.child("intent").fail("unknown intent " + x.intent);
  for (const auto& f : x.required) {
    if (!it->second.slots.count(f)) n.child("required").fail(x.intent + " has no slot " + f);
    // Without it any value would be accepted, e.g. the wrong runway.
    if (n.has("mustMatch") && n.child("mustMatch").j.is_object() && !n.child("mustMatch").j.contains(f))
      n.fail("required field " + f + " needs a mustMatch");
  }
  for (const auto& f : x.optional) {
    if (!it->second.optional.count(f)) n.child("optional").fail(x.intent + " has no optional field " + f);
    if (std::find(x.required.begin(), x.required.end(), f) != x.required.end()) n.child("optional").fail(f + " is already required");
  }
  for (const auto& [f, p] : x.mustMatch)
    if (std::find(x.required.begin(), x.required.end(), f) == x.required.end())
      n.child("mustMatch").fail(f + " is not a required field");
  if (n.has("required") && std::find(x.required.begin(), x.required.end(), "callsign") == x.required.end())
    n.child("required").fail("must contain callsign");
}

void loadStep(const Node& n, const Exercise& e, const Scenario& s, const std::set<std::string>& finals, ScenarioStep& st,
              std::vector<Use>& uses) {
  if (!n.object({"id", "label", "phase", "expects", "responses", "transitions"},
                {"onEnter", "timeoutSeconds", "maxPilotCalls", "maxSayAgain", "safetyRules"}))
    return;
  st.id = n.text("id");
  st.label = n.text("label");
  st.phase = n.text("phase");
  st.final = finals.count(st.id) > 0;
  if (n.has("safetyRules") && !(n.child("safetyRules").j.is_array() && n.child("safetyRules").j.empty()))
    n.child("safetyRules").fail("safety rules are not supported yet; leave the list empty");
  if (n.has("onEnter")) {
    const Node o = n.child("onEnter");
    if (o.object({"say", "delaySeconds"})) {
      StepOnEnter oe;
      oe.reply = o.text("say");
      oe.delay = static_cast<Ns>(o.number("delaySeconds", 0, 0, false) * 1e9);
      if (!oe.reply.empty()) uses.push_back({oe.reply, o.child("say")});
      st.onEnter = oe;
    }
  }
  std::set<std::string> intents;
  n.each("expects", true, [&](const Node& x) {
    Expectation ex;
    loadExpectation(x, e, s, ex);
    if (!ex.intent.empty() && !intents.insert(ex.intent).second) x.fail(ex.intent + " is expected twice");
    st.expects.push_back(std::move(ex));
  });
  if (n.has("responses")) {
    const Node rs = n.child("responses");
    if (!rs.j.is_object()) {
      rs.fail("must be an object");
    } else {
      for (const auto& [name, v] : rs.j.items()) {
        const Node x = rs.child(name);
        if (const auto o = outcomeAt(x, name)) loadResponse(x, st.responses[*o], uses);
      }
    }
  }
  std::set<Outcome> ons;
  n.each("transitions", true, [&](const Node& x) {
    if (!x.object({"on", "to"}, {"errorBranch", "guard"})) return;
    if (x.has("guard")) x.child("guard").fail("guards are not supported yet");
    StepTransition t;
    t.to = x.text("to");
    t.errorBranch = x.flag("errorBranch");
    const std::string on = x.text("on");
    const auto o = x.has("on") ? outcomeAt(x.child("on"), on) : std::nullopt;
    if (!o) return;
    t.on = *o;
    if (!ons.insert(*o).second) x.fail(on + " has two transitions");
    // Only CORRECT advances; anything else is a separate, explicit branch.
    if (*o != Outcome::Correct && !t.errorBranch) x.fail("a transition on " + on + " must say errorBranch: true");
    if (*o == Outcome::Correct && t.errorBranch) x.fail("CORRECT is not an error branch");
    if (*o == Outcome::Timeout) x.fail("no transition on TIMEOUT: the instructor continues or ends (D-424)");
    st.transitions.push_back(std::move(t));
  });
  st.timeout = static_cast<Ns>(n.number("timeoutSeconds", 1, 0, true)) * ms(1000);
  st.maxPilotCalls = static_cast<int>(n.number("maxPilotCalls", 0, 0, true));
  st.maxSayAgain = static_cast<int>(n.number("maxSayAgain", 1, 0, true));

  if (st.final) {
    for (const char* k : {"expects", "responses", "transitions"})
      if (n.has(k) && !n.child(k).j.empty()) n.child(k).fail("a final step has none");
    if (n.has("timeoutSeconds")) n.child("timeoutSeconds").fail("a final step has none");
    return;
  }
  if (st.expects.empty()) n.fail("a step that is not final needs expects");
  // What is given must be read back (D-417), and only what a step takes.
  std::set<std::string> optional;
  for (const auto& x : st.expects) optional.insert(x.optional.begin(), x.optional.end());
  const auto correct = st.responses.find(Outcome::Correct);
  for (const auto& f : optional)
    if (correct == st.responses.end() || !correct->second.withField.count(f))
      n.child("responses").fail("optional " + f + " needs CORRECT/withField/" + f);
  for (const auto& [o, r] : st.responses)
    for (const auto& [f, id] : r.withField)
      if (o != Outcome::Correct || !optional.count(f)) n.child("responses").fail("withField " + f + " is only for CORRECT and an optional field");
  if (!ons.count(Outcome::Correct)) n.fail("no transition on CORRECT");
  for (const auto& [o, name] : kOutcomeNames) {
    // No safety rules yet, so UNSAFE cannot occur; TIMEOUT only with a timeout.
    const bool needed = o == Outcome::Timeout ? st.timeout > 0 : o != Outcome::Unsafe;
    if (needed && n.has("responses") && !st.responses.count(o)) n.child("responses").fail("missing " + std::string(name));
  }
  if (st.timeout > 0 && !n.has("maxPilotCalls")) n.fail("timeoutSeconds needs maxPilotCalls");
}

void loadGenerators(const Node& n, Scenario& s) {
  if (!n.has("generators")) return;
  const Node g = n.child("generators");
  if (!g.j.is_object()) return g.fail("must be an object");
  for (const auto& [name, v] : g.j.items()) {
    const Node x = g.child(name);
    if (!chars(name, kSlotChars)) x.fail("parameter names use letters only");
    if (!x.object({"pool"}, {"mustServe"})) continue;
    ParamGenerator pg{x.text("pool"), x.text("mustServe")};
    if (pg.pool != "runways" && pg.pool != "taxiways" && pg.pool != "aircraftTypes" && pg.pool != "callsigns" && !pg.pool.empty())
      x.child("pool").fail("must be runways, taxiways, aircraftTypes or callsigns");
    s.generators[name] = pg;
  }
  for (const auto& [name, pg] : s.generators) {
    if (pg.mustServe.empty()) continue;
    const Node x = g.child(name).child("mustServe");
    const auto it = s.generators.find(pg.mustServe);
    if (it == s.generators.end())
      x.fail("no generator named " + pg.mustServe);
    else if (!(pg.pool == "taxiways" && it->second.pool == "runways") && !(pg.pool == "callsigns" && it->second.pool == "aircraftTypes"))
      x.fail("only a taxiway can serve a runway, and a callsign an aircraft type");
  }
}

void checkGraph(const Node& n, const Scenario& s) {
  std::set<std::string> reached;
  std::vector<std::string> todo{s.initialStep};
  bool finalReached = false;
  while (!todo.empty()) {
    const std::string id = todo.back();
    todo.pop_back();
    const ScenarioStep* st = s.step(id);
    if (!st || !reached.insert(id).second) continue;
    finalReached = finalReached || st->final;
    for (const auto& t : st->transitions) todo.push_back(t.to);
  }
  for (std::size_t i = 0; i < s.steps.size(); ++i) {
    const auto& st = s.steps[i];
    const Node x = n.child("steps").item(i);
    if (!st.id.empty() && !reached.count(st.id)) x.fail(st.id + " cannot be reached from the initial step");
    for (const auto& t : st.transitions)
      if (!t.to.empty() && !s.step(t.to)) x.child("transitions").fail("no step named " + t.to);
  }
  if (!finalReached) n.fail("no final step can be reached");
}

void loadScenario(const Node& n, const ScenarioSources& src, Exercise& e, std::map<std::string, PilotReply>& replies, Report& r) {
  Scenario& s = e.scenario;
  if (!n.object({"schemaVersion", "id", "name", "category", "validation", "airfield", "initialState", "generators", "initialStep",
                 "steps", "pttRules", "completion"},
                {"objectives", "difficulty", "unexpectedEvents", "comment"}))
    return;
  n.version();
  s.id = n.text("id");
  if (!s.id.empty() && !chars(s.id, kIdChars)) n.child("id").fail("capital letters, digits, - and _ only");
  s.name = n.text("name");
  s.category = n.text("category");
  static const std::set<std::string> categories{"departure", "arrival", "ground", "landing", "go-around", "emergency", "rcf", "abnormal"};
  if (!s.category.empty() && !categories.count(s.category)) n.child("category").fail("unknown category " + s.category);
  s.placeholder = placeholderOf(n, "placeholder", {"sopRef", "validated_by", "validated_at"});
  if (n.has("validation") && n.child("validation").j.is_object()) {
    const json& v = n.child("validation").j;  // types are reported by placeholderOf
    const auto raw = [&](const char* k) { return v.contains(k) && v[k].is_string() ? v[k].get<std::string>() : std::string(); };
    s.sopRef = raw("sopRef");
    s.validatedBy = raw("validated_by");
    s.validatedAt = raw("validated_at");
  }
  s.objectives = n.texts("objectives", true);
  s.difficulty = n.text("difficulty");
  if (!s.difficulty.empty() && s.difficulty != "basic" && s.difficulty != "intermediate" && s.difficulty != "advanced")
    n.child("difficulty").fail("must be basic, intermediate or advanced");
  if (n.has("unexpectedEvents") && !(n.child("unexpectedEvents").j.is_array() && n.child("unexpectedEvents").j.empty()))
    n.child("unexpectedEvents").fail("unexpected events are not supported yet");
  if (n.has("initialState") && n.child("initialState").object({"phase"})) s.initialPhase = n.child("initialState").text("phase");

  s.airfield = n.text("airfield");
  if (!s.airfield.empty()) {
    if (!chars(s.airfield, kIdChars))
      n.child("airfield").fail("capital letters, digits, - and _ only");
    else
      loadAirfield((fs::path(src.airfieldDir) / (s.airfield + ".json")).string(), s.airfield, e.airfield, r);
  }
  loadGenerators(n, s);

  std::vector<std::string> finals;
  if (n.has("completion") && n.child("completion").object({"finalSteps"}, {"maxDurationSeconds"})) {
    finals = n.child("completion").texts("finalSteps", false);
    s.maxDuration = static_cast<Ns>(n.child("completion").number("maxDurationSeconds", 1, 0, true)) * ms(1000);
  }
  if (n.has("pttRules") && n.child("pttRules").object({"pressDuringPilot"}, {"interruptedReply"})) {
    const Node p = n.child("pttRules");
    const std::string rule = p.text("pressDuringPilot"), interrupted = p.text("interruptedReply");
    if (const auto pr = pressRuleFromString(rule))
      s.pressDuringPilot = *pr;
    else if (!rule.empty())
      p.child("pressDuringPilot").fail("must be interrupt, block or queue");
    if (!interrupted.empty() && interrupted != "repeat" && interrupted != "discard")
      p.child("interruptedReply").fail("must be repeat or discard");
    s.repeatInterruptedReply = interrupted != "discard";
  }

  std::vector<Use> uses;
  const std::set<std::string> finalSet(finals.begin(), finals.end());
  std::set<std::string> ids;
  n.each("steps", false, [&](const Node& x) {
    ScenarioStep st;
    loadStep(x, e, s, finalSet, st, uses);
    if (!st.id.empty() && !ids.insert(st.id).second) x.fail(st.id + " is used twice");
    s.steps.push_back(std::move(st));
  });
  s.initialStep = n.text("initialStep");
  if (!s.initialStep.empty() && !s.step(s.initialStep)) n.child("initialStep").fail("no step named " + s.initialStep);
  for (const auto& f : finals)
    if (!s.step(f)) n.child("completion").child("finalSteps").fail("no step named " + f);
  if (s.step(s.initialStep)) checkGraph(n, s);

  for (const auto& u : uses) {
    const auto it = replies.find(u.reply);
    if (it == replies.end()) {
      u.at.fail("no reply named " + u.reply);
      continue;
    }
    // The pilot speaks only the scenario's own values (06 section 1).
    for (const auto& slot : it->second.slots)
      if (!s.generators.count(slot)) u.at.fail(u.reply + " uses {" + slot + "}, which no generator supplies");
    e.replies[u.reply] = it->second;
  }
  // D-429: the pilot answers a correct instruction with a readback of it (complete or not).
  for (std::size_t i = 0; i < s.steps.size(); ++i) {
    const ScenarioStep& st = s.steps[i];
    const auto c = st.responses.find(Outcome::Correct);
    if (c == st.responses.end() || !c->second.say) continue;
    std::vector<std::pair<std::string, Node>> said;
    const Node at = n.child("steps").item(i).child("responses").child("CORRECT");
    if (!c->second.reply.empty()) said.emplace_back(c->second.reply, at.child("reply"));
    for (const auto& [f, id] : c->second.withField) said.emplace_back(id, at.child("withField").child(f));
    for (const auto& [id, where] : said) {
      const auto it = replies.find(id);
      if (it == replies.end()) continue;  // reported above
      const std::string& of = it->second.readbackOf;
      const bool expected = std::any_of(st.expects.begin(), st.expects.end(), [&](const Expectation& x) { return x.intent == of; });
      if (!expected) where.fail(id + (of.empty() ? " is no readback" : " reads back " + of + ", which this step does not expect") +
                                "; CORRECT needs a readback (readbackOf)");
    }
  }
}

bool anyPlaceholder(const Exercise& e) {
  if (e.scenario.placeholder || e.airfield.placeholder) return true;
  for (const auto& [id, p] : e.replies)
    if (p.placeholder) return true;
  for (const auto& st : e.scenario.steps)
    for (const auto& x : st.expects)
      if (const auto it = e.intents.find(x.intent); it == e.intents.end() || it->second.placeholder) return true;
  return false;
}

}  // namespace

std::string toString(const LoadError& e) { return e.file + ": " + e.path + ": " + e.what; }

const ScenarioStep* Scenario::step(const std::string& wanted) const {
  for (const auto& s : steps)
    if (s.id == wanted) return &s;
  return nullptr;
}

bool templateSlots(const std::string& text, std::vector<std::string>& slots) {
  slots.clear();
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '}') return false;
    if (text[i] != '{') continue;
    const auto end = text.find('}', i);
    if (end == std::string::npos) return false;
    const std::string name = text.substr(i + 1, end - i - 1);
    if (!chars(name, kSlotChars)) return false;
    slots.push_back(name);
    i = end;
  }
  return true;
}

ScenarioSources scenarioSources(const std::string& dataDir, const std::string& scenarioFile) {
  return {scenarioFile, (fs::path(dataDir) / "airfields").string(), (fs::path(dataDir) / "phraseology").string()};
}

bool loadExercise(const ScenarioSources& src, Exercise& out, std::vector<LoadError>& errors) {
  errors.clear();
  Report r{errors};
  Exercise e;
  std::map<std::string, PilotReply> replies;
  loadIntents(src.phraseologyDir, e.intents, r);
  loadReplies((fs::path(src.phraseologyDir) / "replies").string(), e.intents, replies, r);
  if (const auto j = read(src.scenarioFile, r)) {
    const Node n{*j, src.scenarioFile, "", r};
    loadScenario(n, src, e, replies, r);
    if (errors.empty() && combinationCount(e.scenario, e.airfield) == 0)
      n.child("generators").fail("no valid combination of values in airfield " + e.airfield.id);
  }
  if (!errors.empty()) return false;
  e.placeholder = anyPlaceholder(e);
  char hex[17];
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(r.hash));
  e.hash = hex;
  out = std::move(e);
  return true;
}

std::string modeError(const Exercise& e, ExerciseMode mode) {
  if (mode == ExerciseMode::Assessed && e.placeholder) return e.scenario.id + " contains placeholder text: Assessed mode is refused (P-11)";
  return {};
}

}  // namespace atc
