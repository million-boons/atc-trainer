# 06 Vertical slice: departure (taxi, line up, takeoff)

Status: **APPROVED with changes, 2026-11-23** (D-400 to D-409); amended 2026-11-24 (D-410 to D-428, package 1c). §4.1 is approved; A-3 is closed (D-308). "A-n" are the open questions in `docs/status/briefs-2026-11-24.md` (branch `audit`). All phraseology is PLACEHOLDER. Hours are estimates.

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
| 0c | Contracts and CMake stubs; event log header with exercise mode, trainee ID code, model id and configuration (D-416, D-39); b2 merges | 8d | 0a | 2 |
| 0d | Location-independent paths (D-410, §4.1): resolver, relative library paths, tests; b2 merges | b2 | 0a, 0b; §4.1 (approved; A-3 closed, D-308); before the first PC-built binary goes to the panel | 2–4 |
| 1a | `RadioCore`, `PulsePlayer`, Lab playback, speaker check, `StatusController`. Export stays off (D-407) | b2 | 0a, 0c | 7–9 |
| 1b | Dashboard wireframe, user OK, QML; remaining Lab controls. Start disabled until 3b | 31 | 0c; 1a to connect | 4–5 |
| 1c | Recordings: speaker ID and consent required before saving, saving wired to RadioCore, off switch in Settings, list, instructor-only logged delete, low-space warning (D-101, D-102, 04 §5b). Speakers are added by the instructor role only. | 31 | 1a; role mechanism (A-4) | to be estimated by 31 |
| **G1** | User tests Lab and Dashboard on the panel with the H540; PTT is the mouse stand-in, Screen PTT and Space (D-309). **Passed 2026-11-25** (user, D-315), pedal pending | user | 1a, 1b | – |
| 1d | Keyboard-type PTT is refused without exclusive grab and exact VID:PID (new config keys, fake-device tests); udev rule with `LIBINPUT_IGNORE_DEVICE` for the matched pedal only, its VID:PID filled in when the pedal arrives (D-310) | b2 | 1a; before G1-P | – |
| **G1-P** | Short pedal test when the replacement pedal arrives (below); 3a and 3c do not wait for it; final acceptance and deployment (phase 10) require it (D-309) | user, b2 | 1d, pedal | – |
| 2a | Loader (hand-written checks, D-408), generator, `scenario_check`, departure JSON | 8d | 0c | 4–5 |
| 2b | Context, validator, sequence engine, pilot behaviour, result metrics, `sim_cli` | 8d | 2a | 8–10 |
| 2c | Template parser, voice bank, coverage, `PhraseAssembler` | 8d | 2a | 4–5 |
| 2d | Clip list, Piper clips on the panel as an offline step (D-418), trim and normalise tool; WAV (D-405) | 8d | 2c | 3 |
| 2e | `LINE_UP` data, parser for 3 intents, 4 classes, `parse_cli`, Lab intent data | 93 | 0b, 0c | 6–8 |
| 2f | `RadioAudioProcessor`, `radio_ab` | b2 | 1a | 2–3 |
| 2g | Exercise and Result mockups, user OK, QML on a mock controller | 31 | 1b | 7–9 |
| 3a | `ExerciseRuntime`: RadioCore, STT, parser, engine, voice, player; model lock; JSON Lines log (D-401); "fallback PTT used" in log and result (D-311) | b2 | G1, 2a–2f | 7–9 |
| 3b | Real controllers for Exercise, Result, Dashboard Start/Stop | 31 | 2g, 3a | 3–4 |
| T | Clean STT timing run on the idle panel (D-406) | 93 | before 3c demo | 1 |
| 3c | Acceptance run, fixes, panel measurements; accepted with the mouse stand-in, marked "pedal pending" (D-309) | all, b2 leads | 3a, 3b, T | 6–10 |

Order: `0a → 0b, 0c, 0d → 1a → G1 → 3a → 3b → 3c`. Wave 2 runs beside Wave 1 from 0c. 1c follows 1a. G1 does not wait for 1c; it needs only 1a and 1b (D-415). G1 has no pedal checks; they are G1-P, and neither 3a nor 3c waits for G1-P (D-309).
Estimate before the move to the PC: 67–88 session-hours (before 0d, which adds 2–4, and before 1c, not yet estimated), likely 5–6 working days, range 4–8. Not yet re-estimated for the PC: builds get faster, the move itself (§4) is extra.

