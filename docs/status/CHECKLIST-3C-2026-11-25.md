# 3c: acceptance checklist, departure by voice

For the user, on the panel, with nothing else running. Departure 01, Practice only; all wording is **PLACEHOLDER**. PTT: mouse stand-in (right button), Screen PTT, Space; accepted as "pedal pending" (D-309). The result of each line goes into the last column; a line that fails is a finding, not a stop, unless it says so.

## Before

1. Close other programs (also the terminals of other sessions). Switch the network off: the trainer must work offline (06 §6.2).
2. Mouse access, only if it was lost (replug or reboot): `sudo setfacl -m u:user:r /dev/input/by-id/usb-LITEON_Dell_Wireless_Device-if01-event-mouse`
3. H540 as sound input and output.
4. Start (**without** `--root`: with it the voice bank of `paths.conf` is not read, D-308):

```
cd /home/user/projects/atc-trainer/.worktrees/runtime
./build/bin/atc_trainer --size 1280x800
```

Expected words for callsign, runway and taxiway are the ones on the Dashboard's scenario card, for example EAGLE-421, runway 09, taxiway A3:

| Step | Say | Pilot answers |
|---|---|---|
| Taxi | "Eagle four two one, taxi via alpha three runway zero nine" | readback |
| Line up | "Eagle four two one, line up runway zero nine" | readback |
| Takeoff | "Eagle four two one, runway zero nine cleared for takeoff" | readback |

With runway 27: taxiway B2 ("bravo two"), runway "two seven"; FALCON-732 is "falcon seven three two".

## A. Ready and Start

| # | Do | Expect | Result |
|---|---|---|---|
| A1 | Dashboard after start | Voice bank goes from CHECKING to **COVERAGE OK** by itself; scenario card shows callsign, type, runway, taxiway, PLACEHOLDER | |
| A2 | Nothing loaded | Start disabled; "Cannot start" names the speech model | |
| A3 | Mic test, tone + "I heard it", **Load exercise model** | Start becomes enabled | |
| A4 | **START EXERCISE** | Start dialog: Practice selected, **Assessed not available** with its reason, model "locked at Start" | |
| A5 | Cancel, then START EXERCISE again | Dashboard, then the dialog again | |
| A6 | Not logged in, or trainee without or with consent | "Exercise audio not saved", with the reason; nothing is stored in any case (D-314) | |

## B. The departure (do it three times; note how often "say again" comes)

| # | Do | Expect | Result |
|---|---|---|---|
| B1 | Start, then **do not press until the pilot has called** (about 2 s; finding 2, not fixed yet) | The pilot calls first ("request taxi"), with the light radio effect; RX shown | |
| B2 | Hold the right button, give the taxi instruction, release | TX while held; text and chips; readback; step 1 Done | |
| B3 | Line up, then takeoff | The pilot calls "ready for departure" before line up; both steps Done | |
| B4 | After the last readback | "Exercise complete"; **Result**: 3 of 3, attempts, response time, PTT, suspected STT apart, no pass mark | |
| B5 | Run again | New values: read the callsign on the Exercise screen, not the Dashboard seen before (finding 1, not fixed yet) | |
| B6 | Third run with Screen PTT, fourth with Space | The same | |

## C. Errors

| # | Do | Expect | Result |
|---|---|---|---|
| C1 | Right callsign, wrong runway | Step not advanced; the pilot does not read back as correct; shown as said, never corrected | |
| C2 | Skip a step: takeoff clearance in step Taxi | Not advanced | |
| C3 | The other callsign | **Silence**; "No reply from the pilot" | |
| C4 | Mumble | "say again"; three times in a step: instructor notice | |
| C5 | Say nothing for 30 s | The pilot calls again, at most twice; then **Continue / End**; Continue: he calls again, same step | |
| C6 | Tap about 0.15 s | No transmission, no reply | |
| C7 | Press while the pilot speaks | Pilot silent **before** TX opens (by ear); PILOT CUT; the reply is repeated afterwards | |
| C8 | Release in the middle of a sentence | "Released early" in the result; not advanced or say again | |
| C9 | Pull the mouse dongle in an exercise | Paused, dialog; **Use Screen PTT** continues; result says "Fallback PTT used: yes". Afterwards the `setfacl` line again | |
| C10 | Stop exercise while the pilot speaks | Silent at once; result STOPPED BY INSTRUCTOR | |
| C11 | Dashboard, Lab, Settings during an exercise | Not reachable; they say why | |
| C12 | After the exercise: Lab, one transmission | Works; saved only for a logged-in trainee with consent and the switch on | |

## D. To write down (06 §6.8)

| # | What | Value |
|---|---|---|
| D1 | Release to text, median (result screen) of B1 to B4, three runs | |
| D2 | Release to the pilot's first sound, by stopwatch or ear | |
| D3 | `free -m` and CPU of `atc_trainer` in `top` while the exercise runs, nothing else open | |
| D4 | "Say again" count per run | |

The log of each exercise is `~/.local/state/atc-trainer/exercises/<session>.jsonl` (text only, no audio). Tell runtime when you are done, so that the logs can be read for D1 and for anything odd.

## Known, not a failure

- Do not say "hold short" in the taxi instruction: the step does not take it and answers "query instruction" (O-3).
- "nine" is heard where the text says "niner" (O-4, clips not regenerated).
- Frequent "say again": best measured result is 25 of 40 instructions fully correct (06 §7), for one speaker. This acceptance is not a speech accuracy test.
- A transmission without text after 10 s is shown as a system fault; until engine builds D-317 the result still counts it in the step's attempts and towards the say-again limit.
- Pedal pending (D-309); Assessed is off while text is placeholder.
