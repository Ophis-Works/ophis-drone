# LOG_HZ_EDIT.md — change log for PLAN_HZ_EDIT.md
# (frequency rows become 5-digit odometer boxes; every numeric field
# rolls over instead of clamping; the semitone ladder dies)

## 2026-11-07 — PINS (step 1 of §2.2; red first, NO implementation)

Changes (pin stage only; no src/ file touched):
- tests/test_seq_items.c — P1 + P3:
  - P3: the old ITF_HZ pos pins (HZ_IDS loop: IT_OSC_FREQ / IT_FILT_CUT
    / IT_STEP_PITCH, fmt ITF_HZ min 0 max 212) REWRITTEN to the new
    integer-Hz bounds per R5: IT_OSC_FREQ 1..20000, IT_FILT_CUT
    20..20000, IT_STEP_PITCH 1..20000 (same value space as OSC FREQ —
    the plan names only three rows, but R9 deletes the ladder
    IT_STEP_PITCH edited through, so it needs integer-Hz bounds; flagged
    for the implementer/owner). IT_DLY_TONE rewritten ITF_TONE 0..212 ->
    20..20000; TONE_MIN_HZ == 20 pin kept.
  - P1: new pure pins (PR-G signatures from §1.3):
    hz_digits(220) -> {0,0,2,2,0}; hz_digits(20000) -> {2,0,0,0,0};
    hz_digits(1) -> {0,0,0,0,1} (return value NOT pinned — the plan
    does not define one); hz_digit_step(220,10,+1,1,20000)==230;
    (90,10,+1)->100 (carry); (100,10,-1)->90 (borrow);
    (1,1,-1,1,20000)->20000 (FREQ floor roll-over, PR-H);
    (20000,1,+1,1,20000)->1; (20000,1,+1,20,20000)->20;
    (20,1,-1,20,20000)->20000.
- tests/test_ui_menu.c — P2 + P4 + P6 (local extern mirrors of
  edit_item/item_get added beside the existing item_value/content_item
  mirrors; this TU keeps ui_internal.h out on purpose):
  - P2 test_item_value_freq_odometer(): IT_OSC_FREQ renders "00220 Hz"
    / "20000 Hz" / "00001 Hz" (freq_hz 220 / 20000 / 1); also pins the
    other two digit-box rows (R1): IT_FILT_CUT "01500 Hz" (fc 1500,
    content_item idx 5 on ENV+FLT), IT_DLY_TONE "08500 Hz" (delay.tone
    8500, content_item idx 8 on MASTER).
  - P4 test_freq_fractional_rounding(): freq_hz 55.5f renders
    "00056 Hz" while item_get still returns 55.5 (no edit); after ONE
    edit_item(+1) on the row (fresh ui -> digit cursor at the 1s
    digit, PR-E), item_get == 57 (round(55.5)=56, +1).
  - P6 test_edit_rollover(): driven through edit_item directly (the
    same edit surface val_edit reaches), item_t constructed literally:
    VEL 100 +1 -> 0 and 0 -1 -> 100; ENV 100 +1 -> -100; DUTY 95 +1
    -> 5 (wave set WAVE_SQUARE so the row is not dim); BPM 500 +1 -> 1;
    MASTER 0 -1 -> 100; SEQ DIV 16 +1 -> 1; ADSR S 100 +1 -> 0; MIXVOL
    (si=chain 1) 100 +1 -> 0 and 0 -1 -> 100.
- tests/test_range.c — P5: ALL hz-ladder pins deleted (hz_to_pos
  hostile-input, pos 0..212 round-trip, hz_to_pos(20000)==211,
  monotonicity sweep). NOTE: the file carried NO ADSR ladder pins
  before (the ADSR ladder had functions in src/ui/range.c:14-26 but no
  pins), so "keep the ADSR ladder pins" was written fresh rather than
  kept: pos 0 == 0 s, pos 1 == 0.001 s, pos 50 == 60 s
  (0.001*60000^((pos-1)/49), verified), strict monotonicity 0..50,
  exact round-trip adsr_s_to_pos(adsr_pos_to_s(pos))==pos for 1..50,
  0 s / negative floor at pos 0. These pass (guards, not red — the
  ADSR ladder is untouched per R9).

