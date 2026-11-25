# 09 Phraseology Studio: backend (approved 2026-11-25, D-331)

By b2, 2026-11-25. Approved with conditions on ST-1 (D-331). Screen: 04 §5 (ui, approved). Interface: ui's `src/app/StudioBackend.h` (0ccbab1) and 07 §5; my answers are in section 8. Binding: D-21, D-25, D-31, D-44, D-117 to D-137, D-312, D-318, D-319. "Exists" was read in the code of 00b307f, not run. Points that need the user are **ST-1 to ST-8** (section 10). No figure here is measured.

## 1. Parts

| Part | Folder | Qt | Does |
|---|---|---|---|
| Recording mode | `src/runtime/RadioCore` | no | microphone to memory while a take is recorded; presses never transmit |
| `DataCopy` | `src/storage` | no | the writable copy of `data/phraseology`, its origin, export |
| `PhraseologyWriter` | `src/phraseology` | no | changes one reply or intent, keeps the order of the file, replaces it in one step |
| `TakeStore` | `src/voice` | no | pending takes, raw recordings, accept, reject, delete, manifest |
| `StudioService` | `src/runtime` | no | one worker thread; rules of sections 3 to 6; makes the previews and clip lists |
| `LiveStudio` | `src/app` | yes | fills `StudioBackend`; role and consent from `AccessController` (GUI thread) |

Reused unchanged: `parsePhraseTemplate`, `lintPhrase`, `clipList`, `checkCoverage`, `trimClip`, `applyRadioEffect`, `PhraseAssembler`, `VoiceBank`, `writePrivateFile`, `appendPrivateLine`. File and trim work runs only on the worker, never on the GUI or core thread.

## 2. Recording mode of RadioCore

Like the PTT test that exists (`setPttTest`): a client asks for it, it is refused unless the radio is free, it is reported when it starts and ends.

| Rule | How |
|---|---|
| Session start | refused unless `free()`: no transmission, no pilot, no PTT test, no exercise. Pilot calls asked for meanwhile wait, as in the test |
| Room tone | the first 2 s of a session: only the level is kept (the noise floor for the trim), the audio is discarded |
| Record | from the next captured sample; each core step (5 ms) moves new samples from the ring to one memory buffer. Nothing from before the press on Record is in the take |
| Pedal on | press starts, release stops. Pedal off: a press does nothing. Neither opens a transmission; `pressed`, `txOpened` and the recogniser never see it |
| Stop | button, pedal, 20 s limit (configurable), microphone lost, log-out, leaving the screen, program end. A take that was stopped for another reason is kept as pending with that warning |
| Between takes | D-312 holds: only the 0.3 s pre-roll exists |
| Playback | refused while recording; Record ends a playback first (the microphone must not hear it) |
| Key held at session end | that press is swallowed, as after a PTT test |
| Lost audio | the take gets the warning "audio lost", never silently accepted |

**ST-1:** D-312 says that nothing is recorded or saved without PTT. The Studio needs an exception: in a session that the instructor started, a press on Record (or the pedal, with Pedal on) records. Nothing else changes, and the D-312 test stays as it is. **Approved with conditions (D-331):** (a) a clear "RECORDING" indicator while recording (ui; it follows `RadioSnapshot::recording`, never the button); (b) a Studio recording is never recognised and never reaches the exercise: no `txOpened`, no `txClosed`, nothing to `dev_.speech`; (c) at the end of the session, or when the Studio is left, the next core step erases everything older than the 0.3 s pre-roll (tested with `nonZeroStored()`, as the D-312 test does).

## 3. The writable copy (D-318)

- Place: `<writable>/phraseology/`, made at the first save (not before). Only `phraseology` (intents and replies): the Studio edits nothing else (S-4).
- `origin.json` beside it: the hash of each installed file it was made from. If the installed files differ from that later, the Studio says "the installed data is newer" and never merges.
- One function, `effectivePhraseology(paths)`, decides which folder is read; used by the loader (`ScenarioSources::phraseologyDir` is already separate), the exercise check and the parser rules. Command-line tools keep `--data` and say which folder they read.
- The scenario hash in the log already covers **every file read**, so a log made with changed wording is told apart.
- A damaged or unreadable copy is an error with its path, never a silent return to the installed data.
- Export: one file with the changed entries (old and new text, status), owner only, at the place the user chooses; never committed by the program.

