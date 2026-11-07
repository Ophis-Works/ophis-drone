# LOG_WAVE_DISPLAY.md — change log for PLAN_WAVE_DISPLAY.md
# (bug: "nothing showing in wave in the UI", owner report 2026-11-07)

## 2026-11-07 — PINS (step 1 of §2.2; red first, NO implementation)

Changes (pin stage only):
- src/ui/ui_draw.c:62 — removed the `static` qualifier from `item_value`
  (the hoist is part of the fix per PLAN §1.3 P1; body unchanged).
- src/ui/ui_internal.h — added the `void item_value(const ui_t*, item_t,
  char*, int)` declaration in a new "value formatting (ui_draw.c)"
  section.
- tests/test_ui_menu.c — added `test_item_value_enum_rows()` (+ the
  local `extern` mirrors of `item_value` and `content_item`, same
  pattern as the existing `param_get`/`param_set` mirrors — this TU
  keeps ui_internal.h out on purpose): a memset-zeroed `ui_t` with
  tab/sub set for `content_item`, sub[0] at factory defaults
  (div 1, WAVE_SINE, duty 50, level 0 — src/preset/defaults.c:15),
  draft.chain[0].wave=WAVE_SAW, fmode=FILT_HP, poles=24,
  xmod[0].src=1, dest=MOD_RING, tgt=2. Pins:
  - P1 (red): IT_OSC_WAVE (content_item idx 1, OSC page) == "SAW";
    IT_XMOD_DEST (idx 2, MOD page, si 0) == "RING"; IT_FILT_MODE
    (idx 4, ENV+FLT page) == "HP"; IT_FILT_POLES (idx 8) == "24DB".
  - P2 (guards): IT_XMOD_SRC (idx 0, src 1) == "OSC 2";
    IT_XMOD_TGT (idx 1, tgt 2) == "OSC 3".

Command + output (build + run, this ask):
    $ make build/tests/test_ui_menu && ./build/tests/test_ui_menu
    FAIL tests/test_ui_menu.c:369: strcmp(b, "SAW") == 0
    FAIL tests/test_ui_menu.c:375: strcmp(b, "RING") == 0
    FAIL tests/test_ui_menu.c:382: strcmp(b, "HP") == 0
    FAIL tests/test_ui_menu.c:387: strcmp(b, "24DB") == 0
    FAIL tests/test_ui_menu.c:394: strcmp(b, "OSC 2") == 0
    154/159 passed
    exit=1

Observed reds (a temporary printf, removed after recording — the
committed pin state has CHECKs only):
- WAVE row renders "" (len 0) — matches the owner report "nothing
  showing in wave".
- DEST, FILT, POLES rows also "" (len 0).
- XSRC row also "" (len 0).
- TGT ("OSC 3", no display-function call in the snprintf) passes.

