# Studio and Planner: hands-on checklist on the panel

For the user, on the panel, with nothing else running. Written by ui on 2026-11-26 from the design and the tests; nothing here has been tried on the panel yet. The result of each line goes into the last column. A line that fails is a finding, not a stop, unless it says so.

**No real voice.** The consent texts are drafts (ST-5, D-134): nobody's voice is recorded. Studio steps use the synthetic clips that are already in the bank and, in part E only, a **test tone**. Part E needs a test record with consent; if the program does not let you give that, part E is skipped, and that is expected.

**Parts P1 to P5 need the Planner backend.** Until runtime has merged it, Training > Exercises says that this build has no Planner. Do part P then.

**Saving wording.** "Save wording" writes the phraseology file in the data folder in use. The steps here use Undo, so the data folder stays as it is. Saving is tested on copies by the tests.

## Before

1. Close other programs, also the terminals of the other sessions. Switch the network off.
2. Mouse access, only if it was lost (replug or reboot): `sudo setfacl -m u:user:r /dev/input/by-id/usb-LITEON_Dell_Wireless_Device-if01-event-mouse`
3. H540 as sound input and output. Foot pedal connected, if you test it (A6, E5).
4. Instructor PIN: if none is set, set it yourself (never written into a file or log by a session):
   `cd /home/user/projects/atc-trainer/.worktrees/runtime && ./build/bin/set_instructor_pin`
5. Start:

```
cd /home/user/projects/atc-trainer/.worktrees/runtime
./build/bin/atc_trainer --size 1280x800
```

Do parts S and P once at 1280x800. If there is time, repeat S1 and P1 with `--size 1920x1080`.

## S. Phraseology Studio

### A. Look and access

| # | Do | Expect | Result |
|---|---|---|---|
| A1 | Not logged in: nav rail **Training**, tab **Phrases** | "Instructor only. Log in to see or change phrases." No phrase is shown | |
| A2 | Log in as instructor with the PIN | List of phrases with Draft or Validated and coverage; READBACK_TAXI can be chosen | |
| A3 | Choose READBACK_TAXI | Text with `{slot}`; below it the segment strip: static parts solid with "S", slots dashed, mono, with "D"; each chip has a dot (grey missing, amber recorded, green accepted); "Used by: DEPARTURE-01 ..." | |
| A4 | Choose an ATC phrase (tab or list) | ATC examples show the strip but **no** dots and **no** recorder ("ATC examples are not recorded.") | |
| A5 | Coverage bar at the bottom | Total, recorded, accepted, missing; **Next missing >** and **< Prev missing** move to a missing clip, or say there is none | |
| A6 | Press the pedal, and Space, with no session started | Nothing is recorded; no RECORDING sign; nothing is transmitted (no TX in the status bar) | |
| A7 | Log out | The Studio's content goes away at once | |

### B. Wording (without saving)

| # | Do | Expect | Result |
|---|---|---|---|
| B1 | Log in. READBACK_TAXI: add a word to the text | The strip follows at once; the phrase is now **Draft**; Save and Undo are enabled | |
| B2 | Press **Validated** | Refused: "Save the changed text first. Then set it to Validated." (D-151) | |
| B3 | Type `{nonsense}` into the text | A warning under the strip names the unknown slot; Save is off | |
| B4 | **Undo** | The saved text is back; Save off | |
| B5 | Change the text again, then choose another phrase | "Save changed wording?" asks; choose not to save | |
| B6 | A phrase whose file is marked PLACEHOLDER: press Validated | Refused, the reason names PLACEHOLDER | |

### C. Listening to the synthetic clips

| # | Do | Expect | Result |
|---|---|---|---|
| C1 | READBACK_TAXI, choose a dynamic chip, for example the runway | Right pane: clip ID, "Say this:" with the target words in bold inside the sentence; take list with "Take 1 synthetic  primary" | |
| C2 | **Raw**, **Trimmed**, **Radio**, **Sentence** | Each plays; Radio with the light radio effect; Sentence is the whole reply joined with the runtime's gaps; a missing clip is a beep and is named | |
| C3 | Take list at 1280x800 | Every shown take is one whole line: number, speaker, state, length and level; nothing under the Primary/Alternate/Reject buttons; with more takes the list scrolls (D-152) | |
| C4 | Listen for clarity: runway 09 and 27, "hold short runway", two callsigns | Clear; note anything that is not | |