**Found in the code, and it must be on the screen:** the parser reads `digits`, `letters`, `pools`, `slots` and each intent's `parse`. It does **not** read the examples. A changed ATC example therefore does not change what the trainer understands; it feeds the speech data, which is generated on the development machine. Proposed text: "Examples do not change recognition on this machine". The Studio never changes `parse`, pools or digits.

## 4. Saving an entry

1. Role is instructor; no exercise; the file's hash is the `revision` given.
2. Text: the template can be read; unknown slots are refused, lint only warns.
3. A changed text, or changed examples, returns the entry to Draft and removes `validated_by` and `validated_at`. Validated needs sopRef, the instructor, and a file that is not marked placeholder (D-25, 04 §5).
4. **Trial load:** the new file is written to a temporary folder beside the copy and every scenario is loaded with it. A scenario that loads today and would not load then: refused, with the loader's error. Missing **clips** do not refuse it; they are shown before saving (D-21) and block Start through the coverage check.
5. Written with the same key order and indentation, temporary file, sync, rename.
6. Afterwards the exercise check runs again; the Dashboard shows the result.

`addReply`: id in the existing form, not used before. A new reply is used by no scenario until a scenario names it; the Studio cannot change scenarios.

## 5. Takes and the bank

```
voices/<voice>/manifest.json, *.wav     the bank: only accepted takes
voices-pending/<voice>/<take>.wav,.json undecided and rejected takes, raw and cut data
voices-raw/<voice>/                     raw recording of every accepted take, for trimming again
voices-pending/deletions.log            written before a file is deleted
```

- Folders owner only (0700), files 0600; outside every work tree (D-31). No bank, check or player reads `voices-pending` (S-1). The bank folder today is 2 levels below the data folder (`~/.local/share/atc-trainer/voices/...`), so the new folders are beside `voices`.
- Sidecar of a take: clip id, words, number, speaker code, the two answers (value, date, version), recorded at, trim, level, warnings, state. Never a name or rank.
- A file is never overwritten: each take has its own number. Re-trim writes a new file and drops the old one only after the manifest names the new one.
- **Accept**, in this order: consent asked again; WAV into the bank; manifest replaced in one step; sidecar marked. After a crash between two steps the bank is valid; a file that no manifest names is listed as "not used" and not loaded.
- The manifest takes the fields of D-319 as they are; a person's take gets speaker and `pilotVoice`.
- At most one primary. Accepting another as primary makes the old one an alternate, **except** a synthetic one, which becomes rejected (ST-3).
- Reject keeps the file. Delete: instructor, only pending, rejected or withdrawn takes; the line in `deletions.log` is on disk first, otherwise nothing is deleted (as for recordings).

**Voice and placeholder (ST-2):** a person's takes in the Piper bank would give a pilot with two voices. Proposed: a new bank folder for the new voice; the running exercise keeps the bank in `paths.conf` until the user changes it; the Studio shows coverage of the bank that it records. `placeholder` becomes false only by the user's order, when every needed clip has a primary take of a person. Choosing a bank on a screen is Settings (S-5, 5f).

**Words (06 O-4, the "niner" case):** the coverage check does not compare words. The Studio does: a clip whose words differ from the data is listed as "words differ". Blocking Start for it changes engine's check: ST-4.

## 6. Consent, checked in the backend

