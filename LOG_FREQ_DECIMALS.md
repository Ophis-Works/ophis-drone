# LOG_FREQ_DECIMALS.md

## 2026-11-07 — PINS stage (workflow subagent "Pin writer")

PLAN_FREQ_DECIMALS.md §1.3 P1–P4 written red-first. NO implementation, NO
commit (per stage order §2.2 step 1).

### Files changed (tests only)
- `tests/test_seq_items.c`
  - P1: the pure-helper pins rewritten to the centi-Hz space
    (22000 = 220.00 Hz). `hz_digits(5550, d)` now expects SEVEN digits
    `{0,0,0,5,5,5,0}` ("00055.50" — integer 00055, decimals 50); the old
    5-digit pins (220/20000/1) are deleted. `hz_digit_step` pins:
    `(22000, 100, +1, 1, 2000000) -> 22100`; carry `(1999000, 1000, +1,
    1, 2000000) -> 2000000`; FREQ-floor roll `(1, 1, -1, 1, 2000000) ->
    2000000`; ceiling roll `(2000000, 1, +1, 1, 2000000) -> 1`; CUT/TONE
    20.00-floor roll `(2000, 1, -1, 2000, 2000000) -> 2000000`. The
    old integer-Hz borrow/ceiling->20 pins are deleted per the ask's
    pin list. Rationale: PLAN §1.3 P1, R2 (7 places 10000..0.01), R4.
  - P4: spec bounds in the centi-Hz edit space — IT_OSC_FREQ and
    IT_STEP_PITCH `min >= 1 && min <= 100 && max == 2000000` (floor is
    the fmt's documented 0.01 Hz, implementer's choice pinned as a
    range per the ask; max 20000.00 Hz = 2000000 centi-Hz); IT_FILT_CUT
    and IT_DLY_TONE `2000..2000000` (R4 keeps 20..20000 Hz = 2000..
    2000000 centi-Hz). `TONE_MIN_HZ` pinned to `2000` and to remain the
    single source of IT_DLY_TONE's min (the constant's unit moves with
    the spec's centi-Hz space). Note: the CUT/TONE/TONE_MIN_HZ pin
    rewrite goes one step beyond the ask's literal P4 (which names only
    IT_OSC_FREQ/IT_STEP_PITCH) — required so no green pin contradicts
    the centi-Hz table R1/R4 mandate.
- `tests/test_ui_menu.c`
  - P2: `test_item_value_freq_odometer` — render pins
    "00220.00 Hz", "20000.00 Hz", "00000.01 Hz" (the 0.01 floor) and
    "00055.50 Hz" (fraction SHOWN, not rounded — R5) on the OSC FREQ
    row; FILTER CUT "01500.00 Hz", DELAY TONE "08500.00 Hz" (R1: every
    frequency box gains the period + 2 decimals).
  - P2: `test_freq_fractional_rounding` — display "00055.50 Hz"
    (replaces the old "00056 Hz" rounding pin, R5), `item_get` stays
    55.5, one 1s-digit step -> 56.5 (5550 + 100 centi-Hz; replaces the
    old round-then-step 57.0). The 1s digit is explicitly addressed as
    `freq_digit = 4` (index 4 of 0..6 from the left).
  - P2: `test_seq_nav_rows` — PITCH render "00220.00 Hz"; ROW ENTRY
    resets the digit cursor to index 4 (the 1s digit, first
    non-decimal digit — R3; driven via ACT_SEL_DOWN/ACT_SEL_UP through
    `freq_digit_sync`); RIGHT walks 4 -> 5 -> 6 and ACT_VAL_INC at
    index 6 steps +0.01 (220.00 -> 220.01); LEFT walks 4 -> 3 -> 2 ->
    1 -> 0 and ACT_VAL_INC at index 0 steps +10000.00 Hz (220.00 ->
    10220.00); the 1 Hz-step pin moved to `freq_digit = 4`
    (220.00 -> 221.00). STEP/VEL row pins unchanged.
  - DISCREPANCY resolved in favour of the binding plan: the stage ask
    said "NAV_LEFT from index 4 -> index 0 and a step moves +10000
    centi-Hz (100.00 Hz)". Index 0 is the 10000s place per R2/R3
    (places 10000..0.01), so one step there is +10000.00 Hz =
    1,000,000 centi-Hz, pinned as `220.0f -> 10220.0f`. The ask's
    "+10000 centi-Hz (100.00 Hz)" would place the 100 s digit at index
    0 and contradicts R2; it reads as an arithmetic slip (place value
    10000 taken as centi-Hz without the x100). Flagged for the
    implementer/owner.
