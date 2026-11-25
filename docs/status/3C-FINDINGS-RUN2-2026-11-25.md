# 3c, second run, 2026-11-25 evening: findings from the logs

Build: `0b39f01` (18:43). Three exercises, all Practice, `whisper-tiny-genprompt`. Read by b2 from the logs; the user's own observations are still to come. Other programs were open: no number here is an idle-panel measurement.

| Log | Values (recomputed from the seed) | End | Voice transmissions | Steps done |
|---|---|---|---|---|
| `20261125T143218Z-b574` (20:02) | FALCON-732, AN-32, B2, 27 | stopped by instructor, 177 s | 3 | 0 |
| `20261125T143520Z-ed50` (20:05) | FALCON-732, AN-32, B2, 27 | **no `ended` entry** | 3, and one cancelled | 0 |
| `20261125T143828Z-5861` (20:08) | FALCON-732, AN-32, B2, 27 | **completed**, 110 s | 8 | 3 of 3 |

## Works (seen in the logs)

- **First complete departure by voice**: taxi, line up, takeoff, each with its readback, until `completed`.
- The pilot calls first, 1.0 s after Start, in all three (D-322).
- A wrong value (A3, 09 for B2, 27): INCORRECT, query, not advanced (C1). No step advanced on a wrong or unclear instruction.
- Key-over: a cut "say again" was not repeated (D-325). It was cut about 0.02 s before its end, so this hardly shows it by ear.
- Two calls again after 30 s each, then the instructor's choice (C5).
- "Not understood 3 times" notice (C4).

## Findings

1. **Taxiway "bravo two"** (stt). In exercise 3 the taxi instruction needed six transmissions. Five were refused: four times "two" was missing after "bravo" (three of them replaced by "turn"), once "brave two". Each was refused with "say again", never corrected: as designed.
   Exercise 2: "alpha" without "three" once; "seven three **turn**" for the callsign, as in run 1 and in exercise 1.
2. **Release to text**, 14 transmissions: median 2.81 s, lowest 2.70, highest 3.01; none at or below 2 s (D-323, run T, stt). Run 1 was 2.83 s over 8.
3. **Two "say again" in a row** (b2, exercise 1, 13 to 27 s). The second transmission was opened 1.5 s after the first release, before the first result. Both results were AMBIGUOUS; both replies were spoken, 1.25 s apart, 7 s of pilot speech. The cut rule (D-325) covers only a reply that was playing. Proposal, **needs the user**: a reply that is not owed is dropped when a newer transmission has been received; only the newest is answered, a readback is always spoken.
4. **Log without an end** (b2, exercise 2). Last entry: transmission 7 cancelled after 0.36 s, at 20:06:55; the program was started again at 20:07:02. No crash report of that time (apport's last entry is 18:15). Not proven: that the window was closed. In the code, closing the program in an exercise writes no `ended`. Proposal: write `ended` with the reason "program closed". Ask the user how the program ended.
5. All three had the same values. With four combinations this is chance one time in 16 for three equal; the seeds differ. In exercise 2 the user spoke A3, 09 while the engine had B2, 27: ask whether the screen showed A3, 09 (D-321 again) or whether it was test C1.

## Not tried yet (by the logs)

Press before the first call (B7), a correct "hold short" (B8), the other callsign (C3), takeoff clearance in Taxi (C2), an interrupted **readback** (C7), dongle pulled (C9). Screen PTT and Space: the log does not tell the source.
