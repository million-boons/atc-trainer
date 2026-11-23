# 06 Vertical slice: departure (taxi, line up, takeoff)

Status: **APPROVED with changes, 2026-11-23** (D-400 to D-409). All phraseology is PLACEHOLDER. Hours are estimates.

Goal: one departure scenario working end to end on real components, after the Speech Lab and Dashboard run on real backends.

## 1. Design rules

1. **One device owner.** Qt-free `RadioCore` (`src/runtime/`) owns PTT, capture, ring, TransmissionController, STT feed and pilot playback. Speech Lab and Exercise are clients. It is extracted from `LiveBackend`.
2. **Contracts first.** One commit adds the shared headers (`ParsedInstruction`, `Outcome`, `EngineEvent`, `PilotUtterance`, `IEventLog`) and one `CMakeLists.txt` per new folder.
3. **Schema additions** (D-402): `onEnter {say, delaySeconds}` on a step, so the pilot can call unprompted; replies referenced by id.
4. **One source per reply** (D-402): templates in `data/phraseology/replies/`; `PhraseTemplateParser` derives the clip IDs. The speech corpus generator never reads that folder.
5. **Slice limit:** the pilot speaks only the scenario's own values, so coverage is finite and checked at load. See open item O-1.
6. **`pilotStop` is silent when it returns** (D-300): synchronous mute and a small PulseAudio buffer.

## 2. Work packages

Sessions: b2 = `runtime`, 93 = `stt`, 31 = `ui`, 8d = `engine`.

| ID | Work | Owner | Needs | Hours |
|---|---|---|---|---|
| 0a | Finish D-300 tests; adapt `MockBackend` and `LiveBackend` to the new TransmissionController callbacks; merge to `main`; repo sync (§4) | b2 | – | 3–5, sync extra |
| 0b | Commit current STT and phraseology work; b2 merges | 93 | 0a | 1 |
| 0c | Contracts and CMake stubs; b2 merges | 8d | 0a | 2 |
| 1a | `RadioCore`, `PulsePlayer`, Lab playback, speaker check, `StatusController`. Export stays off (D-407) | b2 | 0a, 0c | 7–9 |
| 1b | Dashboard wireframe, user OK, QML; remaining Lab controls. Start disabled until 3b | 31 | 0c; 1a to connect | 4–5 |
| **G1** | User tests Lab and Dashboard on the panel with the H540 | user | 1a, 1b | – |
| 2a | Loader (hand-written checks, D-408), generator, `scenario_check`, departure JSON | 8d | 0c | 4–5 |
| 2b | Context, validator, sequence engine, pilot behaviour, result metrics, `sim_cli` | 8d | 2a | 8–10 |
| 2c | Template parser, voice bank, coverage, `PhraseAssembler` | 8d | 2a | 4–5 |
| 2d | Clip list, Piper clips on the PC, trim and normalise tool; WAV (D-405) | 8d | 2c | 3 |
| 2e | `LINE_UP` data, parser for 3 intents, 4 classes, `parse_cli`, Lab intent data | 93 | 0b, 0c | 6–8 |
| 2f | `RadioAudioProcessor`, `radio_ab` | b2 | 1a | 2–3 |
| 2g | Exercise and Result mockups, user OK, QML on a mock controller | 31 | 1b | 7–9 |
| 3a | `ExerciseRuntime`: RadioCore, STT, parser, engine, voice, player; model lock; JSON Lines log (D-401) | b2 | G1, 2a–2f | 7–9 |
| 3b | Real controllers for Exercise, Result, Dashboard Start/Stop | 31 | 2g, 3a | 3–4 |
| T | Clean STT timing run on the idle panel (D-406) | 93 | before 3c demo | 1 |
| 3c | Acceptance run, fixes, panel measurements | all, b2 leads | 3a, 3b, T | 6–10 |

Order: `0a → 0b, 0c → 1a → G1 → 3a → 3b → 3c`. Wave 2 runs beside Wave 1 from 0c.
Estimate before the move to the PC: 67–88 session-hours, likely 5–6 working days, range 4–8. Not yet re-estimated for the PC: builds get faster, the move itself (§4) is extra.

## 3. Where work happens (D-400)

| | GPU PC | i3 panel |
|---|---|---|
| Worktrees, sessions, builds, tests, Piper | Yes | Only until the move: at most two sessions build/test; D-37 was not withdrawn |
| G1 audio checks, timing, 3c acceptance | No | Yes, **nothing else running** |

Until the move the two build/test slots are **b2** (0a) and **93** (0b). 8d and 31 write files but do not build.
Claude sessions do not move with the repository. New sessions on the PC start from `CLAUDE.md`, this file and `docs/decisions.md`, so anything they need must be committed.