- `tests/test_json.c`
  - P3: the freq_hz/pitch_hz "0.05 rejects" entries are REPLACED with
    "0.009 rejects" (sub-centi values reject after R4); the inclusive
    boundary parses move 0.1 -> 0.01 for both freq_hz and step
    pitch_hz (R4 floor widening). 0.009 already rejects under the
    current 0.1 floor (forward-pin, green today); 0.01 parses is the
    red.

### Checks run (exact commands + results)
- `make build/tests/test_seq_items build/tests/test_ui_menu build/tests/test_json`
  — all three link (pre-existing warnings in test_json.c only).
- `./build/tests/test_seq_items` (exit 1) — 561/567, 6 reds:
  - :265 IT_OSC_FREQ bounds (max 20000 != 2000000)
  - :270 IT_FILT_CUT bounds (2000..2000000)
  - :274 IT_STEP_PITCH bounds
  - :351 IT_DLY_TONE bounds
  - :356 TONE_MIN_HZ == 2000 (single-source relation)
  - :525 hz_digits(5550) 7-digit split
  (The five hz_digit_step odometer pins hold in both spaces — pure
  carry/roll arithmetic — as predicted; the new contract red is the
  7-digit splitter.)
- `./build/tests/test_ui_menu` (exit 1) — 206/224, 18 reds: the six
  render pins (:424,:428,:432,:436,:443,:450), the fractional pins
  (:478,:482), the PITCH render (:595), the row-entry reset to 4
  (:606), the RIGHT walk 5/6 + 0.01 step (:617,:620,:622), the LEFT
  walk 3/2/1/0 (:634,:636,:638,:640), the 1 Hz step at index 4 (:683).
  (:642's 10220.0f passes today by coincidence — the old 0..4 from-
  right cursor at 4 IS the 10000s place; the walk-index pins are the
  real reds there.)
- `./build/tests/test_json` (exit 1) — 559/560, 1 red:
  - :981 inclusive-boundary parse (freq_hz 0.01 / pitch_hz 0.01
    rejected by the current 0.1 floors)

Suite state: red as required before implementation. Not run (not in
this stage): forced-rebuild full suite, preset round-trip, smoke, ASan,
commit — M1 implement step.

## 2026-11-07 — IMPLEMENT stage (workflow subagent "Implementer")

PLAN_FREQ_DECIMALS.md §2.2 step 2 executed on the P1–P4 red pins. NO
commit (per the stage ask).

### Files changed (src only; tests untouched — pin writer's)
- `src/ui/seq_items.h`
  - `ITF_HZ`/`ITF_TONE` fmt doc comments rewritten to the centi-Hz edit
    space (R2/R4: 7 editable digits 10000..0.01 Hz, min/max in centi-Hz,
    OSC FREQ / SEQ PITCH floor 0.01 = 1; display "%05d.%02d Hz").
  - `TONE_MIN_HZ` 20 -> 2000 (R4: 20.00 Hz = 2000 centi-Hz; the unit
    follows the spec's centi-Hz space — single source of IT_DLY_TONE's
    min, per the P4 pin).
  - Helper contract block rewritten: all values CENTI-HZ ints (PR-B);
    `hz_digits` splits into SEVEN digits 10000s..0.01s, the period is a
    fixed separator, not a digit; `hz_digit_step` roll-over at
    [floor, ceil] with the R2 floors (FREQ 0.01, CUT/TONE 20.00).
  - R5 fractional rule reworded: a preset fraction is DISPLAYED exactly
    ("00055.50") and keeps its fraction until the first digit edit, which
    snaps through centi-Hz ints (55.5 +1 on the 1s digit -> 5550+100 ->
    56.50) — replaces the old round-first R6 text.
  - Signatures: `hz_digit_step(int centi_hz, int place_centi, int dir,
    int floor, int ceil)`, `hz_digits(int centi_hz, int digits[7])`.
  - `NAV_DIGITS` comment: cursor 0..6 from the left, reset to the 1s
    digit (index 4, first non-decimal digit — R3) on row entry.
- `src/ui/seq_items.c`
  - `hz_digit_step`: centi-Hz ints straight through (no lround re-snap;
    the input is already exact), roll-over unchanged in shape.
  - `hz_digits`: 7-digit split, clamps [0, 2000000] (20000.00 display
    ceiling), returns the centi-Hz value.
  - ITEM_SPECS bounds to the centi-Hz edit space (R2/R4): IT_OSC_FREQ
    and IT_STEP_PITCH `1..2000000` (0.01..20000.00 Hz); IT_FILT_CUT and
    IT_DLY_TONE `2000..2000000` (20.00..20000.00 Hz). Everything else
    untouched.