| When | Check |
|---|---|
| Session start | role instructor; `mayRecordPilot` (both answers yes and not "again") |
| Before a take is saved | the same, for today's answer; otherwise the audio is dropped from memory and nothing is written |
| Accept | `mayUsePilotVoice` |
| Load of a bank | `MayUse` (exists in `VoiceBank::load`; **nobody passes it today**, so every person's take is left out; `ExerciseCheck` must pass it) |
| An answer changes | session ends; a new check is run; the clips are missing at once (D-133). Not possible during an exercise |

The answers are read as a copy made on the GUI thread whenever the store changes; the worker never reads `people.json` itself.

**ST-5:** both consent texts are still drafts (D-134). Answers given under a draft are valid only until the approved text is in place; after that every take of that person is out of use until the person has answered again (D-135). Proposed: the Studio can be built and tested with test tones and synthetic clips now, and **no real voice is recorded before the unit approves the texts**.

## 7. Threads, memory, disk

- One take in memory: 20 s at 16 kHz is 640 kB, plus the cut copy. Freed after it is saved.
- Peaks are made by the worker. Level events come from the 40 ms snapshot that exists (25 per second).
- Disk: refused below the limit that recordings use (200 MB free, `recordingsMinFreeMb`).
- CPU while recording: copying samples only; trim after Stop. **To be measured on the panel.**

## 8. Answers to ui's interface

Accepted as it is, with these changes (only `StudioController`, `StudioMock` and the backend change):

| # | Point | Wanted |
|---|---|---|
| 1 | `StudioClip.takes[].peaks` | not in the list: with many takes every raw file would be read at each refresh. Own function `peaks(take, callback)` |
| 2 | `startSession` | `Done` comes after the room tone (about 2 s). New event `roomTone(dbfs)`, with a warning if it is loud. `endSession` during the room tone: `Done` answers with a refusal, and no session is open; a second `endSession` is harmless (ui) |
| 3 | `play(take, "sentence")` | the backend plays the same reply and values that `sayBefore` and `sayAfter` show. Missing clips: a short beep, and the text says which |
| 4 | `deleteTake` | refused for an active take (primary, alternate) unless consent is withdrawn: reject it first. ui's screen is stricter: a pending take is rejected before it can be deleted |
| 5 | `accept` | `Done` may carry a refusal "manifest changed on disk" |
| 6 | Role | not a parameter: calls come on the GUI thread, the backend asks `AccessController` itself |
| 7 | `StudioContent.voice` | the bank that is recorded (ST-2), which need not be the one of the exercise: add `activeVoice` |
| 8 | Files in the bank that no manifest names | no new take state: only counted in `notice` |
| 9 | `save` | texts of an ATC intent: examples only; `parse` is kept as it is |
| 10 | Stale answers | a callback of an older `load` or `clips` is dropped by `StudioController` (P: the backend answers each call exactly once, in order) |

## 9. Order of work and tests (one commit per package, each with a full build in a free slot)

| Package | Tests (without Qt, devices or sound) |
|---|---|
| S1 recording mode | presses and pedal never open a transmission; nothing before Record in the take; pre-roll only between takes; limit, lost microphone, held key; refused during a transmission, the pilot, an exercise; the D-312 test unchanged |
| S2 copy and writer | order and indentation kept; only the changed entry differs; refused after a change on disk; half-written file impossible; Draft rule; trial load refuses a broken text; newer installed data is reported, not merged |
| S3 takes | consent no, withdrawn or "again" at each of the four places writes nothing (positive control); crash between every two steps of Accept leaves a bank that loads; no overwrite; one primary; delete logged first |
| S4 check | `MayUse` passed; withdrawn consent makes the clip missing at once; words differ |
| S5 `LiveStudio` and wiring | with ui's `StudioController` on fakes; log-out and leaving end the session |
| S6 on the panel | the user, with a person whose answers are yes (after ST-5); trim and level by ear |

Mutation checks as before. My files: `src/runtime`, `src/storage`, `LiveBackend`, `main.cpp`, `ui/CMakeLists.txt`. `src/voice` and `src/phraseology` belong to engine and stt: **ST-6**. Merging ui's Studio screens (mock) does not depend on this design.

Not in this package: editing pools or scenarios, FLAC, Settings, exercise audio, Assessed.

## 10. Decided by the user (D-331: all yes, ST-1 with conditions)

| # | Question | Recommended |
|---|---|---|
| ST-1 | Exception to D-312: recording by Record or pedal in a Studio session | yes |
| ST-2 | A person's voice goes into a new bank; the Piper bank stays unchanged | yes |
| ST-3 | A synthetic primary take that is replaced becomes rejected, a person's becomes alternate | yes |
| ST-4 | Clips whose words differ from the data block Start (changes engine's coverage check) | yes; until then only shown in the Studio |
| ST-5 | No real voice recorded before the unit approves the consent texts | yes |
| ST-6 | b2 may add `TakeStore` (`src/voice`) and `PhraseologyWriter` (`src/phraseology`) as **new** files, and pass `MayUse` in `ExerciseCheck`; engine and stt review later | yes |
| ST-7 | Limits: 20 s per take, 2 s room tone | yes |
| ST-8 | Trim for a person's voice: 20 ms before, 60 ms after (as 04 §5), level −20 dBFS; corrected by ear in S6 | yes |
