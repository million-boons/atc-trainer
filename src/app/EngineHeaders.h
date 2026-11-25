#pragma once
// The Qt-free exercise headers have members named "slots", which Qt defines as a keyword. Qt files include them
// through this header, never directly.
#pragma push_macro("slots")
#undef slots
#include "engine/ExerciseResult.h"
#include "engine/SequenceEngine.h"
#include "engine/Simulator.h"
#include "parser/Parser.h"
#include "runtime/ExerciseCheck.h"
#include "runtime/ExerciseRuntime.h"
#include "runtime/JsonlEventLog.h"
#include "scenario/Scenario.h"
#include "voice/ClipList.h"
#include "voice/PhraseTemplate.h"
#pragma pop_macro("slots")
