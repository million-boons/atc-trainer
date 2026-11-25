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
4. **D-319:** review the manifest fields per take (`speaker`, `pilotVoice` {consent, date, version}, `synthetic`), `VoiceBank::load(..., MayUse)`, `withheld()`, and the refused `voice_tool build --real`. Still yours or the Studio's: the pending folder, the words of the manifest against the data (niner), asking the trainee records in `ExerciseCheck`. See also ui's `brief-engine-from-ui-2026-11-25.md`.
5. **Observation, 2026-11-25 (log of a simulated run, no change made):** with `interruptedReply: repeat`, a reply that is keyed over is repeated **before** the reply to the new instruction, so the pilot may say "say again" to the old one after the trainee has already spoken again. As configured; is it wanted when a new instruction has been given?
6. **3c run 1 (D-322, D-325, D-324):** see `3C-FINDINGS-2026-11-25.md`, sections 2, 4 and 5. Item 5 above is answered by the user: the new instruction is answered first; the interrupted readback is repeated only if still owed. The queue order is b2's (`TransmissionController`), which replies are owed is engine's.
7. **Training Content (D-326, D-327):** `docs/architecture/08-training-content-backend.md`, sections 1 and 3: kept scenario fields, unreachable-step check, exact values instead of a seed only, typed test run as a library, events and guards (refused by the loader today). For review; no code before the user has approved it.