Commands + outputs (this ask):
    $ make build/tests/test_seq_items
    tests/test_seq_items.c:515:9: error: implicit declaration of
        function 'hz_digits' [-Wimplicit-function-declaration]
    tests/test_seq_items.c:525:15: error: implicit declaration of
        function 'hz_digit_step'
    make: *** [Makefile:222: build/tests/test_seq_items] Error 1
    (exit 2 — P1 compile-red as planned; P3's runtime red is masked by
    the same TU failing to compile — the spec table still carries
    0..212, so P3 is also factually red once the decls exist)

    $ make build/tests/test_ui_menu && ./build/tests/test_ui_menu
    FAIL tests/test_ui_menu.c:423: strcmp(b, "00220 Hz") == 0
    FAIL tests/test_ui_menu.c:427: strcmp(b, "20000 Hz") == 0
    FAIL tests/test_ui_menu.c:431: strcmp(b, "00001 Hz") == 0
    FAIL tests/test_ui_menu.c:438: strcmp(b, "01500 Hz") == 0
    FAIL tests/test_ui_menu.c:445: strcmp(b, "08500 Hz") == 0
    FAIL tests/test_ui_menu.c:468: strcmp(b, "00056 Hz") == 0
    FAIL tests/test_ui_menu.c:472: item_get(&u, it) == 57.0
    FAIL tests/test_ui_menu.c:492: item_get(&u, it) == 0
    FAIL tests/test_ui_menu.c:495: item_get(&u, it) == 100
    FAIL tests/test_ui_menu.c:501: item_get(&u, it) == -100
    FAIL tests/test_ui_menu.c:507: item_get(&u, it) == 5
    FAIL tests/test_ui_menu.c:513: item_get(&u, it) == 1
    FAIL tests/test_ui_menu.c:519: item_get(&u, it) == 100
    FAIL tests/test_ui_menu.c:525: item_get(&u, it) == 1
    FAIL tests/test_ui_menu.c:531: item_get(&u, it) == 0
    FAIL tests/test_ui_menu.c:537: item_get(&u, it) == 0
    FAIL tests/test_ui_menu.c:540: item_get(&u, it) == 100
    164/181 passed
    (exit 1 — 17 red: P2 x5, P4 x2, P6 x10; the pass-side P4 check
    item_get==55.5 before any edit is green as intended)

    $ make build/tests/test_range && ./build/tests/test_range
    106/106 passed
    (exit 0 — expected: P5 is a DELETED ladder + green ADSR guards,
    there is no red pin in this file)

Rationale:
- P6 drives edit_item instead of ACT_VAL_INC/val_edit because the
  pins target the EDIT RULE (the wrap branch), not the hold-repeat
  machinery (R7); edit_item is the single surface both paths reach
  (src/ui/valrepeat.c:22).
- IT_STEP_PITCH bounds (1..20000) are an inference from R9 (the ladder
  it edited through is deleted) + the OSC FREQ floor; the plan text
  does not name the row. Owner may want a different floor.
- No implementation, no commit (per the ask).

## 2026-11-07 — IMPLEMENT (step 2 of §2.2; pins were red, suite now green)

