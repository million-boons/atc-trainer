# Handover ui (session closed 2026-11-25, to free memory for builds)

Branch and worktree `ui`, everything committed. Next free decision number: D-141.

**User's words (pasted into ui, meant for b2):** while ui is closed, b2 may fix compile errors, warnings and failing tests in ui's files during the merges, minimal, and list them for ui. Bigger UI changes wait for ui.

## State when closed
- On `main` (79e142f): 97f09d2, 8a2df27, 5090b5a (3b; b2 reported 490 of 491 tests before its test fix, the repeat was not reported to ui). Next ba27fee, then everything up to this commit in one build.
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

## Resumed 2026-11-25, 17:38
- `main` fabbaa1 taken in; b2's changes in ui's files read and kept.
- b2's screenshots were gone (restart). New ones by ui from b2's finished 10:53 program, mock, off-screen, no build: 11 cases at 1280x800 (10 opened, not exresult), 5 at 1920x1080 (4 opened, on sheets at 3/4 size). Fixed in 44ecc8a, not built: Result titles ran together; selected model name cut off in the Lab; mock result without "Fallback PTT used"; D-321 (Run again drew a second seed, the Start dialog did not show the values).
- Seen and left: in the Lab's recording list the model name is shortened in the middle at 1280x800 ("Whisper tiny...ompt"), so two models can look the same; the full name is in the stored result.
- Not seen: the Lab with another model loaded than selected (no mock case).

## After main 53e23ef (2026-11-25, evening; 3c running, so nothing built, run or grabbed)
- Merged. Texts and tests for D-322 ("The pilot calls first.", "You called before the pilot", "Not counted as an attempt", row "Called before the pilot" in the result, timeline "ATC CALLED BEFORE PILOT") and D-325 (the key-over notice says what the pilot will do: readback repeated after the answer / answers what you say now / short tap repeats). Not compiled.
- Found while reading: the key-over notice was cleared at once, because the transmission that cuts the pilot opens right after; fixed, with a test.
- Result: more PTT rows than fit make the rows lower instead of cutting the last one off.

## Screenshots of the new program and the Studio (2026-11-25, 20:10 onward, after 3c)
- 24 mock screenshots (18 at 1280x800, 6 at 1920x1080) from the program built at 18:43; 16 opened, in pairs. Nothing to fix except the old key-over text, which c4342d0 replaces. The Lab with another model loaded than selected (case `mother`) was seen and is right. Not opened: dash, dashfault, exdone, login, setinstr, lab at 1280x800; exwrong, dashready at 1920x1080.
- Studio on the mock written (D-145), **nothing compiled or run**; only the QML syntax check.
- **For b2, `ui/CMakeLists.txt`** (b2's file): QML `components/ClipDot.qml SegmentChip.qml SegmentStrip.qml PhraseEdit.qml TrimWaveform.qml TakeList.qml RecorderPanel.qml CoverageBar.qml`, `screens/Studio.qml`; sources `StudioBackend.h StudioController.h/.cpp StudioMock.h/.cpp`. Without them this commit does not build. `tests/app/CMakeLists.txt` has the new `studio_tests`.
- `main.cpp` needs nothing: live, `Main.qml`'s `studio` property is null and the entry says why it is off.
- Mock cases: phroff, phr, phrrec, phrtake, phrwd, phrmiss, phrint.

## Studio interface (D-331) and Exercise Planner (2026-11-25, night; nothing compiled)
- 3c accepted by the user. Studio: 7415e16 (interface of 09 section 8), no new files.
- Planner on a mock (D-146). **For b2, `ui/CMakeLists.txt`:** QML `components/TrainingTabs.qml PickDialog.qml NoteEdit.qml DelayStep.qml`, `screens/Planner.qml`; sources `PlannerBackend.h PlannerController.h/.cpp PlannerMock.h/.cpp`. `tests/app/CMakeLists.txt` has the new `planner_tests`.
- `main.cpp` needs nothing: live, `Main.qml`'s `planner` property is null, the tab is disabled and the Dashboard has no "Choose plan".
- Mock cases: planoff, plan, plannew, planmis, planchg, planold, dashplan.
- Waits for the design of the Planner backend and the user's approval: plan in `ExerciseView` and `LiveExercise`.
- D-147: the user's review of D-145 and D-146; "Use new preset values" added. D-145 is confirmed finally by the user on the screenshots.
- Next free ui decision number: D-148.
