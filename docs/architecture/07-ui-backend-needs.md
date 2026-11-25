# 07 What the Stage 2 screens need from a backend

For b2. No code. Screens: `04-ui.md` §5d, §5e, §5f and §5 (approved, D-122 to D-129). Proposals are marked (P); nothing here changes a ruling. "Exists" was read in the code, not run.

Common rules: file work never on the GUI thread or RadioCore's thread; answers are queued to the GUI thread; every refusal has a reason as text; nothing here blocks or ends PTT; no network. Role checks are made in the backend too, not only on the screen. All screens are closed during an exercise.

## 5d Scenario Selection

| Need | Fields or action | Exists |
|---|---|---|
| List all scenarios | file, id, name, category, difficulty, objectives, airfield, step count, half-duplex rule, placeholder, SOP reference; errors (file, path, text) for a file that does not load | `loadExercise` with errors. **Missing:** a folder scan; `Scenario` does not keep objectives, difficulty or sopRef (the loader only checks them) |
| Steps | label, expected intent, timeout, pilot calls. Never the expected values | Yes |
| Value pools | count of callsigns, types, runways, taxiways | Yes |
| Coverage per card | Checking, Covered, Missing (clip IDs), Failed (errors) | `ExerciseCheck::select`, one scenario. (P) the opened card only, so that 4 GB is not used for several banks |
| Select | scenario file; result goes to the Dashboard tile; kept for the session only (C-3) | `ExerciseCheck`; `LiveExercise` takes one fixed file today: I add `select()` |
| Fixed seed | Dropped (user's answer to b2): plans hold exact values, `04-ui.md` §5g | – |
| C-1 | Practice: steps for everybody. Assessed: instructor only | Screen, role from `AccessController` |

## 5e Assessment and Debrief

| Need | Fields or action | Exists |
|---|---|---|
| List exercises | session ID, trainee ID, scenario ID **and hash**, mode, start, end reason, duration, steps done/all, errors, missed responses (count, silence), response time, suspected STT, PTT notes, fallback PTT, model, placeholder, "unreadable" with reason. Newest first, pages of 50 (P) | Log files. **Missing:** a reader of JSON Lines back into `EngineEvent` (none found) |
| Filter | trainee, mode, scenario, days | (P) in the store, not in QML |
| Who sees what (H-1) | trainee: own ID only; instructor: all; not logged in: nothing. **Guests (empty trainee ID): in nobody's history (D-127)**; the store must leave them out for the instructor too | **Missing.** The store takes role and ID and enforces it |
| Open one | `ExerciseResult`, the events for the timeline, header | `buildResult` needs the **scenario as it was**: hash differs from the file today → show the events and say "scenario changed"; (P) store the scenario file's copy beside the log |
| Incomplete log | a log without an end: end "fault", and what is there | `ExerciseResult::end` empty |
| Reclassify | instructor only: session, event, new class, note, role, time. Original never changed; both shown | **Missing.** (P) `<sessionId>.review.jsonl`, append only, owner only |
| Delete result (H-3) | instructor only, with confirmation; line in a log first, as `RecordingStore::remove` | **Missing** |
| Play | audio of one transmission, whole or keyed part; only if saved with consent | Not stored (D-314); Play stays off until `setAudioConnected()` |
| Trend | last 20 of one trainee and one scenario: errors, missed responses, response median, suspected STT; never added together | From the list |
| Not used | pilot readback errors (H-4), score, pass mark, comparison of trainees (H-5), export | – |

Guest results (D-131): no list on any screen. The store gives the instructor their count, and deletes them all in one action; each file deleted is a line in the deletion log, written first. The confirmation shows the count; if it changed since, the store refuses and the new count is shown.

## 5f Settings

Values: read, default, "set by user" flag, allowed range, who may change, when it applies; set (checked, refused with the range); restore the starting value. Stored in `settings.json` (writable folder) over `ui.json`.

| Page | Values and actions | Backend |
|---|---|---|
| General | theme, reduce motion, clock | Screen only; a trainee may change the theme (E-1) |
| Audio | list of microphones and outputs (id, name, present); choose; level; tests | **Missing:** device list, change of device while the radio is free (RadioCore restart of the capture, or next start) |
| PTT | device list with name, VID:PID, type; Learn pedal: start, every press reported with its device, hold time, cancel; grabbed yes/no; fallback option; short tap, pre-roll, tail, longest transmission, debounce, queue settle, PILOT CUT, side-tone | Values in `TxConfig`, `PttConfig`. **Missing:** device list, learning, changing a running core (P: refused during a transmission, applied at the next start of the program) |
| PTT, log | each change of a timing goes to the header of the next exercise log (E-2) | **Missing** in `EventLogHeader`: engine's contract |
| Speech | exercise model, installed models, memory | `SttController` |
| Voice bank | folder, voice, clip count, placeholder, radio effect (default light), test sentence | `VoiceBank`, `applyRadioEffect`; test sentence through `playLocal`. One bank per voice (S-5): choosing = another folder, then the coverage check |
| Recordings | switch, free space, limits, folder, list. Guest results (D-131): count, "Delete guest results" with confirmation, instructor only | Exists (1c); **missing:** guest count and deletion |
| Instructor | idle minutes; change PIN on screen, old PIN first (E-3) | `AccessController`; **missing:** changing the PIN from the app (today `set_instructor_pin`) |
| About | versions, model and bank hashes, licences, folders | **Missing:** hashes as text |

Stop in Assessed needs the PIN (D-129): built in `ExerciseController`, a rule and not a setting.

## 5 Phraseology Studio

Instructor only (D-25). Built first, after 3b (D-136): screens and tests ui, backend and manifest fields b2 (engine reviews the fields later, D-319).

| Need | Fields or action | Exists |
|---|---|---|
| Lists | pilot replies and ATC intents: id, text or examples, status, sopRef, validated_by, validated_at (S-3), placeholder; "used by" (scenario, step, outcome) | Loaders; **missing:** "used by", writing |
| Segment strip | segments with S or D, position, slot, pool, clip ID; lint with place | `PhraseTemplate` |
| Save | text, status, sopRef; before saving: clips that become missing or unused; editing returns to Draft; a Validated needs sopRef and the instructor. Writes keep the order of the file and replace it in one step (P: temporary file, rename) | **Missing** |
| Clips of the scenario | id, words, pool value, state M / P / A, takes; unused clips; **words of the manifest against the data** (the niner case: same ID, other words) | `requiredClips`, `clipList`. **Missing:** comparison of words, which the coverage check does not make |
| Record | start, stop, level, elapsed, clipping; from pedal or button; only while the Studio is open, never opens a transmission | **Missing:** a recording mode of RadioCore (P: like the PTT test) |
| Take | raw, trim start and end, level, warnings; change the handles; play raw, trimmed, with radio, whole sentence | `trimClip`, `applyRadioEffect`, `PhraseAssembler`, `playLocal` |
| Pending (S-1) | undecided takes in a separate `pending` folder, never read by bank, check or player; Accept moves to the bank as primary or alternate and writes the manifest; Reject keeps the file as rejected | **Missing** |
| Coverage | per scenario: needed, primary, missing, unused | `checkCoverage` and the above |

Speakers (D-132): only a person with a record and consent "yes"; the instructor starts the session and chooses the speaker; the ID (never name or rank) is stored with each take, also in `pending` and in the manifest. Consent (recording and pilot voice) is checked when the session starts and before each take is saved. D-31 applies: never in git. **Missing:** speaker ID and pilot-voice consent per take in the manifest (b2 adds them, engine reviews later, D-319).

Consent withdrawn (D-133), or "re-consent needed" (D-135): the bank, the coverage check and the player must leave those takes out **at once**: a clip with no other take is then missing, and Start is blocked. The running program must notice the change (P: LiveExercise asks for a new check when an answer changes; not during an exercise, where no record can be changed). The Studio lists them "Consent withdrawn – delete or replace"; deletion by the instructor is logged first.

### Studio interface (proposed, for b2)

As with login and recordings: `StudioController` (ui, GUI thread, state and texts) and `StudioBackend` (b2, a struct of `std::function`), so that the screens and tests run on a mock. Include rule for Qt files: engine, scenario, parser and PhraseTemplate headers only through `src/app/EngineHeaders.h` (Qt's `slots` macro). Every action answers with an empty text or the reason; slow ones through a callback queued to the GUI thread. Names are proposals. Written down as code in `src/app/StudioBackend.h` (with `StudioMock` as a working example), still a proposal; only `StudioController` and a backend know it. There `saveReply` and `saveIntent` are one `save(entry, revision)`, and `setPedal` is its own function.

| Function | Does | Refuses |
|---|---|---|
| `load()` | replies and intents with status, sopRef, validated_by, validated_at, "used by"; scenarios; pools; people who may record | – |
| `preview(replyId, text)` | segments, lint, "say this", and which clips become missing or unused. Pure, no writing | unreadable template |
| `saveReply(reply)`, `addReply`, `saveIntent` | writes the file in one step, keeps its order; a change of text returns the entry to Draft | placeholder file and Validated; Validated without sopRef; not the instructor; the file changed on disk since `load()` |
| `clips(scenario)` | needed and unused clips: id, words of the data, words of the manifest, state M / P / A, takes (id, file, take, speaker, duration, level, warnings, trim, state: active, pending, consent withdrawn) | – |
| `startSession(speaker)`, `endSession()` | recording mode of RadioCore: the microphone is used, no transmission can open | not the instructor; `mayRecordPilot` false (D-132, D-133, D-135); an exercise or a transmission is open; no microphone |
| `record(clipId)`, `stopRecord()` | raw take at 16 kHz into `pending`, trimmed by SilenceTrimmer (`trimClip`): kept part, level, warnings. With "Pedal" on, the pedal starts and stops it; it never transmits | no session; consent changed since the start (checked again **before the take is saved**); disk below the limit |
| `setTrim(take, start, end)` | trim again from the raw samples | – |
| `play(take, raw / trimmed / radio / sentence)`, `stopPlay()` | `playLocal` | the radio is in use |
| `exportChanges(file)` | writes the changes to a file, for review into git by the user (D-318) | not the instructor; file not writable |
| `accept(take, primary / alternate)` | pending → bank, manifest written in one step, at most one primary | consent not valid now; role |
| `reject(take)`, `deleteTake(take)` | reject keeps the file; delete is logged first (D-133) | role; the log cannot be written |

Events from the backend: level (at most 25 per second), elapsed, recording stopped (also by: microphone lost, time limit, log-out, window closed), take ready, session ended with the reason, clips changed (so that Dashboard and Start check again).

Rules to keep in the backend, not only on the screen:
- A session ends when the instructor logs out (also after 10 min idle, D-120) or an answer changes. Idle log-out during a take: the take is stopped and kept as pending (P).
- The session's speaker is chosen in the session; nobody's login changes.
- **PTT is never blocked by the Studio:** outside a session the pedal transmits nothing on this screen but stays alive; in a session with "Pedal" off a press does nothing; leaving the screen ends the session.
- The waveform gets peaks (at most 1000 pairs), not samples.
- Nothing the Studio writes is in git: audio outside the work tree (D-31, D-418). `data/phraseology` **is** in git and read-only in an installed program: the Studio saves into a writable copy outside the repository and the installation, with Export; changed intents become Draft (G-3, D-318). To settle by b2 (P): the copy is in the writable folder; once it exists every program reads it instead of the installed data (loader, parser, coverage check), and the screen says "changed on this machine"; if the installed data is newer than the copy's origin, the Studio says so and never merges by itself; Export writes a file at a place that the user chooses, never into a work tree by itself.

### Planner interface (proposed, for b2)

`src/app/PlannerBackend.h`, in the form of the Studio interface; functions as 08 section 2. Points for b2:

- `load` answers for everybody (the Dashboard's "Choose plan" needs it); saving, deleting and presets: instructor, checked again in the backend. `revision` covers plans and presets; counting a run must not change it.
- `choose(plan, person)` draws the values (never exactly the combination of that person's previous run, D-142), sets `StatusFeed`'s scenario (the values that run, plus `PlannerController::card()`), starts the coverage check and answers with the values and the run number (0: unnumbered). For a plan of one person the draw looks at that person's runs, whoever chooses.
- Run again: the same plan; the backend shows the new values first (D-321).
- The Start dialog needs `ExerciseContext::plan` (name, trainee, mode, run): set by `ExerciseView` from the chosen plan. Not connected yet; `LiveExercise` and `ExerciseView` are ui's and wait for the approved design.
- The header of the log needs plan id, name, run number, exact values and difficulty (08 section 3).
- `setEvents(plansChanged)`: after a run is counted, or the file was replaced.

## Every backend call of 5d, 5e and 5f (as written on the mocks, 2026-11-26; proposals for runtime)

Form as for the Studio: a struct of `std::function` per screen (`ScenarioBackend.h`, `HistoryBackend.h`, `SetupBackend.h`), known only to its controller, its mock and a backend. Each call answers exactly once, with an empty text or the reason; any thread. Role, "no exercise" and ranges are checked again in the backend. Live, the three pointers of `Main.qml` (`scenarios`, `history`, `setup`) are null and the screens are off with their reason; `main.cpp` sets them when a backend exists.

| Call | Does | Refuses | Exists today |
|---|---|---|---|
| **5d** `load()` | every file of `data/scenarios`: id, name, category, difficulty, airfield, sopRef, half-duplex rule, placeholder, objectives, steps (name, intent, wait, pilot calls), number of values per pool, errors with file and path | – | loader; **missing:** folder scan, keeping objectives, difficulty and sopRef |
| `coverage(id)` | coverage check in the background: checking, ok with clip count, missing with IDs, failed with the reason. May answer more than once | – | `ExerciseCheck` (one scenario at a time); **missing:** a check that does not change the selected exercise |
| `select(id)` | scenario on the Dashboard: values drawn and shown (D-321), check runs (D-115), a chosen plan is replaced. Kept for the session only (C-3) | file has errors; during an exercise | **missing:** `LiveExercise` has one fixed file |
| **5e** `list(person)` | results of one person, or all for the instructor; **never guests** (D-127, D-131): id, trainee, scenario, plan, run, mode, placeholder, start, end, how it ended, steps, errors, missed responses, response median, suspected STT, PTT notes, number reclassified. Facts of the log, without the instructor's changes | a trainee for somebody else (H-1) | `JsonlEventLog`, `buildResult`; **missing:** reader of a folder, list, plan and run in the header |
| `open(id)` | head, missed steps, PTT notes, suspected STT, timeline entries with heard, parsed, expected, outcome, class, delay, audio saved or not, reviews. A log that stops early: "fault" and what is there | as `list` | **missing** |
| `reclassify(id, entry, class, note)` | appended to `<session>.review.jsonl`; the log itself is never changed | not the instructor; not an ATC entry; file not writable | **missing** |
| `remove(id)` | a line in the deletion log first, then log and review file (H-3) | not the instructor; the deletion log cannot be written | **missing** |
| `play(id, entry, keyedOnly)`, `stopPlay()` | audio of one transmission, only if saved with consent | no audio; consent withdrawn (P); the radio is in use | **missing** (D-314) |
| **5f** `load()` | numbers (value, starting value, range, step, unit, "set by user", when it applies), choices with options (a missing device is "off"), facts, number of guest results | – | `SettingsController` has two switches; `ui.json` the starting values |
| `setNumber(key, value)`, `setChoice(key, id)`, `restore(page)` | stored in `settings.json`, over `ui.json`; `restore` removes them. Keys: tap, preroll, tail, longest, debounce, settle, cut, toneMs, toneHz, toneDb, free, idle; theme, motion, clock, mic, out, ptt, sidetone, model, bank, radio | not the instructor (theme, motion, clock: everybody); out of range; tail not below longest; during an exercise or a transmission | **missing:** device lists, change of device, values into a running core (P: next transmission or next start, as each row says); each change of a timing into the next log header (E-2) |
| theme, motion at the start | the stored value must reach `main.cpp` as the starting mode: the screen changes it only when somebody chooses | – | **missing** |
| `startLearn(report)`, `stopLearn()` | every device that reports presses: id, name, VID:PID and type, held ms, down, reason if it may not be used. Never transmits; ends with the screen, the login and an exercise | a transmission is open; no right to read the devices | **missing** (D-310) |
| `changePin(old, new)` | on a worker, through the same try limit as the login (P); never logged | old PIN wrong; not 6 to 12 digits | **missing** (E-3) |
| `testSentence()` | one sentence with the bank and radio effect chosen | the radio is in use; clips missing | `playLocal`; **missing:** the call |
| `deleteGuests(expected)` | deletes all guest results, each a line in the deletion log first; answers the count that is left | not the instructor; not exactly `expected` (then nothing is deleted and the new count comes back) (D-131) | **missing** |
| **login** `marked()`, `setWider(pilotVoice, wider)` | consent versions with the instructor's marks (D-137), kept in `people.json` | the text has replaced no approved one; "wider" is not taken back (assumed) | exists (`PeopleStore`, D-139, ui) |

Already there and used as they are: Dashboard tiles for microphone, speaker, PTT, model, bank and storage with their tests; save switch, PTT fallback (D-116), trainee records; the PIN before the end of an assessed exercise (D-125, D-129, in `ExerciseController`).

## Order and dependencies

1. Studio (D-136): writing, pending folder, record mode, consent checks.
2. 5d: folder scan and the three kept fields (engine's loader), the rest is mine.
3. 5e: log reader, store with H-1 and guests, review file, delete.
4. 5f: device lists, learning, PIN change, About.

Until a backend exists a screen runs on the mock, and a live build says in its navigation entry why it is off.
