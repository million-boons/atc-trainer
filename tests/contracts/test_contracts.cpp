#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/EngineEvent.h"
#include "core/IEventLog.h"
#include "core/Outcome.h"
#include "core/ParsedInstruction.h"
#include "core/PilotUtterance.h"
using namespace atc;

namespace {
EventLogHeader goodHeader() {
  EventLogHeader h;
  h.sessionId = "S1";
  h.scenarioId = "DEP-01";
  h.scenarioHash = "abc";
  h.modelId = "whisper-tiny-genprompt";
  h.modelConfig = "{}";
  return h;
}

struct MemoryLog : IEventLog {
  std::vector<EngineEvent> events;
  bool ended = false;
  bool begin(const EventLogHeader& h, std::string& error) override {
    error = headerError(h);
    return error.empty();
  }
  void append(const EngineEvent& e) override { events.push_back(e); }
  void end() override { ended = true; }
  std::uint64_t lost() const override { return 0; }
};

template <class Side, std::size_t... I>
void collect(std::vector<std::string>& names, std::index_sequence<I...>) {
  (names.push_back(std::string(eventName(EngineEvent{1, 0, Side(std::variant_alternative_t<I, Side>{})}))), ...);
}
}  // namespace

TEST(Outcome, NamesRoundTripAndUnknownIsRefused) {
  std::set<std::string> names;
  for (const auto& [value, name] : kOutcomeNames) {
    EXPECT_EQ(toString(value), name);
    ASSERT_TRUE(outcomeFromString(name).has_value());
    EXPECT_EQ(*outcomeFromString(name), value);
    names.insert(std::string(name));
  }
  EXPECT_EQ(names.size(), 10u);
  EXPECT_FALSE(outcomeFromString("correct").has_value());
  EXPECT_FALSE(outcomeFromString("").has_value());
}

TEST(ParsedInstruction, DefaultsAreTheSafeOnes) {
  const ParsedInstruction p;
  EXPECT_EQ(p.cls, RecognitionClass::Unrecognized);
  EXPECT_TRUE(p.intent.empty());
  EXPECT_EQ(toString(RecognitionClass::Partial), "PARTIAL");
}

TEST(Judged, DoesNotAdvanceByDefault) {
  const out::Judged j;
  EXPECT_FALSE(j.advanced);
  EXPECT_NE(j.outcome, Outcome::Correct);
}

TEST(EngineEvent, EveryNameIsUniqueAndNotEmpty) {
  std::vector<std::string> names;
  collect<EngineInput>(names, std::make_index_sequence<std::variant_size_v<EngineInput>>{});
  collect<EngineOutput>(names, std::make_index_sequence<std::variant_size_v<EngineOutput>>{});
  std::set<std::string> unique(names.begin(), names.end());
  EXPECT_EQ(unique.size(), names.size());
  EXPECT_EQ(unique.count(""), 0u);
}

TEST(EngineEvent, KnowsInputsFromOutputs) {
  EXPECT_TRUE(isInput(EngineEvent{1, 0, EngineInput(in::InstructorStop{})}));
  EXPECT_FALSE(isInput(EngineEvent{2, 0, EngineOutput(out::Ended{})}));
}

TEST(EventLogHeader, ModelIsRequired) {
  EXPECT_EQ(headerError(goodHeader()), "");
  auto h = goodHeader();
  h.modelId.clear();
  EXPECT_NE(headerError(h).find("modelId"), std::string::npos);
  h = goodHeader();
  h.modelConfig.clear();
  EXPECT_NE(headerError(h).find("modelConfig"), std::string::npos);
}

TEST(EventLogHeader, TraineeIdIsEmptyOrACode) {
  auto h = goodHeader();
  EXPECT_TRUE(h.traineeId.empty());
  h.traineeId = "T-01_a";
  EXPECT_EQ(headerError(h), "");
  for (const char* bad : {"First Last", "a.b", "t/1", "\xC3\xA9"}) {
    h.traineeId = bad;
    EXPECT_NE(headerError(h).find("traineeId"), std::string::npos) << bad;
  }
}

TEST(EventLogHeader, ModeIsPracticeByDefaultAndRoundTrips) {
  EXPECT_EQ(goodHeader().mode, ExerciseMode::Practice);
  for (const auto m : {ExerciseMode::Practice, ExerciseMode::Assessed})
    EXPECT_EQ(exerciseModeFromString(toString(m)), m);
  EXPECT_FALSE(exerciseModeFromString("Assessed").has_value());
}

TEST(IEventLog, RefusesABadHeaderAndKeepsOrder) {
  auto log = std::make_unique<MemoryLog>();
  std::string error;
  EXPECT_FALSE(log->begin(EventLogHeader{}, error));
  EXPECT_FALSE(error.empty());
  ASSERT_TRUE(log->begin(goodHeader(), error)) << error;
  log->append(EngineEvent{1, ms(5), EngineInput(in::Started{})});
  out::PilotRequested r;
  r.utterance.id = 1;
  r.utterance.text = "PLACEHOLDER";
  log->append(EngineEvent{2, ms(5), EngineOutput(r)});
  log->end();
  ASSERT_EQ(log->events.size(), 2u);
  EXPECT_EQ(eventName(log->events[0]), "started");
  EXPECT_EQ(eventName(log->events[1]), "pilot_requested");
  EXPECT_TRUE(log->ended);
}