### G1-P: pedal test (D-309, deferred until the pedal arrives)

The pedal is still **not passed** (D-02, D-03); G1 does not pass it.

1. VID:PID and the kind of device Linux reports (mouse, joystick or keyboard). A keyboard-type pedal only under D-310: exact VID:PID and grabbed exclusively; no other keyboard.
2. Press and release are reported as separate events, also for a hold of several seconds; not pulse type (D-01). Which event code it sends; `pttDevice`, `pttCode` and `pttGrab` in `ui.json` are set to match.
3. D-01: right down opens the transmission, right up closes it; left is reported only.
4. `uaccess` (and, for a keyboard-type pedal, package 1d first): after the pedal rule has its VID:PID and the user has installed it, the app opens the device read-only without a group or root, also after replug, login and reboot.
5. In the app: PTT tile OK instead of STAND-IN; PTT test 2 s; hold over 30 s closes as STUCK; unplug while held closes the transmission and shows MISSING; replug recovers.
6. The result is recorded against D-02.

## 3. Where work happens (D-400)

| | GPU PC | i3 panel |
|---|---|---|
| Worktrees, sessions, builds, tests, Piper | Yes | Only until the move: at most two sessions build/test; D-37 was not withdrawn |
| G1 audio checks, G1-P, timing, 3c acceptance | No | Yes, **nothing else running** |

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
| Pilot clips | Made on the panel (D-418), in the voice-bank folder. Audio is never in git (D-31); `manifest.json` with SHA-256 is. |
| Voice recordings | Panel → PC only, over the LAN: rsync over ssh, one-way, logged (D-412). On the PC: one folder outside every git working copy, no cloud sync, local access only. Never PC → panel, never in the bare repository or a bundle. |

Conditions for PC-built binaries to run on the panel:
1. **Same OS libraries:** Ubuntu 22.04, Qt 6.2.4, on the PC, or in an Ubuntu 22.04 + Qt 6.2.4 container if the PC runs anything else (D-411).
2. **No fixed path** (D-410). Today `ATC_ROOT`, `ATC_GGML_DIR` and the library RUNPATH are compiled in as absolute paths, so until 0d is merged a PC-built binary runs on the panel only from the identical path. 0d removes this.
3. **No newer CPU instructions:** `-march=x86-64-v2` (P-03) is already set; whisper.cpp is built with `GGML_NATIVE=OFF` and run-time variants. Proved by running all tests on the panel after the first copy.

Panel measurement rule: no Claude session, browser, build or sync running; record load, free memory and governor with each result; network off for the 3c acceptance run.

### 4.1 Location-independent paths (task 0d, APPROVED IN PART)

Approved per Q-3 (ruling 2026-11-24) as: install prefix + config file / executable-relative paths, with **no compiled-in absolute paths**. A-3 is closed (D-308): `--root` is kept; no `ATC_ROOT` variable and no developer fallback. The approval is D-305.

Found 2026-11-24 in the code: `ATC_ROOT` in `src/app/main.cpp`, `tools/ptt_text.cpp`, `tests/test_stt.cpp`, `tests/test_phraseology.cpp`; `ATC_GGML_DIR` in `src/app/main.cpp`, `tools/ptt_text.cpp`, `tools/stt_bench.cpp`; absolute RUNPATH to `third_party/vosk` and `third_party/whisper-install/lib` in `atc_trainer`, `ptt_text`, `stt_bench`, `stt_tests`. Model paths in `config/stt_models.json` are already relative.

