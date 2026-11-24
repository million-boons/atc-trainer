#include "app/AccessBackends.h"

#include <algorithm>
#include <memory>
#include <utility>

#ifdef ATC_HAVE_PIN
#include "storage/InstructorPin.h"
#endif

using namespace atc;

AccessBackend realAccess([[maybe_unused]] const std::string& writable) {
  AccessBackend b;
#ifdef ATC_HAVE_STORAGE
  auto store = std::make_shared<PeopleStore>(writable + "/people.json");
  b.load = [store] { return store->load(); };
  b.people = [store] { return store->people(); };
  b.add = [store](Person p, const std::string& today) { return store->add(std::move(p), today); };
  b.setConsent = [store](const std::string& id, bool consent, const std::string& date, const std::string& today) {
    return store->setConsent(id, consent, date, today);
  };
#else
  b.load = [] { return std::string("built without the recording store"); };
  b.people = [] { return std::vector<Person>{}; };
#endif
#ifdef ATC_HAVE_PIN
  const std::string pin = writable + "/instructor.pin";
  b.pinSet = [pin] { return loadPinHash(pin).has_value(); };
  b.checkPin = [pin](const std::string& p) {
    const auto hash = loadPinHash(pin);
    return hash && checkPin(p, *hash);
  };
#endif
  return b;
}

AccessBackend mockAccess() {
  auto all = std::make_shared<std::vector<Person>>(std::vector<Person>{{"S01", "Mock Name", "Mock Rank", true, "2026-11-24"}, {"S02", "", "", false, "2026-11-24"}});
  AccessBackend b;
  b.load = [] { return std::string(); };
  b.people = [all] { return *all; };
  b.add = [all](Person p, const std::string&) {
    if (p.id.empty()) return std::string("the ID must be 1 to 16 letters, digits, - or _");
    if (std::any_of(all->begin(), all->end(), [&](const Person& q) { return q.id == p.id; })) return std::string("this ID exists already");
    all->push_back(std::move(p));
    return std::string();
  };
  b.setConsent = [all](const std::string& id, bool consent, const std::string& date, const std::string&) {
    for (Person& p : *all)
      if (p.id == id) {
        p.consent = consent;
        p.consentDate = date;
      }
    return std::string();
  };
  b.pinSet = [] { return true; };
  b.checkPin = [](const std::string& p) { return p == "123456"; };
  return b;
}

RecordingBackend realRecordings([[maybe_unused]] const std::string& writable, [[maybe_unused]] long minFreeMb, [[maybe_unused]] RecordingsController& c) {
  RecordingBackend b;
#ifdef ATC_HAVE_STORAGE
  StoreConfig cfg;
  cfg.dir = writable + "/recordings";
  cfg.minFreeMb = minFreeMb;
  auto store = std::make_shared<RecordingStore>(cfg, c.savedCallback());
  b.makeKey = [](std::int64_t t, int tx) { return RecordingStore::makeKey(t, tx); };
  b.save = [store](StoredRecording r, std::vector<std::int16_t> a) { store->save(std::move(r), std::move(a)); };
  b.addResult = [store](const std::string& key, StoredResult r) { store->addResult(key, std::move(r)); };
  b.list = [store](std::size_t max, std::function<void(std::vector<StoredRecording>, int)> done) { store->list(max, std::move(done)); };
  b.remove = [store](const std::string& key, const std::string& role, std::function<void(const std::string&)> done) { store->remove(key, role, std::move(done)); };
#endif
  return b;
}

RecordingBackend mockRecordings(RecordingsController& c) {
  auto all = std::make_shared<std::vector<StoredRecording>>();  // newest first
  RecordingBackend b;
  b.makeKey = [](std::int64_t t, int tx) { return "mock-" + std::to_string(t) + "-" + std::to_string(tx); };
  b.save = [all, done = c.savedCallback()](StoredRecording r, std::vector<std::int16_t> a) {
    r.samples = int(a.size());
    const std::string key = r.key;
    all->insert(all->begin(), std::move(r));
    done(key, true, "");
  };
  b.addResult = [all](const std::string& key, StoredResult r) {
    for (auto& x : *all)
      if (x.key == key) x.results.push_back(r);
  };
  b.list = [all](std::size_t max, std::function<void(std::vector<StoredRecording>, int)> done) {
    done({all->begin(), all->begin() + std::ptrdiff_t(std::min(max, all->size()))}, 0);
  };
  b.remove = [all](const std::string& key, const std::string&, std::function<void(const std::string&)> done) {
    std::erase_if(*all, [&](const StoredRecording& r) { return r.key == key; });
    done("");
  };
  return b;
}
