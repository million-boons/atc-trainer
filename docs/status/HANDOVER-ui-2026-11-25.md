# Handover ui (session closed 2026-11-25, to free memory for builds)

Branch and worktree `ui`, everything committed. Next free decision number: D-141.

**User's words (pasted into ui, meant for b2):** while ui is closed, b2 may fix compile errors, warnings and failing tests in ui's files during the merges, minimal, and list them for ui. Bigger UI changes wait for ui.

## State when closed
- On `main` (e471207): 97f09d2, 8a2df27. Being merged by b2: 5090b5a (3b; b2: 490 of 491 tests before its test fix). Then ba27fee, then everything up to this commit in one build.
- **Not compiled or tested by ui:** every code commit from 5090b5a on.
- Fixed by b2 in the merges so far: `src/app/EngineHeaders.h` (Qt's `slots`; includes of ExerciseView.h, LiveExercise.h/.cpp, test_live_exercise.cpp), `-Wsequence-point` at `++calls_` in ExerciseView, `Ui.InstructorChoiceLeavesTheRadioBarFree`.

## When ui resumes
1. Take `main` into `ui`; read b2's list of changes in ui's files.
2. Look at the screenshots, both sizes: exchoice, exlost, dashready, exstart, lost, exresult, exresulta, setinstr, the Lab with another model loaded than selected; fix what they show.
3. Then the Phraseology Studio screens and tests (04 §5), first of Stage 2 (D-136); interface "Studio interface" in `07-ui-backend-needs.md`, b2 objects first. Then 5d, 5e, 5f.
4. Qt files include engine headers only through `src/app/EngineHeaders.h`.
5. No build without b2's slot; no Piper run by ui (D-128).

## Open
- "niner" clips deferred (D-130): the bank says "nine", the data "niner"; the coverage check does not compare words.
- Re-consent at login and the "wider use" mark (D-137, D-139): before a second approved consent text.
- Consent texts are drafts until the user confirms the unit's approval (D-134): then the version in `src/core/Consent.h`.
- Engine: `brief-engine-from-ui-2026-11-25.md` and b2's brief.