1. One Qt-free `PathResolver` (`src/core/`) gives the config, data, models, voices and engine folders. Nothing else builds a path from a compiled-in constant.
2. Root, first match wins: `--root` option, which replaces the config files, so models and voices come from it too (D-308); `root=` in the config file (`~/.config/atc-trainer/paths.conf`, then `/etc/atc-trainer/paths.conf`); relative to the executable (`/proc/self/exe`): `<prefix>/bin` → `<prefix>/share/atc-trainer`. The config file may also set the models and voices folders separately. The voices folder **is** the bank: it holds `manifest.json` (D-420); without a `voices=` line it is `root/voices`.
3. A location that was set explicitly but is missing is an error that names it. It never falls through to the next one. If nothing matches, the error lists every place tried.
4. Vosk, whisper and ggml libraries: `$ORIGIN`-relative RUNPATH, and the ggml plugin folder found from the engine folder. `cmake --install` gives `bin/`, `lib/`, `share/atc-trainer/`.
5. Files the program writes (logs, results, recordings) go to a separate writable folder, never under the prefix.
6. Tests use the same resolver. Done when: an installed tree copied to a different path, with the source tree out of reach, passes all tests and starts `atc_trainer`, on the PC and then on the panel; and `readelf -d` and `strings` show no compiled-in absolute build or source path in an installed binary. `/etc/atc-trainer` is allowed: it is the config file's fixed place.

Not affected: the udev rules and H540 service, which use fixed system paths by design.

## 5. Piper licence check (D-404, done 2026-11-23 from the primary files; not legal advice)

| Item | Licence | Use |
|---|---|---|
| OHF-Voice/piper1-gpl (maintained) | GPL-3 | Offline tool (D-418); not linked into or shipped with the trainer |
| rhasspy/piper (archived) | MIT | Same use; alternative |
| Voice `en_US-norman-medium` | Dataset public domain (LibriVox); trained from scratch; US English male | **Approved placeholder voice** (D-413), offline step on the panel (D-418), not shipped |
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
6. The model is locked; its id and configuration are in every result (D-39). The log holds the header, every recognition, each pilot reply (reply id, clip IDs played) and each PTT/transmission event (D-427).
7. Result screen: steps, attempts, response time, PTT discipline, suspected STT errors shown separately, no pass mark.
8. Release-to-reply time, RAM and CPU measured on the idle panel and reported.

Out of scope: Phraseology Studio, real recordings, SQLite and resume, injected events, assessed mode, packaging, pedal gate (D-03; its checks are G1-P, D-309).

Modes (ruling 2026-11-24): Practice shows the live verdict; Assessed hides it until debrief. The slice runs Practice only; P-11 refuses Assessed while any text is placeholder. Exercise audio is stored only with the trainee's consent on record; until A-5 is answered none is saved. (ui records these as D-107 and D-108.)

## 7. Known limits of the demo

- Speech: best result so far is 25 of 40 instructions fully correct (whisper tiny, generated prompt, shape rules, one speaker, placeholder phrases). Frequent "say again" is expected.
- Delay: median 2.0 s, maximum 4.9 s release to text, measured with the older prompt on a busy machine; the timing run T (D-406, D-414) re-measures it. In 3c run 1 on the panel (2026-11-25) the median was 2.8 s over 8 transmissions; target at most 2 s (D-323).
- "0.9" for "zero nine" is classed AMBIGUOUS, never corrected.

## 8. Open items

| ID | Item | State |
|---|---|---|
| O-1 | Pilot readback of a wrong value that **exists in the airfield pools** should become possible, so readback errors can be trained. Values outside the pools: challenge or say again. | Later; not in the slice (D-409) |
| O-2 | Approved line-up wording | Placeholder "line up runway {runway}" confirmed 2026-11-24 until the user supplies wording, if unclassified (D-403) |
| O-3 | A step that takes hold short | **Ruled 2026-11-25 (D-328):** optional in the taxi step, placeholder wording, with readback; clip made (D-329) |
| O-4 | Pilot clips for "niner" (D-123, D-128) | **Regenerated 2026-11-25 (D-329)**; the user's listening is open |
| O-5 | Intent RADIO_CHECK with station names (D-324). Whisper wrote the user's "Rajali" as "a rally" in 3c run 1, and the callsign in it was not found either. | Future; not in the slice |
| Q-1 | May voice recordings be copied to the PC? | **Closed:** yes, LAN only, one-way (D-412) |
| Q-2 | PC operating system, LAN address, user name, GPU. SSH server is installed; the user name no longer has to match the panel (D-410). | **Closed 2026-11-24** (D-418): Piper runs on the panel, offline. The PC values stay blank |
| Q-3 | Approve §4.1 (0d design) | **Closed 2026-11-24:** approved as install prefix + config file / executable-relative, no compiled-in absolute paths. A-3 (extra lookups) closed by D-308 |
