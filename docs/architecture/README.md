# Architecture – Military ATC Training Simulator

Status: **v0.1 APPROVED by the user, 2026-11-23** (D-04). C-1 ruled: interrupt (D-05). See `docs/decisions.md`.

| File | Content |
|---|---|
| [01-system.md](01-system.md) | Architecture, threads, module matrix, class diagram, end-to-end sequence, folder structure |
| [02-speech-ptt.md](02-speech-ptt.md) | Foot PTT, audio capture, STT comparison and benchmark, parser |
| [03-scenario-pilot.md](03-scenario-pilot.md) | Scenario schema, sequence engine, pilot behaviour, radio voice, assessment, logging |
| [04-ui.md](04-ui.md) | Wireframes at 1280×800, QML structure |
| [05-budget-roadmap-risks.md](05-budget-roadmap-risks.md) | Memory/CPU budget, roadmap with gates, risks |
| [06-vertical-slice.md](06-vertical-slice.md) | Vertical slice: work packages, PC/panel split, repo sync, open items (D-400 to D-409) |
| [schema/](schema/) | `scenario.schema.json`, `airfield.schema.json`, and PLACEHOLDER samples |

## Observed on this machine (2026-11-23, read-only inspection)

Assumed to be the target panel; please confirm.

| Item | Observed | Consequence |
|---|---|---|
| CPU | i3-3217U, 1.8 GHz, 4 logical CPUs; AVX and F16C, **no AVX2, no FMA** | Binaries built with AVX2 would crash with SIGILL. whisper.cpp runs on slower code paths. |
| RAM | 3.7 GiB total, 2.0 GiB available with Firefox, GNOME and Claude Code running; 3.9 GiB swap file, 437 MiB used | App must not depend on swap. Use a minimal kiosk session. |
| Display | 1280×800 LVDS, Intel 3rd-gen graphics, ILITEK touch | 1280×800 is the primary layout. |
| OS | Ubuntu 22.04.5, kernel 6.8 | |
| Audio | PulseAudio; default source and sink are the Logitech H540 USB headset; it offers 16 kHz capture, was running at 44.1 kHz | Capture through PulseAudio, resampled to 16 kHz mono. |
| Pedal candidate | USB `04d8:0055` "Microchip Keyboard Demo". Despite the name it is a **HID boot mouse** (event14): BTN_LEFT/RIGHT/MIDDLE, REL_X/Y | evtest (user, 2026-11-23): activity produces BTN_LEFT. 60 s capture: 22 pulses (14 BTN_RIGHT, 8 BTN_LEFT), **every one about 1 ms, never held**. Pulse-type; cannot do hold-to-talk (02 §1.2). Unless grabbed, each press is a **mouse click**. |
| Permissions | User is not in group `input` | udev rule needed. |
| Toolchain | cmake, g++, Qt 6, evtest, eSpeak NG not on PATH | Distro candidates: Qt **6.2.4**, CMake 3.22, g++ 11.2, eSpeak NG 1.50, libevdev 1.12. |

## Proposed decisions (approve, change or reject each)

| ID | Proposal | Reason |
|---|---|---|
| P-01 | Qt-free C++20 core library; Qt only in `app/` | Testable without Qt or hardware |
| P-02 | Distro Qt 6.2.4, not a newer bundled Qt | Simple offline packaging. Cost: no Qt 6.5+ QML features |
| P-03 | Build baseline `-march=x86-64-v2`; AVX only behind runtime dispatch | Panel lacks AVX2/FMA |
| P-04 | Vosk small (Indian English) as *leading candidate*, whisper.cpp as challenger; decide only on panel benchmark | Streams during the transmission; whisper.cpp decodes after release |
| P-05 | PTT through libevdev + EVIOCGRAB + udev; mouse-button pedals added to supported types | Candidate device is a mouse |
| P-06 | CLOCK_MONOTONIC for pedal and audio; cut audio by timestamp | Prevents clipped speech whatever the thread latency |
| P-07 | Default press-during-pilot policy: **interrupt** | Only policy that always satisfies "pilot never talks while the pedal is held". See conflict C-1 |
| P-08 (**withdrawn**, D-27) | eSpeak NG live baseline behind `ITextToSpeech`; optional pre-rendered phrase bank later | Low cost on the panel; bank improves voice without runtime synthesis |
| P-09 | Single-threaded, seeded, event-driven simulation core | Deterministic tests and replay |
| P-10 | SQLite WAL with one writer thread; audio as WAV files | Never blocks audio or UI |
| P-11 | Validated SOP content is a release gate: scenarios with any unvalidated text cannot run in assessed mode | No invented procedures |
| P-12 | Kiosk session: no browser, no screen lock, no notifications | RAM and distraction |
| P-13 | Replace the current pedal with a hold-type one (verified with evtest) before Phase 2 exit; keyboard/on-screen PTT used until then | Current pedal cannot report hold (02 §1.2) |

## Conflict needing your ruling

**C-1.** The requirements say the pilot never talks while the pedal is held, and also that a press during pilot speech may *block* or *queue*. Under block/queue the pilot keeps talking while the pedal is physically held.
Proposal: the rule is absolute for `interrupt`. If you want block/queue, define the rule as "pilot never talks while a transmission is **open**" (a blocked press never opens one).

## Inputs needed from you

1. **Pedal test** (about 2 minutes, procedure in 02 §1.2). The single most useful input.
2. **SOP extracts** in `docs/sop/`. Anything I read there is sent to a cloud model, so place **only material your authority has cleared for that**. Otherwise tell me, and your staff fill the JSON on an offline machine instead.
3. Rulings on C-1 and P-01 to P-13.
4. Confirm this machine is the target panel, and whether the trainee uses the H540 headset in service.

## Assumptions

One trainee and one aircraft per exercise; English phraseology; instructor edits scenarios as JSON; no accounts in v1; network permitted on the build machine only.

## Self-review

| Check | Result |
|---|---|
| Fixed constraints kept | Yes; C-1 flagged rather than silently resolved |
| Invented SOP or phraseology | None intended as real; sample text marked PLACEHOLDER |
| Performance claims | All are estimates or targets, to be measured on the panel |
| Qt-free testability | core, parser, ptt logic, DSP |
| Pedal and half-duplex | 01 §4, 02 §1 |
