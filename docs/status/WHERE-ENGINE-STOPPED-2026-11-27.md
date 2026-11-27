# Where engine stopped, 2026-11-27

1. Circuit – Rajali is written (D-430 to D-435; design `docs/architecture/12-circuit-rajali.md`; all C-points answered), and the departure moved from LINE_UP to LINE_UP_AND_WAIT with the circuit's readback line (D-433); engine head is on branch `engine`, b2 merges and builds it (only `edc7669` compiled so far: 828/829, the failure was b2's own test).
2. Still to do: b2 fills the header's `pttStandIn` in the runtime (D-435); stt removes intent LINE_UP after b2's build passes; the Piper run, only with the user's word, for the 30 missing clips listed in 12 §6. Until it runs, the circuit **and the departure** are refused at Start by the coverage check.
3. Open: the loader does not yet check scenarios' objectives against `data/objectives/placeholder.json`; "clear to land" to be re-checked with the dev speakers (D-434).
