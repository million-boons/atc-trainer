# Brief for runtime (b2) from engine, 2026-11-27

Engine `613ddf6` (D-429, the D-334 data gaps) is ready to merge and build **after the idle window (D-340) ends**. Not compiled: engine built nothing.

Changed: `src/scenario/Scenario.h`, `src/scenario/ScenarioLoader.cpp`, `tests/scenario/test_scenario.cpp`, `data/phraseology/placeholder.json` (`shortName` on 3 intents; stt's file), `data/phraseology/replies/placeholder.json` (`readbackOf` on 4 readbacks), schema docs, 03, 06, 08, decisions.

## Contract

1. Scenario and airfield `validation` use `validated_by` and `validated_at`, like intents and replies; `validated` needs both. `validatedBy` is refused with a hint. No shipped file used it.
2. A step's CORRECT reply and each `withField` reply must have `readbackOf` naming an intent that step expects, or the load fails. The Studio's `appendReply` writes no `readbackOf`, so a new reply cannot be a CORRECT answer until it has one.
3. `Scenario` now keeps `objectives`, `difficulty`, `sopRef`, `validatedBy`, `validatedAt`. `ScenarioService` may stop reading them from the file itself (your choice).
4. Every data file is in the scenario hash: past results will show "Scenario changed since this run" (D-340).

## Review of your edits in engine files (read, not run)

- **`fallback_ptt_used` (D-311):** contract fine; the engine ignores it and replay is unchanged. Nit: a short tap with Screen PTT or Space still counts as a fallback use (`tx_opened`, then `tx_cancelled`).
- **Planner contract (D-336):** fine. Plan values replace only `generateParams`; the seeded RNG for delays and takes is unchanged; `fitsParams` refuses rather than corrects; header version 2 is read back. Nit: the "does not serve" reason tests the pool name `taxiways` in C++.
- **D-338:** no engine code touched. Open, asked of the user: an exercise keyed with `--ptt-device stand-in` leaves no trace in the log or the result (the link is Ok, so no `fallback_ptt_used`, and nothing in the header).
- 06 lines 47–49 and 127 brought up to D-339 (they still said the pedal was not passed).

## Added later the same day: the circuit (edc7669 and after)

- **Merge stt `db77be4` (D-203, the circuit intents) before engine**: the shipped circuit names those intents, and without them the circuit file does not load.
- Engine commits: `edc7669` (D-431 engine part) and the circuit data commit (D-432). Not compiled.
- **Your tests that assume one shipped scenario** will see two: `ScenarioServiceTest.ListsChecksAndSelects` (`list.size() == 1`), `test_planner_backend.cpp:142` (`c.scenarios.size() == 1`), `test_studio_backend.cpp:637` (`c.scenarios.size() == 1`). Please look the departure up by id instead.
- `out::ReadbackCheck`: please add one to `EveryKindOfEventComesBackUnchanged` (EventRead is in your library).
- **Piper run** for the circuit's 30 missing clips (list in `12-circuit-rajali.md` §6), with nobody building. Until then the circuit's coverage check fails and its Start is refused, as designed; the departure is unaffected.
- The departure stays on LINE_UP for now. Moving it to LINE_UP_AND_WAIT (user ruling) changes its readback clips and tests in runtime, ui and stt; to be planned with you.
