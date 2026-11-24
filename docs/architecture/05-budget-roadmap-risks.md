# 05 Budget, roadmap, risks

## 1. Memory budget (estimates, to be measured on the panel)

| Item | Estimate | Cap |
|---|---|---|
| Ubuntu + minimal session (not full GNOME) | 500–800 MB | 900 MB |
| Qt UI, QML engine, textures | 120–200 MB | 250 MB |
| STT engine + model | 200–350 MB (Vosk small) | 450 MB |
| Voice bank, decoded clips (D-27) | about 30 MB per voice | 50 MB |
| Audio buffers, core, SQLite | 20–50 MB | 80 MB |
| **App total** | 350–630 MB | **830 MB** |
| **System total** | 0.85–1.45 GB | **1.75 GB of 3.7** |

Pass: no swap growth across a 4 h soak.

## 2. CPU budget (estimates)

| Moment | Estimate |
|---|---|
| Idle, capture and meter | 3–8 % of one core |
| TX with streaming STT | 60–100 % of one core |
| Clip assembly + DSP | short burst, under 30 % of one core |
| UI during state changes | under 25 % of one core |

i3-3217U has 2 physical cores. Priorities: audio threads raised (rtkit, if allowed), STT normal, DB and voice assembly lower. Governor is `schedutil`; compare with `performance` in benchmarks. Check thermal throttling in the soak test.

Technology cost summary: Qt Quick (about 150 MB, for touch UI); Vosk (about 300 MB, for streaming); prerecorded voice bank (recording effort, no runtime synthesis); SQLite (negligible); libpulse (small, some latency vs direct ALSA, but tolerates sharing and hot-plug).

## 3. Roadmap

| Phase | Output | Exit gate |
|---|---|---|
| 0 | This design | Your approval, C-1 ruling, pedal test |
| 1 | UI prototype on mock backend | You approve screens on the panel |
| 2 | `ptt_probe`, capture, Speech Lab, `stt_bench` | **Pedal works with hold/release, no clipped speech in test set, STT engine chosen from measured data** |
| 3 | Parser | Agreed accuracy on text and WAV corpus, no silent correction |
| 4 | Scenario generation, `scenario_check` | All samples valid, property tests pass |
| 5 | Sequence engine, `sim_cli` | Full exercise by text, incorrect cannot advance |
| 6 | Pilot engine + half-duplex | Invariants of 01 §4 pass |
| 7 | Voice bank + radio DSP | Listeners identify callsigns and numbers; you choose intensity |
| 8 | Assessment and logging | Debrief replay matches the log |
| 9 | Integration and performance | Budgets measured, 4 h soak |
| 10 | Deployment | Clean-panel offline install: .deb, udev, autostart. Requires G1-P (D-309) |

Phase 2 precedes everything that depends on speech. Phases 1 and 2 can overlap. SOP content entry runs alongside from Phase 3.

Build: compile on the panel is possible but slow at 4 GB; prefer another Ubuntu 22.04 machine with the same baseline (P-03). All dependencies archived with checksums, so the panel never needs a network. The panel is online now; disconnect it for acceptance tests to prove offline operation.

## 4. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| 1 | Current pedal is pulse-type (22 of 22 presses lasted about 1 ms, 02 §1.2) | Confirmed | **Blocks hold-to-talk** | Replace pedal or fit hold-type firmware; develop with keyboard/on-screen PTT meanwhile; Learn-pedal refuses pulse devices |
| 2 | Mouse-type pedal clicks the UI | High if confirmed | High | EVIOCGRAB + libinput ignore |
| 3 | STT too weak on callsigns, Indian English | Medium | High | Phase 2 gate, aliases, adaptation |
| 4 | Grammar forces off-script speech to valid instruction | Medium | High | `[unk]`, thresholds, negatives in benchmark |
| 5 | Wrong critical value accepted | Medium | High | Primary benchmark metric, no correction |
| 6 | CPU without AVX2: crashes or slowness | High for whisper | Medium | Own builds, verify on CPU |
| 7 | 4 GB exhausted by desktop | Medium | High | Kiosk session, caps |
| 8 | Headset muted or device changed | Medium | Medium | Readiness check, level floor, hot-plug |
| 9 | Unvalidated phraseology used for training | Medium | High | Placeholder flag, assessed mode refuses |
| 10 | Restricted SOP exposed to cloud services | Low if controlled | High | Cleared extracts only, else offline entry |
| 11 | Voice bank incomplete or unclear through radio effect | Medium | Medium | Light default, A/B test, phrase bank |
| 12 | STT errors scored as trainee errors | Medium | High | Separate category, instructor override |
| 13 | SQLite or disk full | Low | Medium | Free-space check, retention policy |

## 5. Next step after approval

Phase 2 starts with `ptt_probe` (Qt-free CMake target): lists devices, grabs the pedal, prints monotonic edges, hold length and bounce. Phase 1 UI prototype can run in parallel.