Files touched (exactly the ask's list; NO test file touched by this
stage — the three tests/ diffs predate this ask, they are the pin
writer's): src/ui/seq_items.c, src/ui/seq_items.h, src/ui/ui.c,
src/ui/ui.h, src/ui/ui_internal.h, src/ui/ui_draw.c, src/ui/range.c,
src/ui/range.h.

Changes:
- seq_items.c/h — the pure odometer helpers (PR-G): hz_digit_step(hz,
  place, dir, floor, ceil) = round(hz) + dir*place, below floor ->
  ceil, above ceil -> floor (R5 roll-over, PR-C/PR-H); hz_digits(hz,
  digits[5]) = zero-padded 5-digit split of the ROUNDED value,
  digits[0] = 10000s place (R2/R6); doc comment for the R6 fractional
  rule (display rounds, DSP keeps the fraction, the FIRST digit edit
  rounds then steps) and the PR-H edit floor (1 Hz; presets keep
  0.1..0.9) in seq_items.h; both declared there. Spec table: IT_OSC_FREQ
  1..20000, IT_FILT_CUT 20..20000, IT_STEP_PITCH 1..20000, IT_DLY_TONE
  ITF_TONE 20..20000 (P3); ITF_HZ/ITF_TONE comments rewritten (integer
  Hz, "%05d Hz").
- ui.h — ui_t gains `int freq_digit` (R3/PR-E: one field, see the
  encoding note below).
- ui.c — freq_digit_sync(): resets the digit cursor whenever the cursor
  lands on a digit-box row; called after ACT_SEL_UP/ACT_SEL_DOWN (incl.
  entering content from the sub-tab row) and ACT_TAB_BACK/ACT_TAB_NEXT
  (PR-E). ACT_NAV_LEFT/RIGHT: AFTER the existing sub-tab and SEQ
  branches (priority unchanged, PR-A), the digit cursor moves (clamped
  0..4) ONLY when the current row is a digit box AND not dimmed (R10);
  non-digit rows ignore NAV as today. edit_item(): ITF_HZ/ITF_TONE now
  step via hz_digit_step on the CURRENT digit's place, rounding the
  value first (R6: (int)lround(v), so 55.5 +1 -> 57); IT_STEP_PITCH
  (no digit cursor on the SEQ page) steps the 1s digit — digit_place()
  returns 10^freq_digit for the three digit-box ids, 1 otherwise. R11:
  the generic non-wrap clamp branch is now the SAME wrap formula as the
  selector branch (v = min + ((v - min + dir) mod n + n) mod n) for ALL
  default-branch fields (percent, signed percent, DUTY, BPM, XDIV,
  selectors); ITF_SEC wraps its pos ladder (top -> 0 s), ITF_CENTS
  wraps 0..200, ITF_AMT keeps its dest-dependent range (PITCH ±50 /
  RING 0..100 / others ±100) but WRAPS inside it; ITF_POLES stays a
  toggle; the dest-switch AMT re-clamp stays a clamp (PLAN §8.3, not an
  edit step). item_dim gate untouched; val_edit path untouched (PR-F/R7:
  same retrig, hold-repeat steps the current digit since the place is
  read per edit).
- ui_internal.h — declares item_is_digit_box() (the digit-box row
  predicate shared by ui.c and ui_draw.c). edit_item/item_get were
  already declared there (the test TU keeps mirroring them locally).
- ui_draw.c — ITF_HZ/ITF_TONE render zero-padded "%05d Hz" of
  lround(v) (P2/R2/R6); draw_sections draws the digit marker for the
  selected, undimmed digit-box row: a 2 px accent underline one glyph
  cell wide under the ACTIVE digit cell (value field at x=460, cell
  index from the left = 4 - freq_digit, PR-D).
- range.c/h — pos_to_hz/hz_to_pos DELETED (R9); ADSR seconds ladder
  untouched. grep-verified after deletion: only doc comments mention
  the names, no code consumer remains.

ENCODING NOTE (deviation from the ask's literal freq_digit mapping,
forced by a binding pin): the ask says freq_digit "resets to 4 (the 1s
digit)" with place = 10000 / 10^freq_digit. The pinned P4
(tests/test_ui_menu.c:455, not editable by this ask) drives edit_item
on a memset-zeroed ui (freq_digit == 0) and requires a 1s-digit step
(55.5 -> 57); with place = 10000/10^0 that step yields 10056, and no
place mapping can give place(0) == place(4) == 1. The pins are the TDD
ground truth (PLAN §2.2: pins first, all red), so the shipped encoding
is the OBSERVABLY IDENTICAL mirror: freq_digit 0 = the 1s digit (place
10^freq_digit), reset to 0 on row entry, LEFT moves toward the 10000s
digit (freq_digit - dir, clamped 0..4), marker cell from the left =
4 - freq_digit. Every observable behavior the plan/ask demand holds:
entry lands on the 1s digit, LEFT sweeps coarse, marker under the
active cell, step uses the selected digit's place. To restore the
ask's literal encoding, P4 must set u.freq_digit = 4 after its memset
(and the NAV dir sign and marker index flip back) — flagged for the
owner.

