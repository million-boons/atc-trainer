# Proposed skill changes, 2026-11-24

Skills are read-only here; apply on claude.ai. Each is from a recorded decision or a conflict in AUDIT-2026-11-24.md; undecided ones are marked.

| Skill | Section | Old text | New text |
|---|---|---|---|
| atc-scenario-pilot-engine | Phraseology Studio, Data model | `` Data model (`config/phraseology/*.json`, schema-validated): `` | `` Data model (`data/phraseology/*.json`, checked by the loader, D-45/D-408): `` |
| atc-scenario-pilot-engine | Scenario configuration | `- Ship a JSON Schema and a validator; reject invalid scenario files at load with clear errors.` | `- Ship a JSON Schema as documentation; hand-written loader checks reject invalid scenario files with file and JSON path (D-408).` |
| atc-ui-console | App shell | `status chips (Mic, Speaker, Foot PTT, STT, TTS) with ok/caution/fault` | `status chips (Mic, Speaker, Foot PTT, STT, VOICE BANK) with ok/caution/fault (D-27)` |
| atc-ui-console | Architecture rules | `all audio/STT/TTS on worker threads` | `all audio/STT/voice-bank work on worker threads` |
| atc-ui-console | Screens, Speech & PTT Lab | `recording history list with export.` | `recording history list (export off for the slice, D-407).` |
| atc-sim-architect | Screens, 4 | `exportable recording history.` | `recording history (export off for the slice, D-407).` |
| atc-sim-architect | Fixed constraints | `pilot never talks while pedal held.` | `pilot speech never starts while the pedal is held, and no transmission is open while the pilot speaks (D-20).` |
| atc-sim-architect | Sibling skills line | `pilot behaviour, TTS/radio DSP, assessment` | `pilot behaviour, voice bank/radio DSP, assessment` |

Undecided, need your ruling first:

| Skill | Section | Old text | Proposed text, if approved |
|---|---|---|---|
| atc-speech-ptt-lab | Foot PTT pedal | `- udev rule + group so the app runs without root;` | `` - udev rule with `uaccess` so the app runs without root; `` (D-200, uncommitted; W-1) |
| atc-speech-ptt-lab | Decisions log | `- 2026-11-24: Slice STT model = Whisper tiny + generated prompt, locked per exercise.` | name the exact model id after W-11 |
| atc-scenario-pilot-engine | Pilot voice, Runtime | `random 0.6–1.5 s reply delay` | `random 0.5–1.5 s reply delay (03 §3)` or change 03 |
