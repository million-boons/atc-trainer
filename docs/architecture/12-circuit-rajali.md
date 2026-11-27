# 12. Scenario "Circuit – Rajali" (design, not built)

Status: **2026-11-27: C-2 answered and the engine part built (D-431, not compiled yet); C-1, C-3, C-4, C-7, C-9 open.** First version: DRAFT for the user, 2026-11-27. Source: the user's script of 2026-11-27 and answers (all wording **PLACEHOLDER – validate against approved SOP**; no SOP reference yet). Owner: engine, with parts for stt and runtime (§7). Questions C-1 to C-9 are in §8, each with a recommendation.

## 1. What the user decided

| Point | Ruling (user, 2026-11-27) |
|---|---|
| First call | The pilot calls first (step 0) |
| Short readbacks of clearances ("Rolling", "Lining up", "Cleared to land, continuing approach") | Deliberately incomplete; the trainee must challenge; a missed challenge is a trainee error in Assessment. Full readbacks are the default; incomplete ones come at a configurable rate |
| "line up and wait" | Replaces "line up runway {runway}" |
| Lists | Callsigns like "Falcon 12" (two digits), runway 05, frequencies (e.g. 121.7), readability 1–5, units Tower and Ground |
| Step 1 | Readability is a value 1–5 that the trainee chooses |
| Step 9 | Frequency is a value checked against the airfield's list |

## 2. Airfield `data/airfields/RAJALI.json` (draft)

```json
{
  "schemaVersion": 1,
  "comment": "PLACEHOLDER - validate against approved SOP.",
  "id": "RAJALI",
  "validation": { "status": "placeholder" },
  "runways": ["05"],
  "taxiways": [],
  "aircraftTypes": [],
  "callsigns": [ { "id": "FALCON-12", "spoken": "falcon one two" } ],
  "units": [
    { "id": "TOWER",  "spoken": "rajali tower",  "frequencies": [] },
    { "id": "GROUND", "spoken": "rajali ground", "frequencies": ["121.7"] }
  ]
}
```

Only the values in the script are listed. More callsigns and the tower frequency are C-5. `units` is new (E-4). An empty `taxiways` or `aircraftTypes` list is allowed when no generator uses it (loader change, small).

## 3. Steps

| # | Step | Expected ATC intent (from the script) | Values checked | Pilot full / short (verbatim from the script) |
|---|---|---|---|---|
| 0 | RADIO_CHECK, pilot calls first | – | – | "Rajali Tower, {callsign}, radio check." |
| 1 | RADIO_CHECK | RADIO_CHECK_REPLY "{callsign}, Rajali Tower, read you {readability}" | callsign = generated; readability whole 1–5 (E-3) | "Rajali Tower, {callsign}, reading you five by five. Ready for departure, request taxi." / "Loud and clear, {callsign}." |
| 2 | TAXI | TAXI_TO_HOLDING_POINT "{callsign}, taxi to holding point runway {runway}" | callsign, runway | "Taxiing to holding point runway {runway}, {callsign}." / "Wilco, taxiing to the holding point, {callsign}." |
| 3 | HOLD_SHORT | HOLD_SHORT "{callsign}, hold short runway {runway}" | callsign, runway | "Holding short runway {runway}, {callsign}." / "Wilco, holding short of the runway, {callsign}." |
| 4 | LINE_UP | LINE_UP_AND_WAIT "{callsign}, runway {runway}, line up and wait" | callsign, runway | "Lining up runway {runway} and waiting, {callsign}." / **"Lining up, {callsign}."** (incomplete) |
| 5 | TAKEOFF | TAKEOFF_CLEARANCE "{callsign}, runway {runway}, cleared for takeoff" | callsign, runway | "Cleared for takeoff runway {runway}, rolling, {callsign}." / **"Rolling, {callsign}."** (incomplete) |
| 6 | CIRCUIT | JOIN_CIRCUIT "{callsign}, join the circuit, runway {runway}" | callsign, runway | "Joining the circuit runway {runway}, {callsign}." / "Joining the circuit as instructed, {callsign}." |
| 7 | FINAL | REPORT_FINAL "{callsign}, report final runway {runway}" | callsign, runway | "Wilco, will report final, {callsign}." (one variant only) |
| 8 | LANDING | LANDING_CLEARANCE "{callsign}, runway {runway}, cleared to land" | callsign, runway | "Cleared to land runway {runway}, {callsign}." / **"Cleared to land, continuing approach, {callsign}."** (incomplete) |
| 9 | CONTACT | CONTACT_FREQUENCY "{callsign}, contact ground {frequency}" | callsign, frequency = the ground frequency drawn from the airfield (E-4) | "Contacting ground {frequency}, {callsign}." / "Over to ground, good day, {callsign}." |
| 10 | DONE, final | – | – | – |

