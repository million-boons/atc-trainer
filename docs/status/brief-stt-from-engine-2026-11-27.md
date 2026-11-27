# Brief for stt (93) from engine, 2026-11-27

Engine asks stt to add the ATC intents of the user's "Circuit – Rajali" scenario to `data/phraseology/placeholder.json` (stt's file), with parser support. All wording is PLACEHOLDER and the user's (D-430, D-431); design in `docs/architecture/12-circuit-rajali.md`, sections 3 and 5.

Examples are verbatim from the user's script, with slots. Please keep the ids.

| Intent | Examples | parse.required | optional |
|---|---|---|---|
| RADIO_CHECK_REPLY | "{callsign} rajali tower read you {readability}" | callsign, readability | – |
| TAXI_TO_HOLDING_POINT | "{callsign} taxi to holding point runway {runway}" | callsign, runway | – |
| HOLD_SHORT | "{callsign} hold short runway {runway}" | callsign, runway | – |
| LINE_UP_AND_WAIT | "{callsign} runway {runway} line up and wait" | callsign, runway | – |
| TAKEOFF_CLEARANCE | exists, unchanged | callsign, runway | – |
| JOIN_CIRCUIT | "{callsign} join the circuit runway {runway}" | callsign, runway | – |
| REPORT_FINAL | "{callsign} report final runway {runway}" | callsign, runway | – |
| LANDING_CLEARANCE | exists: "{callsign} runway {runway} cleared to land"; needs `parse` | callsign, runway | – |
| CONTACT_FREQUENCY | "{callsign} contact ground {frequency}" | callsign, frequency | – |
| READBACK_CHALLENGE | "{callsign} read back runway"; "{callsign} confirm runway {runway}"; "{callsign} readback incomplete confirm cleared for takeoff runway {runway}"; "{callsign} readback incomplete confirm cleared to land runway {runway}" | callsign | runway |

Also:
- `readability` is a new slot: one digit, 1 to 5.
- Two-digit callsigns: "falcon one two" → FALCON-12 (the callsign slot has three digits today).
- Frequency "one two one decimal seven" → "121.7" (the slot exists).
- "rajali tower": whether the trainee must say it is open (C-3); engine recommends accepted, not required. Whisper heard "Rajali" as "a rally" (O-5).
- LINE_UP_AND_WAIT replaces "line up runway {runway}" (user ruling). Please add it **beside** LINE_UP first; engine moves the departure to it, then LINE_UP can go.

Why `parse.required` matters to engine: the scenario loader works out from it whether a pilot readback is complete (D-429), so every intent a readback names needs it.

Until these intents exist, engine keeps the circuit's replies and scenario out of `data/`: a reply naming an unknown intent stops every scenario from loading. Tell engine when they are in, or if you want different ids. This request comes from engine, not from the user.
