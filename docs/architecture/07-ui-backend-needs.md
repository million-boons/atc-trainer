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
| Fixed seed | number, or random; instructor only, Practice only | `ExerciseOptions::seed`; I add the choice |
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

Instructor only (D-25). Built first, after 3b (D-136): screens and tests ui, backend and manifest fields b2 (engine reviews the fields later, D-138).

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

Speakers (D-132): only a person with a record and consent "yes"; the instructor starts the session and chooses the speaker; the ID (never name or rank) is stored with each take, also in `pending` and in the manifest. Consent (recording and pilot voice) is checked when the session starts and before each take is saved. D-31 applies: never in git. **Missing:** speaker ID and pilot-voice consent per take in the manifest (b2 adds them, engine reviews later, D-138).

Consent withdrawn (D-133), or "re-consent needed" (D-135): the bank, the coverage check and the player must leave those takes out **at once**: a clip with no other take is then missing, and Start is blocked. The running program must notice the change (P: LiveExercise asks for a new check when an answer changes; not during an exercise, where no record can be changed). The Studio lists them "Consent withdrawn – delete or replace"; deletion by the instructor is logged first.

### Studio interface (proposed, for b2)

As with login and recordings: `StudioController` (ui, GUI thread, state and texts) and `StudioBackend` (b2, a struct of `std::function`), so that the screens and tests run on a mock. Every action answers with an empty text or the reason; slow ones through a callback queued to the GUI thread. Names are proposals.

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
| `exportChanges(folder)` | writes the changed files for review into git by the user (D-138) | not the instructor; folder not writable |
| `accept(take, primary / alternate)` | pending → bank, manifest written in one step, at most one primary | consent not valid now; role |
| `reject(take)`, `deleteTake(take)` | reject keeps the file; delete is logged first (D-133) | role; the log cannot be written |

Events from the backend: level (at most 25 per second), elapsed, recording stopped (also by: microphone lost, time limit, log-out, window closed), take ready, session ended with the reason, clips changed (so that Dashboard and Start check again).

Rules to keep in the backend, not only on the screen:
- A session ends when the instructor logs out (also after 10 min idle, D-120) or an answer changes. Idle log-out during a take: the take is stopped and kept as pending (P).
- The session's speaker is chosen in the session; nobody's login changes.
- **PTT is never blocked by the Studio:** outside a session the pedal transmits nothing on this screen but stays alive; in a session with "Pedal" off a press does nothing; leaving the screen ends the session.
- The waveform gets peaks (at most 1000 pairs), not samples.
- Nothing the Studio writes is in git: audio outside the work tree (D-31, D-418). `data/phraseology` **is** in git and read-only in an installed program: the Studio saves into a writable copy outside the repository and the installation, with Export; changed intents become Draft (G-3, D-138). To settle by b2 (P): the copy is in the writable folder; once it exists every program reads it instead of the installed data (loader, parser, coverage check), and the screen says "changed on this machine"; if the installed data is newer than the copy's origin, the Studio says so and never merges by itself; Export writes a folder that the user chooses and never into a work tree by itself.

## Order and dependencies

1. Studio (D-136): writing, pending folder, record mode, consent checks.
2. 5d: folder scan and the three kept fields (engine's loader), the rest is mine.
3. 5e: log reader, store with H-1 and guests, review file, delete.
4. 5f: device lists, learning, PIN change, About.

Until a backend exists a screen runs on the mock, and a live build says in its navigation entry why it is off.
