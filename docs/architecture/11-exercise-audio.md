# 11 Exercise audio with consent (D-108): backend (proposal, for the user's approval)

By b2, 2026-11-27 (D-340 queue). **No code before approval.** Binding: D-101, D-102, D-108, D-112, D-121, D-133 to D-135, D-312, D-314, D-412, D-44. Today nothing of an exercise is stored but its log (D-314); the screens say "Exercise audio not saved" until a backend says it stores audio (D-121). Points for the user: **EA-1 to EA-6** (section 5).

## 1. What is stored, and when

| Rule | Proposed |
|---|---|
| Whose | The logged-in **trainee** of the exercise, with recording consent **yes** today (`mayRecord`, D-135 versions). Never a guest, never the instructor's own login, never in the Studio |
| Switch | The Lab's save switch of D-101 covers exercise audio too (**EA-1**) |
| What | Each closed transmission as it was cut from the radio: pre-roll, speech, tail (as the recogniser got it). Never the pilot's audio, never anything without PTT (D-312) |
| Checked when | At Start (the result screen and the Start dialog say "saved" or why not), and again before each file is written; a "no" in between writes nothing more |
| Not stored | A short tap; a transmission of an exercise whose log could not be created |
| Disk | Below the Lab's limit (200 MB free): not saved, noted once in the result ("Exercise audio not saved: disk nearly full"); the exercise goes on |

## 2. Where and how

- `<writable>/exercise-audio/<session>/tx<N>.wav` (16 kHz mono, owner only, folder 0700) and `index.jsonl` beside it: tx, file, samples, speaker ID, consent date and version, written at.
- Written by a worker thread (as `RecordingStore`), never on the radio's or the GUI thread; the audio leaves memory when written.
- Nothing in git (D-31); a copy to the PC only as D-412 (manual, LAN, logged).
- The event log is not changed: the audio is found by session and transmission number.

## 3. Use

- Assessment (5e): `HistoryEntry.audio` true when the file exists **and** the consent is still yes (**EA-2**); play the whole transmission or the keyed part only, through the radio's local playback (refused while the radio is in use, as the Lab's playback).
- Deleting a result (instructor) deletes its audio, after the line in `deletions.log`.
- No automatic deletion (D-102).

## 4. Tests (no hardware, no person's voice)

Guest, no consent, switch off, disk full: nothing written (positive control with consent); a consent "no" during a run stops further files; files owner only; playback refused for a withdrawn consent (if EA-2); deleting a result deletes its audio, logged first.

## 5. The user decides

| # | Question | Recommended |
|---|---|---|
| EA-1 | The Lab's save switch also switches exercise audio | yes |
| EA-2 | Consent withdrawn later: the files stay until the instructor deletes them (D-102), but they are no longer played | yes |
| EA-3 | Assessed and Practice both store audio under the same rules | yes |
| EA-4 | Audio of an exercise is kept as long as its result | yes |
| EA-5 | The trainee may play their own audio in their history; the instructor all | yes |
| EA-6 | The pre-roll and tail are kept with the speech (as the recogniser heard it) | yes |
