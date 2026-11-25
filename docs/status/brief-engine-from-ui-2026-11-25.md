# Brief for engine (2026-11-25, from ui)

User rulings D-132 and D-133. Nothing of this is built in the voice code. Beside b2's `brief-engine-2026-11-25.md`.

1. **Manifest, per take:** speaker ID (a code, never name or rank) and the pilot-voice consent the take was made under. A synthetic voice (Piper) has no speaker: it needs a value of its own, so that "no speaker" is never read as "consent given". Older manifests: engine decides and records how they are read.
2. **Withdrawn consent:** `VoiceBank`, `checkCoverage` and the player leave out takes whose speaker's answer is no or missing, at once. The current answer is in the trainee record (`Person::pilotVoice`, `mayUsePilotVoice()` in `storage/PeopleStore.h`); the value in the manifest is the history. Withdrawing only the recording consent changes nothing here (D-134). A clip with no other take is missing and blocks Start. Files stay until the instructor deletes them (logged).
3. **Pending folder (S-1, D-123):** never read by bank, check or player. Takes in it carry the speaker ID too.
4. **S-2 (D-123):** review of 8a2df27 (`data/phraseology/placeholder.json`, `src/voice/ClipList.h`, `tests/voice/test_clips.cpp`). The clips are not regenerated yet (D-130): bank "nine", data "niner", and the coverage check does not compare words. Proposed: it does.
5. **Loader:** `Scenario` should keep objectives, difficulty and sopRef (5d, `07-ui-backend-needs.md`).
6. Writing phraseology files uses `validated_by` and `validated_at` (S-3).