Commands + outputs (this ask):
    $ make build/tests/test_seq_items && ./build/tests/test_seq_items
    571/571 passed                      (P1 + P3 green)

    $ make -B test                      (the suite target is `test`;
                                         no `tests` target exists in the
                                         Makefile — asked for `make -B
                                         tests`, ran the real one)
    ... 25 binaries rebuilt and run ...
    TOTAL: 11164/11164 passed           (exit 0)

    $ for b in build/tests/test_*; do ./$b; done   (each binary
    individually; every rc=0):
    test_adsr 2427/2427; test_autoquit 12/12; test_chain 23/23;
    test_engine 12/12 (xruns: 0); test_engine_seq 2031/2031;
    test_filter 23/23; test_font 4465/4465; test_fx 25/25;
    test_json 560/560; test_keymap 46/46; test_keymap_events 36/36;
    test_keymap_events_r36s 31/31; test_keymap_r36s 39/39;
    test_mailbox 18/18; test_mixer 35/35; test_osc 32/32;
    test_presdirs 37/37; test_preset 179/179; test_range 106/106;
    test_sanity 1/1; test_seq_items 571/571; test_sequencer 221/221;
    test_theme 18/18; test_transport 35/35; test_ui_menu 181/181
    (P2 x5 + P4 x2 + P6 x10 all green; 181/181 = no regressions in the
    pre-existing menu/valrepeat pins)

    $ grep -rn "pos_to_hz\|hz_to_pos" --include=*.c --include=*.h src tools tests
    only comment lines in range.c:2, range.h:2, test_range.c:3 — no
    code consumer (grep-verified per the ask)

    $ make build/desktop/ophis-drone
    links clean (exit 0; the freq ladder's former consumers compile)

Not run (not in this ask's scope): tools/migrate_presets preset
round-trip, ASan pass, handheld relink, desktop XRUN smoke — plan §2.2
step 3 items the orchestrator may run next. Presets are untouched
(R8); test_preset 179/179 and test_presdirs 37/37 pass.

No commit (per the ask).

Rationale:
- digit_place() gates the digit place to the three R1 digit boxes so
  the SEQ page's STEP PITCH (same ITF_HZ fmt, pinned by P3) edits in
  1 Hz steps and cannot inherit a stale digit cursor from another page.
- The dest-switch AMT re-clamp (edit_item tail) intentionally stays a
  clamp: R11 governs edit steps; PLAN §8.3's "re-clamps on dest
  switch" narrows a value into a NEW range, where wrap would be
  meaningless.
- ITF_HZ's formatter change also re-renders STEP PITCH as "%05d Hz"
  (same fmt, P3 pins fmt == ITF_HZ) — display-only, consistent with
  the odometer box value space; no pin contradicts.
- ui_init needs no freq_digit line: memset zeroes it to the 1s digit
  under the shipped encoding.

## 2026-11-07 — P7 PINS (M2 step 1, §1.1a R12/R12a; red first, NO
## implementation)

Changes (pin stage only; no src/ file touched):
- tests/test_ui_menu.c — P7:
  - seq_ui(row, fd, sel) helper: memset-zeroed ui + the file's tab/sub
    setup pattern (TAB_OSC1, sub[0] = SUB_SEQ), focus[0] = 1 (ON
    content — memset zeroes it to the sub-tab row, where NAV cycles
    sub-tabs and VAL is inert), freq_digit = fd, seq_sel[0] = sel, and
    cursor written to the SEQ page's PER-TAB+SUB SLOT: index =
    tab*SUB_COUNT + sub = 2 for TAB_OSC1/SUB_SEQ (ui.h layout note) —
    NOT cursor[0]; see the pitfall below.
  - test_seq_nav_rows():
    1. PITCH row renders zero-padded: steps[3].pitch_hz = 220,
       seq_sel 3 -> content_item(idx 5) is IT_STEP_PITCH si == 3 and
       item_value == "00220 Hz".
    2. PITCH row: ACT_NAV_LEFT moves freq_digit 2 -> 3 and NEVER
       seq_sel (stays 3); ACT_NAV_RIGHT back to 2, seq_sel still 3.
    3. STEP row (content_item idx 4 == IT_SEQ_STEPSEL):
       ACT_NAV_LEFT from seq_sel 0 -> 7 (wraps down); fresh 0,
       ACT_NAV_RIGHT -> 1; freq_digit untouched (2 throughout).
    4. VEL row (idx 6 == IT_STEP_VEL): ACT_NAV_LEFT/RIGHT change
       neither seq_sel (3) nor freq_digit (1).
    5. PITCH digit edit: ACT_VAL_INC with freq_digit 0 (the 1s digit)
       steps draft.seq[0].steps[seq_sel].pitch_hz 220 -> 221 and only
       that step (steps[0] and steps[4] stay 0).
  - test_seq_nav_spec_modes() (R12a, table-driven): item_spec(
    IT_SEQ_STEPSEL)->nav == NAV_STEPSEL, item_spec(IT_STEP_PITCH)->nav
    == NAV_DIGITS, item_spec(IT_STEP_VEL)->nav == NAV_NONE.
  - main(): both called after test_edit_rollover().

Commands + outputs (this ask):
    $ make build/tests/test_ui_menu
    tests/test_ui_menu.c:648:36: error: 'item_spec_t' has no member
        named 'nav'
    tests/test_ui_menu.c:648:45: error: 'NAV_STEPSEL' undeclared ...
    tests/test_ui_menu.c:649:35: error: 'item_spec_t' has no member
        named 'nav'
    tests/test_ui_menu.c:649:44: error: 'NAV_DIGITS' undeclared ...
    tests/test_ui_menu.c:650:33: error: 'item_spec_t' has no member
        named 'nav'
    tests/test_ui_menu.c:650:42: error: 'NAV_NONE' undeclared ...
    make: *** [Makefile:247: build/tests/test_ui_menu] Error 1
    (exit 2 — the R12a compile-red, as anticipated by the ask; it MASKS
    the runtime pins, so they were observed separately:)

    $ cp tests/test_ui_menu.c /tmp/p7probe.c  # throwaway copy, spec-
      mode fn + call #if 0-ed out, then the Makefile's exact link list
    $ /tmp/p7probe
    FAIL /tmp/p7probe.c:592: u.freq_digit == 3
    FAIL /tmp/p7probe.c:593: u.seq_sel[0] == 3
    FAIL /tmp/p7probe.c:622: u.seq_sel[0] == 3
    190/193 passed
    (exit 1 — runtime reds, all in the R12 delta: pin 2 x3 (freq_digit
    does not move; seq_sel moved by LEFT; the RIGHT-side seq_sel==3
    only re-passes because LEFT's spurious 3->2 is walked back), pin 4
    x1 (seq_sel moved by LEFT; RIGHT walks it back, freq_digit inert
    both ways). Pins 1, 3, 5 are GREEN guards, not red: M1's ITF_HZ
    formatter already renders IT_STEP_PITCH "%05d Hz" (the LOG's M1
    rationale predicted this), the STEP row keeps its rectangle
    behavior by design, and the PITCH 1s-digit edit already lands via
    edit_item's non-digit-box place-1 path (verified by direct
    edit_item call in the probe: 220 -> 221).)

Pitfall recorded for the implementer (cost a debugging cycle):
- ui_handle_action-driven pins MUST write the per-tab+sub cursor slot
  cursor[tab*SUB_COUNT + sub] (slot 2 here). Writing cursor[0] leaves
  *cur_cursor() == 0, so val_edit silently edits SEQ_ITEMS[0]
  (IT_SEQ_ON, id 23) — the DBG evidence: "DBGV id=23 si=0" from an
  instrumented valrepeat.c copy — while direct content_item(&u, 5)
  calls still resolve PITCH, hiding the bug in the render pins.

Not run: full suite, ASan, smoke — out of this ask's scope (the TU
does not compile until the nav enum lands, by design). No
implementation, no commit (per the ask).

## 2026-11-07 — M2 IMPLEMENT (R12/R12a SEQ extension; P7 pins were red,
## suite now green)

Files touched: src/ui/seq_items.h, src/ui/seq_items.c, src/ui/ui.c,
src/ui/ui_internal.h, src/ui/ui_draw.c. NO test file touched by this
stage (the tests/ + LOG diff lines predate this ask — the pin writer's).

Changes (all TABLE-DRIVEN per R12a; no id comparisons, no string
checks, no per-page flags anywhere):
- seq_items.h — new nav_mode_t { NAV_NONE, NAV_STEPSEL, NAV_DIGITS }
  (NAV_NONE first, so zero-init specs are inert by default) and
  item_spec_t gains `nav_mode_t nav` (field comment: R12a, zero =
  NAV_NONE).
- seq_items.c — ITEM_SPECS entries: IT_SEQ_STEPSEL -> NAV_STEPSEL,
  IT_STEP_PITCH -> NAV_DIGITS, IT_OSC_FREQ / IT_FILT_CUT / IT_DLY_TONE
  -> NAV_DIGITS; every other entry omits nav and zero-inits to NAV_NONE
  (table comment says so).
- ui.c —
  - DELETED ID SPECIAL CASE #1: item_is_digit_box(id)
    (`id == IT_OSC_FREQ || id == IT_FILT_CUT || id == IT_DLY_TONE`)
    replaced by static is_digit_box(id) = item_spec(id)->nav ==
    NAV_DIGITS. digit_place()'s gate (was: non-digit-box Hz rows step
    the 1s digit — the M1 hack that kept IT_STEP_PITCH place-1) and
    freq_digit_sync()'s gate now go through the spec predicate, so the
    SEQ PITCH row gets the full digit cursor and resets it to the 1s
    digit on row entry (R12), like the three M1 boxes.
  - DELETED ID SPECIAL CASE #2: the ACT_NAV_LEFT/RIGHT branch's
    `cur_sub(ui) == SUB_SEQ` hardcode (moved seq_sel on EVERY SEQ row,
    (sel + dir + 8) % 8 with a magic 8) replaced by a switch on the
    current item's spec nav mode: NAV_STEPSEL -> seq_sel wraps across
    the SPEC's min..max (0..7 from ITEM_SPECS, no magic numbers);
    NAV_DIGITS -> freq_digit moves (clamped 0..4, LEFT toward the
    10000s digit) with the item_dim gate (R10: dimmed rows digit-inert
    — STEPSEL/PITCH are never dim, item_dim unchanged); NAV_NONE ->
    inert. The sub-tab-row guard keeps first priority exactly as before
    (PR-A).
  - val_edit path: NO new path — edit_item's ITF_HZ case already steps
    via hz_digit_step(round(v), digit_place(...), dir, spec min, spec
    max); with digit_place now nav-driven, X/Z on the PITCH row steps
    the CURRENT digit's place and lands in
    draft.seq[ci].steps[seq_sel].pitch_hz through the EXISTING
    item_get/item_set mapping (ITT_STEP / PK_STEP_PITCH, content_item
    binds si = seq_sel). Hold-repeat (R7) steps the current digit
    unchanged.
- ui_internal.h — DELETED the item_is_digit_box decl (id special case
  #1's header); the digit-box property is documented as
  item_spec(id)->nav == NAV_DIGITS.
- ui_draw.c — draw_sections' marker condition extended from the M1 id
  check (item_is_digit_box(it.id)) to the spec mode (nav == NAV_DIGITS);
  the marker body factored into draw_digit_marker(ui, r, y) (same
  geometry: x=460 field, cell from the left = 4 - freq_digit). NOTE:
  the SEQ page does NOT render through draw_sections (it has its own
  single-column loop), so the condition extension alone would never
  draw for IT_STEP_PITCH — the same sel && !dim && nav==NAV_DIGITS
  marker draw was added to the SEQ row loop (table-driven, no id
  checks; behavior of the other SEQ rows unchanged). Zero-padded render
  needed NO change: IT_STEP_PITCH is ITF_HZ (P3) and item_value's
  ITF_HZ/ITF_TONE cases already render "%05d Hz" (P7 pin 1 was a green
  guard in the pin run).
- item_dim gate: UNTOUCHED (ask item 6) — the SEQ rows fall in its
  default (never dim).

Commands + outputs (this ask):
    $ make -B test
    ... 25 binaries rebuilt and run ...
    TOTAL: 11185/11185 passed           (exit 0; test_ui_menu 202/202 —
    the 21 P7 checks green: spec modes, NAV row dispatch, PITCH digit
    edit; test_seq_items 571/571 — P1/P3 stay green)

    $ for b in build/tests/test_*; do ./$b; done   (each binary
    individually, rc checked; every rc=0):
    test_adsr 2427/2427; test_autoquit 12/12; test_chain 23/23;
    test_engine 12/12 (xruns: 0); test_engine_seq 2031/2031;
    test_filter 23/23; test_font 4465/4465; test_fx 25/25;
    test_json 560/560; test_keymap 46/46; test_keymap_events 36/36;
    test_keymap_events_r36s 31/31; test_keymap_r36s 39/39;
    test_mailbox 18/18; test_mixer 35/35; test_osc 32/32;
    test_presdirs 37/37; test_preset 179/179; test_range 106/106;
    test_sanity 1/1; test_seq_items 571/571; test_sequencer 221/221;
    test_theme 18/18; test_transport 35/35; test_ui_menu 202/202

    $ make build/desktop/ophis-drone
    links clean (exit 0)

    $ grep -rn "item_is_digit_box" src tests tools
    no matches (the predicate is gone, not re-implemented elsewhere)

Not run (not in this ask's scope): ASan pass, handheld relink, desktop
XRUN smoke, preset round-trip (presets untouched, R8; test_preset
179/179 green).

No commit (per the ask).

Rationale:
- The freq_digit encoding is unchanged from M1 (0 = the 1s digit, LEFT
  increases it toward the 10000s) — the P7 pins were written against
  that observable encoding (LEFT 2 -> 3) and pass without touching it.
- NAV_STEPSEL's wrap uses the spec's min..max instead of the deleted
  literal 8 so the rectangle range stays single-sourced in ITEM_SPECS
  (R12a: no magic numbers beside the table).
- The SEQ-page marker addition is the minimal way to make the NAV_DIGITS
  mode observable on the one page draw_sections never renders; without
  it R12's digit cursor on PITCH would be invisible (R3: the cursor is
  SHOWN).

## 2026-11-07 — M3 step 1: pin P8 written (red), no implementation
Files: tests/test_ui_menu.c (only).

Added test_digit_marker_focus_gate() + the extern mirror
`int ui_digit_marker_active(const ui_t *ui, item_t it);` (same
established mirror pattern as item_value/edit_item/content_item in that
TU) and registered it in main() after the P7 calls. Semantics pinned:
true iff `it` IS the cursor row && CONTENT FOCUS && !dim && spec nav ==
NAV_DIGITS (R13; the predicate the fix exposes for draw_sections —
sel_for_marker = sel && content_focus per M3).

Pins:
(a) OSC FREQ (TAB_OSC1/SUB_OSC, cursor[0]=0, focus[0]=1, wave 0=SINE so
    undimmed) -> 1.
(b) same row, focus[0]=0 (sub-tab row) -> 0 — THE bug: ui_draw.c:309's
    condition (`sel && !dim && item_spec(it.id)->nav == NAV_DIGITS`)
    has no content-focus term, so the underscore draws with the cursor
    parked on a sub-tab row.
(c) MASTER (no sub-tab row; focus[] is per OSC tab): cursor parked on
    TONE (cursor slot tab*SUB_COUNT = 12 = 8, MASTER_ITEMS[8]) -> 1 with
    focus[0] = 1 AND focus[0] = 0 (focus-independent).
Runtime red: fresh_ui -> ACT_SEL_DOWN into content (cursor 0 = FREQ) ->
1; ACT_SEL_UP (the ask's "ACT_NAV_UP"; keymap.h:19 has no nav *_UP — UP
selection is ACT_SEL_UP, which parks on the sub-tab row from the top of
content, ui.c:482-483) -> 0.

Reds recorded:
- $ make build/tests/test_ui_menu
  link RED: "undefined reference to `ui_digit_marker_active'" (x6) —
  the extern does not exist yet; compile red is a valid red per the ask.
- Runtime red observed WITHOUT implementing: a throwaway probe
  (tests/p8_red_probe.c, since deleted) shimmed ui_digit_marker_active
  to EXACTLY today's ui_draw.c:309 condition and ran the suite:
  FAIL :714 (case b — marker active on sub-tab focus) and
  FAIL :747 (runtime UP — gate not closed); 211/213. Cases (a) and (c)
  pass under today's condition once the cursor is parked on the row —
  they pin the full gate semantics, not the bug itself. Probe removed;
  nothing shimmed remains in the tree (git status: only
  tests/test_ui_menu.c modified).

Not run (not in this ask's scope): the rest of the suite (test_ui_menu
does not link, so nothing else was touched), ASan, smoke, presets.

No implementation, no commit (per the ask).

## 2026-11-07 — M3 IMPLEMENT (R13 focus gate; P8 pin was red, suite now
## green)

Files touched: src/ui/ui_draw.c, src/ui/ui_internal.h. NO other file, no
test file, no commit (per the ask).

Changes:
- ui_internal.h — declares ui_digit_marker_active(const ui_t *ui,
  item_t it) beside the digit-box section (R13): true iff `it` IS the
  cursor row AND content focus AND !item_dim AND spec nav ==
  NAV_DIGITS; documents that the ROW HIGHLIGHT keeps its own sel
  condition (R13 does not change it).
- ui_draw.c — the gate factored OUT of the two draw paths into
  ui_digit_marker_active (defined here, above draw_digit_marker):
  - cursor row: the page's cursor slot (tab*SUB_COUNT + sub for OSC
    tabs, tab*SUB_COUNT for MOD/MASTER — the cur_cursor_idx layout,
    inlined because cur_cursor() takes a non-const ui) dereferenced and
    content_item'd; sel compares id + si + name pointer against `it`
    (item_t carries no flat index).
  - content focus (R13): !is_osc_tab(ui) || ui->focus[chain_of(ui)]
    != 0 — OSC tabs (incl. the SEQ sub-tab) gate on the per-tab focus[];
    MOD/MASTER have no sub-tab row -> always content (focus-
    independent, per P8 case c).
  - !item_dim(ui, it) (R10) && item_spec(it.id)->nav == NAV_DIGITS
    (R12a, no id comparisons).
- draw_sections (was `sel && !dim && item_spec(...)->nav ==
  NAV_DIGITS` at the old :309) and the SEQ row loop (was `sel_row &&
  !dim && ...` at the old :498) both now call
  `if (ui_digit_marker_active(ui, it)) draw_digit_marker(...)`. The
  sel/sel_row and dim locals stay — the row highlight (row_cursor) and
  the text colors keep their exact prior behavior (R13: highlight
  unchanged). draw_digit_marker body/geometry untouched.

Commands + outputs (this ask):
    $ make -B test
    ... 25 binaries rebuilt and run ...
    TOTAL: 11196/11196 passed           (exit 0; test_ui_menu 213/213 —
    the 2 P8 reds from the pin stage green: case (b) sub-tab focus ->
    0 and the runtime ACT_SEL_UP -> 0; cases (a)/(c) stay green)

    $ for b in build/tests/test_*; do ./$b; done   (each binary
    individually, rc checked; every rc=0):
    test_adsr 2427/2427; test_autoquit 12/12; test_chain 23/23;
    test_engine 12/12 (xruns: 0); test_engine_seq 2031/2031;
    test_filter 23/23; test_font 4465/4465; test_fx 25/25;
    test_json 560/560; test_keymap 46/46; test_keymap_events 36/36;
    test_keymap_events_r36s 31/31; test_keymap_r36s 39/39;
    test_mailbox 18/18; test_mixer 35/35; test_osc 32/32;
    test_presdirs 37/37; test_preset 179/179; test_range 106/106;
    test_sanity 1/1; test_seq_items 571/571; test_sequencer 221/221;
    test_theme 18/18; test_transport 35/35; test_ui_menu 213/213

One mid-implementation fix (caught by the suite, kept for honesty): the
first cut of ui_digit_marker_active passed the cursor SLOT index
(tab*SUB_COUNT + sub) to content_item instead of the cursor VALUE
(ui->cursor[slot]) — P8 case (c) (MASTER/TONE, cursor 8) caught it
(211/213, FAIL :719/:721). Fixed by dereferencing the slot first.

Not run (not in this ask's scope): ASan pass, desktop/handheld relink,
XRUN smoke, preset round-trip — orchestrator items per plan §2.2.

No commit (per the ask).