## 4. Repo sync (part of 0a; planned, not set up)

Found 2026-11-23: no git remote; `.git` 2.7 MB; `third_party` 335 MB, not in git; ssh server active on the panel.

| What | How |
|---|---|
| Code, docs, text data | Bare repository **on the PC**, reached over LAN ssh. Both machines push and pull. No cloud host. Without a LAN: `git bundle` on USB. |
| First move | Every session commits; b2 pushes all branches and tags to the PC; the worktrees are recreated there; each tip hash is compared. Panel worktrees are removed only after the user confirms. |
| Panel checkout | `main` or a tag only, detached, never edited. Measurement results go on branch `panel` and are pushed back. |
| Engines, models | `tools/fetch_stt.sh` on the PC (pinned checksums). Then copied to the panel with rsync. |
| Binaries | Built on the PC, copied with rsync with commit hash and SHA-256 list. |
| Pilot clips | rsync PC to panel. Audio is never in git (D-31); `manifest.json` with SHA-256 is. |
| Voice recordings | Stay on the panel until the user rules on it (Q-1). |

Conditions for PC-built binaries to run on the panel:
1. **Same OS libraries:** Ubuntu 22.04, Qt 6.2.4, on the PC or in a 22.04 container there.
2. **Same absolute path** `/home/user/projects/atc-trainer`. `ATC_ROOT`, `ATC_GGML_DIR` and the library RUNPATH are compiled in. Otherwise they must first be made relative.
3. **No newer CPU instructions:** `-march=x86-64-v2` (P-03) is already set; whisper.cpp is built with `GGML_NATIVE=OFF` and run-time variants. Proved by running all tests on the panel after the first copy.

Panel measurement rule: no Claude session, browser, build or sync running; record load, free memory and governor with each result; network off for the 3c acceptance run.

## 5. Piper licence check (D-404, done 2026-11-23 from the primary files; not legal advice)

| Item | Licence | Use |
|---|---|---|
| OHF-Voice/piper1-gpl (maintained) | GPL-3 | Tool on the PC only; not linked into or shipped with the trainer |
| rhasspy/piper (archived) | MIT | Same use; alternative |
| Voice `en_US-norman-medium` | Dataset public domain (LibriVox); trained from scratch; US English male | **Proposed placeholder voice** |
| Voice `en_US-ljspeech-*`, `en_GB-cori-high` | Dataset public domain; trained from scratch; female | Alternatives |
| Voice `en_US-libritts-high` | CC BY 4.0; trained from scratch | Alternative, attribution needed |
| Voice `en_US-ryan-high` | CC BY-NC-SA 4.0 | Not used |
| Voices fine-tuned from `lessac` (`alan`, `libritts_r-medium`) | Base model's terms reported as restrictive | Not used |

## 6. Done when

1. `sim_cli` completes the departure from text; same seed and events give the same log.
2. Taxi, line up, takeoff completes by voice with the network disconnected.
3. Wrong runway or skipped step never advances; wrong callsign gets silence; unclear speech gets "say again"; no call makes the pilot call again.
4. Removing any one required clip makes the scenario fail at load, naming the clip.
5. A press during pilot speech silences the pilot before TX opens: tested with fakes, then measured on the H540.
6. The model is locked; its id and configuration are in every result (D-39).
7. Result screen: steps, attempts, response time, PTT discipline, suspected STT errors shown separately, no pass mark.
8. Release-to-reply time, RAM and CPU measured on the idle panel and reported.

Out of scope: Phraseology Studio, real recordings, SQLite and resume, injected events, assessed mode, packaging, pedal gate (D-03).

## 7. Known limits of the demo

- Speech: best result so far is 25 of 40 instructions fully correct (whisper tiny, generated prompt, shape rules, one speaker, placeholder phrases). Frequent "say again" is expected.
- Delay: median 2.0 s, maximum 4.9 s release to text, measured with the older prompt on a busy machine; D-406 re-measures it.
- "0.9" for "zero nine" is classed AMBIGUOUS, never corrected.

## 8. Open items

| ID | Item | State |
|---|---|---|
| O-1 | Pilot readback of a wrong value that **exists in the airfield pools** should become possible, so readback errors can be trained. Values outside the pools: challenge or say again. | Later; not in the slice (D-409) |
| O-2 | Approved line-up wording | User supplies it if unclassified (D-403) |
| Q-1 | May voice recordings be copied to the PC over the LAN, or do they stay on the panel (D-44)? 93's accuracy work needs them where it runs. | User |
| Q-2 | PC operating system and address; is its user name `user`? | User |
