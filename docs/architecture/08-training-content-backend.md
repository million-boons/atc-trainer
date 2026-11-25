# 08 Training Content: backend (proposal, for the user's approval)

Requirement: D-326, phased by D-141; order: D-327. **Planner backend first** (sections 2 and 3); the Scenario Builder is later and constrained (section 4). **No code** before approval, except the contract change of section 3.1, which is approved. The Planner wireframe is 04 §5g (ui). This file says what exists, what is missing and what must be decided. "Exists" was read in the code of fabbaa1, not run. (P) marks a proposal.

Common rules: those of 07. Instructor role checked in the backend too; closed during an exercise; files in the writable copy (D-318) only, replaced in one step, refused if the file changed on disk; file work never on the GUI or RadioCore thread.

## 1. Now: predefined scenarios

Authored by engine from the user's dialogue scripts, in `data/scenarios`, all PLACEHOLDER until validated. Events and guards are refused by the loader; the screens show them as "not built".

## 2. Planner API (P), same form as the Studio interface in 07

| Function | Does | Refuses |
|---|---|---|
| `scenarios()` | the predefined ones: id, name, status, steps, pools, load errors | – |
| `values(scenario)` | the valid combinations of the pools, for "fixed values" | – |
| `plans()`, `savePlan`, `deletePlan` | the plan store: `plans.json` in the writable folder, owner only, replaced in one step | not the instructor; unknown scenario; Assessed and the scenario not Validated (every text it uses must be, user); values that are no valid combination; file changed on disk |
| `choosePlan(id)` | the plan on the Dashboard, with its exact values | scenario or airfield changed since it was saved (hash), until the instructor confirms |
| `start()` | an `ExerciseOptions` from the chosen plan, with these values | as Start today |

A plan: id, name, scenario id and hash, one trainee or none (groups later), mode, values (fixed, or "random"), reply delay, radio effect, events ("not built").

## 3. Contract changes (engine, with b2)

1. **Exact values (approved by the user).** `ExerciseOptions` and the log header hold only a seed, and a seed gives other values once a pool changes. A plan with fixed values needs the values themselves, checked against the pools, given to `SequenceEngine` and written to the header. This also serves D-321.
2. **Random plan:** the values are drawn when the plan is put on the Dashboard and are the ones run. Run again keeps what the card shows; a new draw is shown before it is used (P).
3. **Difficulty:** reply delay is `EngineConfig` (0.5 to 1.5 s), radio effect is `ExerciseOptions::radio`. Both per plan; both in the header.
4. **Injected events:** engine design first (03). They must be deterministic by seed.
5. Plan ID in the log header.

## 4. Later: Scenario Builder, constrained

Steps only from approved intents and replies and from airfield pools; new words only through the Phrases tab (the Studio). An incomplete Draft may be saved, but neither started nor validated. Saved to the writable copy with Export (D-318).

### 4.1 What it asks for, and what the engine can do

| Part of the requirement | Today | Missing |
|---|---|---|
| Airfield, value pools | `data/airfields/*.json`, loaded and checked | Nothing: chosen, never edited (user; as S-5) |
| Ordered steps: pilot call, expected intent, required values | `onEnter`, `expects`, `required`, `mustMatch` | Writing |
| Mistake → pilot response, timeouts, call-again count | `responses`, `timeoutSeconds`, `maxPilotCalls`, `maxSayAgain` | Writing |
| Branches | `transitions` on an outcome, with `errorBranch` | Guards are **refused by the loader** |
| Events | `unexpectedEvents` and `safetyRules`: **refused by the loader** unless empty | Engine work; until then "not built" on the screens (user) |
| Objectives, difficulty, SOP reference | Checked, not kept in `Scenario` | Keep them (also needed by 5d) |
| Check: missing phrases and clips | `loadExercise` errors, `checkCoverage` | One answer for the screen |
| Check: PLACEHOLDER wording | `Exercise::placeholder` for the whole | Which text it is |
| Check: unreachable steps | **Checked by the loader** (`checkGraph`: "cannot be reached from the initial step", "no final step can be reached"; corrected, an earlier version of this file said it was not) | A step that can never reach a final one |
| Typed test run | `sim_cli`: intents with fields (`say INTENT field=value`), virtual time. It says itself that free text needs the parser; the parser now exists (`parse_cli`) | Free text through the parser; a library function under `sim_cli` and the screen, so that both run the same code |
| Draft / Validated with SOP reference | `validation {status, sopRef, validated_by, validated_at}` is read | Writing; a change returns it to Draft (D-25 rule applied to scenarios) |
| Export | – | With the Studio's export (D-318) |

### 4.2 API (P)

`open`, `check`, `save`, `validate` (needs an SOP reference, all checks, and every text it uses Validated), `testRun` (engine in virtual time, no microphone, radio, log or result), `exportChanges`. Instructor only.

## 5. Order of work

1. Studio: design and approval, then code (unchanged, first).
2. Planner wireframe 04 §5g (ui): the user's review.
3. Engine: exact values; scenarios from the user's dialogue scripts; the kept scenario fields.
4. Plan store (b2), Planner screen (ui), plans on the Dashboard.
5. 5d (without the fixed seed), 5e, 5f.
6. Scenario Builder; events and guards when engine has built them.