Plan discrepancy found (NOT fixed here — implementation step decides):
PLAN §1.1 claims ITF_XSRC is NOT affected ("direct ui_xsrc_display
write"), but src/ui/ui_draw.c:93-96 wraps it in the SAME
`snprintf(b, blen, "%s", ui_xsrc_display((int)v, b, blen))`
through-buffer pattern, and the P2 pin proves it red. The R1 fix must
rewrite FIVE cases to direct calls (ITF_WAVE, ITF_DEST, ITF_FILT,
ITF_POLES, AND ITF_XSRC), not four, or the "OSC 2" pin stays red.

Not run here: the full suite and the ASan pass — both belong to the
verify step (PLAN §2.3). No implementation, no commit (per ask).

## 2026-11-07 — IMPLEMENT + VERIFY (step 2+3 of §2.2; pins -> green)

Scope correction (escalated to the run owner, resolved): the ask named
FOUR cases (ITF_WAVE, ITF_DEST, ITF_FILT, ITF_POLES) and required
ITF_XSRC byte-identical, but the pins stage proved ITF_XSRC
(src/ui/ui_draw.c:93-97) has the same through-buffer aliasing and its
P2 pin ("OSC 2", test_ui_menu.c:394) was red. Owner ruling: rewrite
ITF_XSRC to a direct call too — five cases total; R1's intent is "no
snprintf reads a buffer the display function writes", the four-case
list was an incomplete enumeration. Output stays byte-identical
(same "OSC n" strings, same truncation contract).

Changes (src/ui/ui_draw.c only, item_value switch):
- ITF_WAVE: `snprintf(b, blen, "%s", ui_enum_display(...))` ->
  `ui_enum_display(schema_wave_name((int)v), b, blen);`
- ITF_DEST: same -> `ui_dest_display(schema_dest_name((int)v), b, blen);`
- ITF_XSRC: same -> `ui_xsrc_display((int)v, b, blen);`
- ITF_FILT: same -> `ui_enum_display(schema_filt_name((int)v), b, blen);`
- ITF_POLES: same -> `ui_enum_display(schema_poles_name((int)v), b, blen);`
ITF_TGT ("OSC %d", no display-function call) and every other case
untouched. No other behavior change (R2).

Command + output (implementation step, this ask):
    $ make -B test        # NB: 'make -B tests' matches the tests/ dir and
                          # does nothing; the suite target is 'test'
    ...
    test_ui_menu   159/159 passed
    TOTAL: 91672/91672 passed          (exit 0 — all 25 binaries green)

    $ for t in build/tests/test_*; do "$t"; echo exit=$?; done
    every binary exit=0; per-binary tails all "N/N passed", including
    build/tests/test_ui_menu 159/159 passed (P1 four rows + P2 both
    guards green)

ASan pass (PLAN §2.3; the bug is memory-overlap UB):
    $ gcc ... -fsanitize=address,undefined -fno-omit-frame-pointer
      (same source list as the build/tests/test_ui_menu rule,
      Makefile:236-254, -O1 -g)
    $ ./build/tests/test_ui_menu_asan
    159/159 passed
    exit=0                              (no ASan/UBSan reports; temp
                                         binary removed afterwards)

Not done: commit (per ask). Working tree left with the ui_draw.c fix +
the earlier pins-stage changes uncommitted.

## 2026-11-07 — FULL-TREE AUDIT (owner ruling: fix ALL aliasing cases,
# not just the enumerated five)

Ruling applied: the pattern is "a display/format function that writes
into buffer b is passed to snprintf (or similar) WITH b as both
destination and an input argument". Audited EVERY snprintf site and
every caller-buffer display function in src/ (grep for
snprintf/ui_enum_display/ui_dest_display/ui_xsrc_display/fmt_sec
callers, then read each call site's context). Result per site:

src/ui/ui_draw.c (item_value switch, post-fix):
- ITF_WAVE/ITF_DEST/ITF_XSRC/ITF_FILT/ITF_POLES — FIXED (previous
  stage): direct calls into b, no snprintf aliasing.
- ITF_TGT (:89 "OSC %d") — CLEAN: literal format, only b written.
- fmt_sec (:55-60) — VERIFIED CLEAN as the ask required: it writes b
  directly from a double (no pointer input at all — snprintf(b, blen,
  "0.000 s"/"%.3f s"/"%.2f s")); caller ITF_SEC (:79) passes only
  (b, blen, v). No in/out overlap possible.
- Unknown-id "?" and dimmed "----" (:67/:71) — CLEAN: literals.
- ITF_AMT/ONOFF/SEQMODE/SEQONCE/STEPSEL/INT/XDIV/DUTY/PCT/SPCT
  (:100-112) — CLEAN: literal formats / static name tables
  (SEQMODE_NAMES, SEQONCE_NAMES are const arrays, never b).
- hint_binding (:238-258) — CLEAN: snprintf dests b1/b2 are distinct
  buffers; the %s sources are nine DISTINCT local label buffers
  (l_tab_b..l_menu) filled by keymap_label — no overlap with b1/b2.
- ui_draw body snprintfs (:307/:393/:451/:520/:531/:532/:535/:554/
  :569/:580) — CLEAN: every dest is a local (b/b2/lab) and every %s
  source is a literal, a struct field, or another local (lab), never
  the same buffer.

src/ui/ui_menu.c:
- :62 (dest ui->load_dir, src parent local), :64/:69/:104/:118/:181/
  :186 (literals or local err into ui->load_err/save_err), :98 (dest
  local name, src ui->load_entries), :107 (dest ui->load_dir, src
  newdir local), :148 (dest ui->load_dir, src ui->preset_dir —
  distinct fields) — ALL CLEAN.

src/ui/seq_items.c:
- ui_enum_display (:138) / ui_dest_display (:172) — CLEAN as
  primitives: they write buf while reading raw; every current caller
  passes raw from a const/static table or a literal, never buf.
- ui_xsrc_display (:154) — CLEAN: snprintf src is the frozen static
  XSRC_NAMES table, never buf.

src/ui/keymap.c: keymap_label (:131) — CLEAN: writes buf from a
  static table; both caller groups (ui_draw.c:242-250, :547-552) pass
  distinct local buffers.

src/preset/preset.c: :26 set_err (msg is a literal at every call
  site), :48 build_path (out is a local path at both callers), :67
  preset_dir (src is local exe), :108/:197/:234 (dests are locals
  tmp/path/joined), :168 (src is local jerr) — ALL CLEAN.

src/preset/schema_parse.c: :60 perr (src msg is a literal or a local
  path, never j->err), :66/:67 join_path (out is a local cpath/b at
  both callers, :258/:325), :281 (dest local cpath), :726 (src is a
  literal) — ALL CLEAN.

src/preset/schema_write.c: :67 wnum_raw (src text is a local b at both
  callers :79/:87), :78/:86 (dest locals) — CLEAN.

Additional cases found beyond the five: NONE. Therefore step (c)
(pins-first for additional cases) is vacuous — no new pins added, no
code changed in this stage. Output strings byte-identical: no edit
made at all in this stage.

Verification (this ask):
    $ make -B tests
    make: Nada a fazer para "tests".        (exit 0 — target matches
    the tests/ dir and does nothing, as the log already recorded)
    $ make -B test
    TOTAL: 91672/91672 passed               (exit 0)
    $ for t in build/tests/test_*; do "$t" || echo "FAIL: $t"; done
    ALL test_* binaries exit 0              (25 binaries)
    $ ./build/tests/test_ui_menu
    159/159 passed

Not done: commit (per ask). Working tree unchanged in this stage.
