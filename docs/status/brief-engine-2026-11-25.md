# Brief for engine (2026-11-25, from runtime)

1. **D-311:** review the contract change `fallback_ptt_used` and `ExerciseResult::ptt.fallback` (as built accepted by the user).
2. **D-317 (user ruling):** a transmission that hits the 10 s recogniser limit (`Recognised.sttFault` = "no result in time") is a system fault.
   - Not a trainee attempt, not toward `maxSayAgain`, not in any score; counted separately in the result.
   - After 2 in a row on the same step (configurable) an `InstructorAlert` with `choice = true`: Continue or End.
   - The pilot still says "say again" (D-314) each time.
   - Where it counts today (read in the code, not run): `++sayAgain_` in `SequenceEngine` for an UNINTELLIGIBLE verdict; `++s->attempts` and `responseTime` in `ExerciseResult.cpp`. `InstructorContinue` is ignored unless `callsExhausted_`.
   - To decide by engine, with the user if not obvious: whether the other `sttFault` ("no speech model") counts the same; what resets "in a row" (a recognised transmission, a step change, Continue); what Continue does; whether timers wait while the dialog is open.
   - The screens are ui's (`ExerciseView`); tell them the new result field.
3. **S-2 (D-123):** ui changed `data/phraseology/placeholder.json`, `src/voice/ClipList.h` (comment or text, see commit 8a2df27) and `tests/voice/test_clips.cpp` on the user's order, niner first. Review. Clips are regenerated in a no-build window.