In the replies "Falcon One Two" becomes `{callsign}`, "zero five" `{runway}` and "one two one decimal seven" `{frequency}`, so the values are recorded whole and the templates can be used again. "Rajali Tower" in the pilot's lines stays static text (C-3). Errors (missing value, unclear speech, wrong callsign and the rest) use the departure's answers: REQUEST_MISSING, SAY_AGAIN, silence, CHALLENGE, QUERY, CALL_AGAIN.

Five short variants (steps 1, 2, 3, 6 and 9) also leave a value out, but the user named only three as incomplete. C-1 asks how to treat them.

## 4. Engine changes

| ID | Change | Size |
|---|---|---|
| E-1 | **Reply variants.** The CORRECT response gets `incompleteReply` beside `reply`. The scenario's `incompleteReadbackRate` (0 to 1) decides, by the seeded RNG, which one the pilot says; the Planner can set the rate per plan, like the reply delay. Same seed, same choice. Plain alternates, if C-1 wants them: `alternates` chosen equally. | 2–3 h |
| E-2 | **Readback-incomplete check (built, D-431).** When the pilot says an incomplete readback, a challenge is due. A challenge (scenario `readbackCheck.challenge`, judged like a step that expects it, whatever step is current) → the pilot says the full readback: **caught**. Any other instruction that was understood → **missed** ("incomplete readback not challenged", trainee error on the step it belongs to), and that instruction is then judged as usual; unclear speech ("say again") does not use the chance up. A challenge after a complete readback → **unnecessary** (warning only); the pilot says the latest readback again. An exercise stopped while one is due → **open** (not counted). A failed challenge (wrong callsign or value) is judged in the current step but takes none of its branches. Log: output `readback_check` {step, tx, utterance, result}. Result: per step `readbackCaught`, `readbackMissed`, `unnecessaryChallenges`, `readbackOpen`. The loader refuses an incomplete readback whose next step is final, since the exercise would end first. | 4–6 h |
| E-3 | **A value in a range** instead of a generated one: `check: {readability: {range: [1, 5]}}` beside `mustMatch`. A whole number outside it, or not a number → INCORRECT. | 1–2 h |
| E-4 | **Units and frequencies.** `units` in the airfield, generator pool `frequencies` with `unit: GROUND`, so `mustMatch: {frequency: "{groundFrequency}"}` works as for runways. Frequency text "121.7", one decimal, checked by the loader. | 2 h |
| E-5 | `LINE_UP` → `LINE_UP_AND_WAIT` in DEPARTURE-01 too (as "replaces"; C-6): intent, example, the readback "Lining up runway {runway} and waiting, {callsign}" from this script, and its clips. | 1 h |

Tests for each: loader errors with file and path, the rate (0, 1 and the shares over fixed seeds, as for the value draw), replay unchanged, a missed and a caught challenge, readability 0, 6 and "five".

## 5. Speech (stt's part, for 93)

The parser must know: the 9 intents and their cues; two-digit callsigns (`{callsignName} {digit} {digit}`; the slot has three digits today); "read you {readability}"; "decimal" in frequencies; "Rajali Tower" and "ground" as station words (Whisper heard "Rajali" as "a rally", O-5, so a station name is **not required** in the slice, C-3); "line up and wait"; the challenge intent (C-2). Every word must be explained (no filler words), so every ATC line of the script must parse completely.

## 6. Voice

