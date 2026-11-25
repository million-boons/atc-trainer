# 08 Training Content: backend (proposal, for the user's approval)

Requirement: D-326. Order: D-327. **No code.** The screens (wireframes of tabs 2 and 3) are ui's and come first, in `04-ui.md`, for the user's review. This file says what exists, what is missing and what must be decided. "Exists" was read in the code of fabbaa1, not run. (P) marks a proposal.

Common rules: those of 07. Instructor role checked in the backend too; closed during an exercise; files in the writable copy (D-318) only, replaced in one step, refused if the file changed on disk; file work never on the GUI or RadioCore thread.

## 1. What the builder asks for, and what the engine can do

| Part of the requirement | Today | Missing |
|---|---|---|
| Airfield, value pools | `data/airfields/*.json`, loaded and checked | Writing. **S-5 (D-123) says the Studio does not edit airfield pools**; the Builder does (question 1) |
| Ordered steps: pilot call, expected intent, required values | `onEnter`, `expects`, `required`, `mustMatch` | Writing |
| Mistake → pilot response, timeouts, call-again count | `responses`, `timeoutSeconds`, `maxPilotCalls`, `maxSayAgain` | Writing |
| Branches | `transitions` on an outcome, with `errorBranch` | Guards are **refused by the loader** |
| Events | `unexpectedEvents` and `safetyRules`: **refused by the loader** unless empty | Engine work, before the Builder or Planner can offer them |
| Objectives, difficulty, SOP reference | Checked, not kept in `Scenario` | Keep them (also needed by 5d) |
| Check: missing phrases and clips | `loadExercise` errors, `checkCoverage` | One answer for the screen |
| Check: PLACEHOLDER wording | `Exercise::placeholder` for the whole | Which text it is |
| Check: unreachable steps | Not checked as a list for the screen (P: walk over `transitions` from `initialStep`; also a step that can never reach a final one) | New, pure, tested |
| Typed test run | `sim_cli`: intents with fields (`say INTENT field=value`), virtual time. It says itself that free text needs the parser; the parser now exists (`parse_cli`) | Free text through the parser; a library function under `sim_cli` and the screen, so that both run the same code |
| Draft / Validated with SOP reference | `validation {status, sopRef, validated_by, validated_at}` is read | Writing; a change returns it to Draft (D-25 rule applied to scenarios) |
| Export | – | With the Studio's export (D-318) |

## 2. API (P), same form as the Studio interface in 07

| Function | Does | Refuses |
|---|---|---|
| `scenarios()`, `open(file)` | list, or one with all fields and its load errors (file, JSON pointer, text) | – |
| `check(draft)` | errors, missing replies and clips, placeholder texts, unreachable steps; nothing is written | – |
| `save(draft)` | writes to the copy; status Draft | not the instructor; changed on disk; not loadable (question 2) |
| `validate(id, sopRef)` | Validated, with who and when | not the instructor; no SOP reference; any check fails; any text it uses is placeholder |
| `testRun(draft, script)` | the engine in virtual time: verdicts, pilot texts, timeline. No microphone, radio, log or result | a draft that does not load |
| `exportChanges(file)` | D-318 | not the instructor |
| `plans()`, `savePlan`, `deletePlan` | the plan store, `plans.json` in the writable folder, owner only (P) | not the instructor; scenario unknown; Assessed and the scenario not Validated; fixed values that no valid combination has |
| `startPlan(id)` | an `ExerciseOptions` from the plan | scenario changed since the plan was saved (hash), until the instructor confirms |

## 3. Contract changes (engine, with b2)

1. **Exact values.** `ExerciseOptions` and the log header hold only a seed, and a seed gives other values once a pool changes. A plan with fixed values needs the values themselves, checked against the pools, given to `SequenceEngine` and written to the header. This also serves D-321.
2. **Random plan:** the values are drawn when the plan is put on the Dashboard and are the ones run. Run again keeps what the card shows; a new draw is shown before it is used (P).
3. **Difficulty:** reply delay is `EngineConfig` (0.5 to 1.5 s), radio effect is `ExerciseOptions::radio`. Both per plan; both in the header.
4. **Injected events:** engine design first (03). They must be deterministic by seed.
5. Plan ID in the log header.

## 4. Order of work (P)

1. Studio: design and approval, then code (unchanged, first).
2. Wireframes of tabs 2 and 3 (ui), review by the user. Can go on beside 1.
3. Engine: kept fields, unreachable-step check, exact values, then events and guards.
4. Scenario API and typed test run (b2, engine); Builder screen (ui).
5. Plan store (b2); Planner screen (ui); Dashboard shows plans.
6. 5d, 5e, 5f.

## 5. Questions for the user

1. May the Builder **edit** airfield pools (S-5 says the Studio does not), or only choose an airfield and from its pools? Each new pool value needs its clips recorded. Proposal: choose only, at first.
2. May an incomplete Draft be saved? Proposal: yes, but it can be neither started nor validated.
3. "Trainee/group": there are no groups in the trainee records (`PeopleStore`). Proposal: single trainee first, groups later.
4. Events and guards are refused by the loader today. Proposal: Builder and Planner are released without them, and show them as "not built", until engine has built them.
5. 5d has an instructor's "fixed seed" (Practice only). With plans it is a second way to the same thing. Proposal: drop it from 5d.
6. Assessed: one unvalidated text anywhere in a scenario refuses it today (P-11). That rule stays; "Validated" for a scenario then also needs all its texts Validated. Agreed?
