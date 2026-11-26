# Instructor guide

DRAFT, 2026-11-26. Everything stays on this machine. Nothing is sent over a network.

## 1. Your PIN

[Screenshot: login dialog, instructor keypad]

- Set or change the PIN in a terminal on the trainer machine:
  `cd /home/user/projects/atc-trainer/.worktrees/runtime && ./build/bin/set_instructor_pin`
  Do not write the PIN down in a file.
- Log in: **Log in > Instructor**, then enter the PIN on the keypad. After 5 wrong entries the keypad waits 60 s.
- After 10 minutes without input you are logged out. This never happens during a transmission.

## 2. Trainee records and consent

[Screenshot: Settings > Trainee records]

- Each trainee has an **ID** (for example S01). The name stays with the ID, on this machine only.
- **Consent to be recorded**: yes or no, with the date. Without consent, nothing of that trainee is stored.
- **Pilot voice**: a separate yes or no, for using the trainee's voice as a pilot that others hear. If this is withdrawn, the takes leave the voice bank at once. The Studio then lists them as "Consent withdrawn – delete or replace".
- If a consent text changes, the trainee is asked again at their next login. Their answer is stored only after you confirm it with your PIN.
- Until your unit approves the consent texts, nobody is asked, and **no real voice is recorded**.

## 3. Exercise plans

[Screenshot: Training > Exercises]

- In **Training > Exercises**, use **New**. Set a name, scenario, trainee (or Anyone), Practice or Assessed, fixed or random values, a briefing (at most 200 characters), difficulty, reply delays and radio effect. Then **Save**.
- The card on the right shows the **saved** plan exactly as the Dashboard will show it. While you edit, it says "Unsaved changes".
- **Assessed** needs a trainee and a Validated scenario.
- **Put on the Dashboard** selects the plan. A trainee's plan is put away when that trainee logs out. After a restart, no plan is chosen.
- **Presets...** changes Easy, Normal and Hard. Saved plans keep their values; "Use new preset values" updates one plan.

## 4. Phraseology Studio

[Screenshot: Training > Phrases]

- In **Training > Phrases**, choose a phrase. The strip under the text shows fixed words ("S") and slots ("D") with a dot for each clip: grey means missing, amber recorded, green accepted.
- If you change a text, it becomes **Draft**. **Save** it first, then set it to **Validated**, which needs the SOP reference.
- To record a clip: **Start session...**, choose the speaker, choose a clip, then use **RECORD** and **STOP** (or Space, or the pedal). Listen with Raw, Trimmed, Radio and Sentence. Then choose **Primary**, **Alternate** or **Reject**.
- **RECORDING** is shown while the microphone records. Studio recordings are never recognised and never go to an exercise.
- **Next missing** finds clips that still need a take. A missing clip blocks Start of the scenarios that need it.

## 5. Deleting results and recordings

[Screenshot: Assessment, Delete result]

- Only the instructor can delete, and every deletion is logged. Nothing is deleted automatically.
- **Assessment**: **Delete result** removes one exercise log. **Delete guest results** removes all results of exercises without a login.
- **Stored recordings** (Settings): play or delete, instructor only.
- **Studio**: a rejected take or a withdrawn take can be deleted with **Delete...**.