**Missing clips, 2026-11-27** (36 needed, 30 not in the bank `en_US-norman-medium`; worked out by a script that follows `PhraseTemplateParser`'s rules, so `voice_tool list` after the build is the authority; reply that first needs each in brackets):

- `CALLSIGN_FALCON_12@final`  ( CHALLENGE )
- `CALLSIGN_FALCON_12@initial`  ( CALL_AGAIN )
- `CALLSIGN_FALCON_12@medial`  ( CIRCUIT_RADIO_CHECK )
- `FREQUENCY_121_7@medial`  ( CIRCUIT_READBACK_CONTACT )
- `PH_AND_WAITING@medial`  ( CIRCUIT_READBACK_LINE_UP )
- `PH_CLEARED_FOR_TAKEOFF_RUNWAY@initial`  ( CIRCUIT_READBACK_TAKEOFF )
- `PH_CLEARED_TO_LAND@initial`  ( CIRCUIT_READBACK_LANDING_SHORT )
- `PH_CLEARED_TO_LAND_RUNWAY@initial`  ( CIRCUIT_READBACK_LANDING )
- `PH_CONTACTING_GROUND@initial`  ( CIRCUIT_READBACK_CONTACT )
- `PH_CONTINUING_APPROACH@medial`  ( CIRCUIT_READBACK_LANDING_SHORT )
- `PH_GOOD_DAY@medial`  ( CIRCUIT_READBACK_CONTACT_ALT )
- `PH_HOLDING_SHORT_OF_THE_RUNWAY@medial`  ( CIRCUIT_READBACK_HOLD_SHORT_SHORT )
- `PH_HOLDING_SHORT_RUNWAY@initial`  ( CIRCUIT_READBACK_HOLD_SHORT )
- `PH_JOINING_THE_CIRCUIT_AS_INSTRUCTED@initial`  ( CIRCUIT_READBACK_JOIN_ALT )
- `PH_JOINING_THE_CIRCUIT_RUNWAY@initial`  ( CIRCUIT_READBACK_JOIN )
- `PH_LINING_UP@initial`  ( CIRCUIT_READBACK_LINE_UP_SHORT )
- `PH_LINING_UP_RUNWAY@initial`  ( CIRCUIT_READBACK_LINE_UP )
- `PH_LOUD_AND_CLEAR@initial`  ( CIRCUIT_READBACK_RADIO_CHECK_ALT )
- `PH_OVER_TO_GROUND@initial`  ( CIRCUIT_READBACK_CONTACT_ALT )
- `PH_RADIO_CHECK@final`  ( CIRCUIT_RADIO_CHECK )
- `PH_RAJALI_TOWER@initial`  ( CIRCUIT_RADIO_CHECK )
- `PH_READING_YOU_FIVE_BY_FIVE@medial`  ( CIRCUIT_READBACK_RADIO_CHECK )
- `PH_READY_FOR_DEPARTURE@medial`  ( CIRCUIT_READBACK_RADIO_CHECK )
- `PH_ROLLING@initial`  ( CIRCUIT_READBACK_TAKEOFF_SHORT )
- `PH_ROLLING@medial`  ( CIRCUIT_READBACK_TAKEOFF )
- `PH_TAXIING_TO_HOLDING_POINT_RUNWAY@initial`  ( CIRCUIT_READBACK_TAXI )
- `PH_TAXIING_TO_THE_HOLDING_POINT@medial`  ( CIRCUIT_READBACK_TAXI_ALT )
- `PH_WILCO@initial`  ( CIRCUIT_READBACK_HOLD_SHORT_SHORT )
- `PH_WILL_REPORT_FINAL@medial`  ( CIRCUIT_READBACK_REPORT_FINAL )
- `RUNWAY_05@medial`  ( CIRCUIT_READBACK_HOLD_SHORT )

Earlier estimate:

New clips: about 16 fixed phrases × their positions, FALCON-12 @initial and @final, runway 05 positions, the frequency's digits and "decimal". The list comes from `voice_tool list` once the data loads; coverage then fails, naming every missing clip, until Piper has made them. That is an offline Piper run on the panel with nobody building (D-418, D-329), after the idle window. No clips go into git.

## 7. Order and owners

1. User answers C-1 to C-9.
2. engine: E-3, E-4, airfield, replies, scenario, loader tests (about 4 h); then E-1, E-2 and E-5 (about 7–10 h).
3. stt: intents and parser (§5), in its own file `data/phraseology/placeholder.json`, after its timing run; its estimate.
4. b2: merge and build; Piper run for the new clips; coverage and the missing-clip list (empty when done).
5. ui: a "readback not challenged" row on the Result screen; Planner field for the rate.

## 8. Questions

| # | Question | Recommended |
|---|---|---|
| C-1 | The second variants of steps 1, 2, 3, 6 and 9 also leave a value out ("Loud and clear", "taxiing to the holding point", "holding short of the runway", "as instructed", "Over to ground, good day"). Incomplete readbacks to be challenged too, or accepted alternates? | Hold short (3) incomplete like the three you named; 1, 2, 6 and 9 accepted alternates chosen 50/50 (9 cannot be challenged: the exercise ends after it) |
| C-2 | **Answered (D-431):** "{callsign}, read back runway"; "{callsign}, confirm runway {runway}"; "{callsign}, readback incomplete, confirm cleared for takeoff / cleared to land runway {runway}". The pilot answers with the full readback. | – |
| C-3 | "Rajali Tower" in the ATC lines: must the trainee say it? | Accepted if said, not required (O-5: Whisper mishears it). Static text in the pilot's lines. |
| C-4 | "reading you five by five": always five, whatever ATC said? | Yes, the pilot's own reading, fixed text |
| C-5 | More values for the lists: callsigns besides Falcon 12, the tower frequency, other runways? | Only Falcon 12, 05 and 121.7 until you give more. With one value each, every run has the same values. |
| C-6 | Does "line up and wait" also replace the departure's "line up runway {runway}"? | Yes, one intent LINE_UP_AND_WAIT for both |
| C-7 | The default rate of incomplete readbacks | 0.25, set per plan |
| C-8 | Step 7: does the pilot call "final" before ATC clears him to land? The script has no such line. | No, as the script |
| C-9 | Still open from before: a run with the mouse stand-in leaves no trace in the log or result; record it? Wording of the objectives list? D-429 accepted? | Record it in the header and the result; objectives left out until you give them; D-429 as built |