### D. A recording session is refused without consent

| # | Do | Expect | Result |
|---|---|---|---|
| D1 | **Start session...** | "Who records?" lists every record, each with the reason if it may not record (no consent, or asked again) | |
| D2 | Try to choose a record without both answers | It cannot be chosen; nothing starts; no RECORDING sign | |
| D3 | **Cancel** | Back to the Studio, nothing changed | |

### E. Test tone (only with a test record; otherwise skip)

Only if you decide to create a test record, for example `TONE1`, give it both consent answers (recording and pilot voice), and record **only a tone**. The tone: play it on the headset and hold the earpiece close to the boom microphone, for example `speaker-test -t sine -f 1000 -l 1` in a terminal, or a tone app on a phone. **No speech.** After the part: delete the takes (E9) and the record.

| # | Do | Expect | Result |
|---|---|---|---|
| E1 | Start session..., TONE1 | "Measuring the room tone: please be quiet" for about 2 s; then the session runs; a loud room is named | |
| E2 | Choose a missing or dynamic clip; **RECORD**; play the tone 2 s; **STOP** | **RECORDING** sign while it records (from the backend); elapsed time; level meter moves; after Stop the waveform with the trimmed part marked | |
| E3 | Space starts and stops a take too | Same as E2; nothing transmitted | |
| E4 | Record for more than 20 s | The take ends by itself at 20 s (ST-7) | |
| E5 | Pedal: hold it, tone, release | Records only while held, for the chosen clip; RECORDING shown | |
| E6 | Very loud tone | Warning (clipping, TOO LOUD); a very quiet one: "quiet: check the microphone" | |
| E7 | Drag the trim handles; Play trimmed | Plays the trimmed part | |
| E8 | **Primary** on a tone take | Becomes primary in the Studio's own bank (`studio-1`), **not** the exercise's bank; the exercise never plays it unless `paths.conf` names that bank | |
| E9 | **Reject**, then **Delete...**, **Delete take** | The take is gone; the deletion is logged | |
| E10 | **End session**, then the Speech Lab: pedal | The pedal works as PTT again, under the normal rule (D-312); the Studio's takes never appear in the Lab or the exercise | |
| E11 | Start a session again, then leave the Studio (another nav entry) | The session ends by itself; the microphone is back under the normal rule | |

## P. Exercise Planner (after the Planner backend is merged)

| # | Do | Expect | Result |
|---|---|---|---|
| P1 | Not logged in, Training > **Exercises** | "Instructor only. Log in to plan exercises." | |
| P2 | Log in as instructor. **New**: name, scenario Departure 01, trainee S01, Practice, **Fixed** values | Card on the right: **Not saved yet**, no values; checks under "CHECKS OF THE EDIT"; after **Save**: the card shows the saved values and the name | |
| P3 | Change runway to one that does not fit the taxiway | The value is marked "does not fit", Save off with the reason; the card still shows the **saved** values and "Unsaved changes" (D-152) | |
| P4 | **Undo**; **Put on the Dashboard**; open the Dashboard | Card: plan name, values, briefing; Start dialog: plan, run number, mode fixed by the plan; run it | |
| P5 | Result after the exercise | Result names the plan and run number; the plan's run count went up by one (also if you stop it early, PB-1) | |
| P6 | A **Random** plan for Anyone; put it on the Dashboard twice | Values drawn and shown before Start; never exactly the previous combination of that person; Run again draws new values and shows them first (PB-4) | |
| P7 | Assessed plan on a PLACEHOLDER scenario | Not possible: "Assessed: scenario not Validated" | |
| P8 | Log in as trainee S01 (no PIN): Dashboard, **Choose plan** | Only S01's plans and Anyone plans; nothing can be edited | |
| P9 | S01's plan on the Dashboard; S01 logs out | S01's plan is put away; an Anyone plan would stay (PB-6) | |
| P10 | As guest: Choose plan | Anyone plans only | |
| P11 | **Presets...**: change Normal, save; open an older plan | That plan keeps its values; "Use new preset values" is offered (D-147) | |
| P12 | Reply delays 0.3 to 5.0 s, radio heavy: run it | The pilot answers within the delays; heavy radio effect audible | |
| P13 | Quit and start the program again | No plan is chosen (PB-5); saved plans are still there | |

## After

- Delete the test record and its takes, if you made them (part E).
- Note in this file: date, size, findings. Each finding with the step number.