- `src/ui/ui.c`
  - `digit_place`: TABLE-driven centi-Hz places {1000000, 100000, 10000,
    1000, 100, 10, 1} indexed by freq_digit (replaces the 10^fd loop;
    bounds-guarded 0..6).
  - `freq_digit_sync`: reset constant 0 -> 4 (R3: the 1s digit, first
    non-decimal digit).
  - ACT_NAV_RIGHT/LEFT clamp 0..6 — and the DIRECTION FLIPPED
    (`freq_digit + dir`, was `- dir`): freq_digit now counts FROM THE
    LEFT, so RIGHT (+1) walks toward the 0.01s digit and LEFT (-1)
    toward the 10000s. Without the flip the P2 walk pins (4 -> 5 -> 6
    rightward) went red; caught by the first suite run, fixed, re-run
    green.
  - `edit_item` ITF_HZ/ITF_TONE merged into one centi-Hz branch:
    `v = hz_digit_step((int)lround(v * 100.0), digit_place(...), dir,
    sp->min, sp->max) / 100.0` — the float param value snaps through
    centi-Hz ints (R5 fractional rule), steps in the selected digit's
    place, converts back to Hz for item_set (params keep floats:
    v_hz = centi / 100.0). Spec min/max are already centi-Hz.
  - Header range doc updated (centi-Hz 7-digit boxes, floors).
- `src/ui/ui_draw.c`
  - `item_value` ITF_HZ/ITF_TONE: "%05d.%02d Hz" — centi =
    lround(v*100), integer part centi/100, decimals the remainder; a
    preset fraction is SHOWN (R5), not rounded.
  - `draw_digit_marker`: cell index from the left = freq_digit for the
    integer digits (0..4) and freq_digit + 1 for the decimal digits
    (5 -> 6, 6 -> 7) — the period cell (5) is skipped (PR-C). Doc
    comment rewritten for the 8-cell layout.
- `src/preset/schema_parse.c`
  - KT_FNUM floors 0.1 -> 0.01 for `freq_hz` (CHAIN_KEYS) and
    `pitch_hz` (STEP_KEYS) — R4; `fc`/`tone` unchanged.

### Checks run (exact commands + results)
- `make -B test` (forced rebuild, whole suite) — run twice:
  - 1st run: `make: *** Error 1`, 11195/11203 — 8 reds in
    tests/test_ui_menu.c (:617,:620,:622,:634,:636,:638,:640,:643), all
    the NAV-walk direction pins. Root cause: the NAV_DIGITS clamp kept
    the old `- dir` sign while freq_digit moved to a from-the-left
    index. Fixed (`+ dir`), documented above.
  - 2nd run: `TOTAL: 11203/11203 passed`, exit 0.
- Every binary, individually (`for t in build/tests/test_*; do ./$t;
  done`): all 26 binaries exit 0 — test_adsr 2427/2427, test_autoquit
  12/12, test_chain 23/23, test_engine 12/12 + xruns 0, test_engine_seq
  2031/2031, test_filter 23/23, test_font 4465/4465, test_fx 25/25,
  test_json 560/560, test_keymap 46/46, test_keymap_events 36/36,
  test_keymap_events_r36s 31/31, test_keymap_r36s 39/39, test_mailbox
  18/18, test_mixer 35/35, test_osc 32/32, test_presdirs 37/37,
  test_preset 179/179, test_range 106/106, test_sanity 1/1,
  test_seq_items 567/567, test_sequencer 221/221, test_theme 18/18,
  test_transport 35/35, test_ui_menu 224/224.
- ASan on the touched tests (M1 §2.3): gcc -O1 -g -fsanitize=address
  builds of test_seq_items + test_json + test_ui_menu in /tmp/ophis_asan
  (test_ui_menu under SDL_VIDEODRIVER=dummy) — all three exit 0, full
  counts (567/560/224), no AddressSanitizer/LeakSanitizer reports.
- Preset round-trip: covered by test_preset 179/179 in the runs above
  (existing shipped presets unaffected — R4 only widens the parse
  floor; the 0.01 boundary parse is pinned in test_json 560/560).

### Not run (not in the stage ask)
- `make handheld` / R36S deploy artifacts; smoke beyond test_engine's
  xruns: 0 (the ask's check list ends at the suite + green).

Suite state: GREEN. Nothing committed (per the ask).
