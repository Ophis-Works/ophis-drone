# LOG_OSC_NAMING.md — changes + rationale (PLAN_OSC_NAMING.md §2.2, subagent 1: PINS)

## Scope
Pins only, NO implementation, NO commit. Touched files:
- tests/test_seq_items.c — P1, P2, P3, P4, P5 (plan §1.4)
- tests/test_mixer.c — PR1 (comment reference only), PR2
- tests/test_json.c — PR3
Rationale: strict TDD (AGENTS.md) — pins written first, all red before any
implementation change; compile errors count as red (plan §2.2 step 1).

## test_seq_items.c (appended before HARNESS_REPORT)
- P1 (§1.0a/R1): MOD_ITEMS names are "SOURCE","OSC","DEST","DEPTH","LEVEL"
  per slot, si == i/5. RED (runtime): 12 fails — the 4 renamed labels ×
  3 slots (DEST already matches, si checks pass).
- P2 (§1.0a/R4): MASTER_ITEMS[3..5] are "OSC A"/"OSC B"/"OSC C" at si
  0/1/2; MASTER_SECTIONS[1].label "MIX" pinned UNCHANGED (that check is
  green now, by design). RED (runtime): 3 fails (the three row names);
  si checks and the section title pass.
- P5 (§1.0/R0e, §1.4): item_spec(IT_XMOD_DEST) min 0, max 7, wrap 1.
  RED (runtime): 1 fail (max == 7; min/wrap already hold).
- P3 (§1.3, §1.4): item_spec(IT_XMOD_TGT)->fmt == ITF_TGT; min 0/max 2/
  wrap 1/tgt ITT_XMOD. RED (compile): `'ITF_TGT' undeclared`
  (tests/test_seq_items.c:465) — seq_items.h has no ITF_TGT yet.
- P4 (§1.3, §1.4): ui_xsrc_display(0/1/2/6/7/9) -> "OFF"/"OSC 2"/
  "OSC 2 SUB 1"/"OSC 3 SUB 2"/"OSC 1"/"OSC 1 SUB 2". RED (compile):
  `implicit declaration of function 'ui_xsrc_display'`
  (tests/test_seq_items.c:478) — expected red until the implementer adds
  the pure SDL-free function to seq_items.c.
  Commands + outputs (in order written):
  1. `make build/tests/test_seq_items && ./build/tests/test_seq_items`
     (P1/P2/P5 only): 536/552 passed, 16 FAIL lines, rc=1.
  2. after adding P3/P4: `make build/tests/test_seq_items` — build
     fails, 2 errors (ITF_TGT undeclared; implicit ui_xsrc_display).

## test_mixer.c
- PR1 (§1.0g/R0b): NO new pin. The existing block at tests/test_mixer.c
  ("amp / filter destinations carry amount*m into the right field") is
  the matrix-level FLT_CUT spec — dest MOD_FILTER (renamed "flt_cut",
  §1.0e) accumulates filter_mod = amount/100 × src exactly as today.
  Added only the explicit FLT_CUT comment reference (per the ask: do not
  duplicate).
- PR2 (§1.0 R0c): new mixer-level FLT_RES pin — dest MOD_FRES
  accumulates res_mod = amount/100 × src into xm->res_mod (the NEW 5th
  xmod_in_t field); filter_mod pinned untouched. RED (compile):
  - `'MOD_FRES' undeclared` (tests/test_mixer.c:226) — types.h
  - `'xmod_in_t' has no member named 'res_mod'` (tests/test_mixer.c:228)
    — chain.h
  Command: `make build/tests/test_mixer` — build fails, 2 errors.

## test_json.c
- PR3 (§1.0e/R0g): appended section 10 — dest "flt_res" parses with
  amount 0, "flt_res" amount 101 rejects, dest "flt_cut" parses, old
  dest "filter" REJECTS (new format only). Deliberately string-level
  (no MOD_FRES reference) so the reds stay RUNTIME reds.
- Command: `make build/tests/test_json && ./build/tests/test_json` —
  built clean; 557/560 passed, rc=1:
  - FAIL test_json.c:1188 "flt_res" amount 0 parses — RED (unknown
    string rejects today, schema_parse.c val_enum)
  - FAIL test_json.c:1207 "flt_cut" parses — RED
  - FAIL test_json.c:1216 "filter" rejects — RED (it parses today)
  - "flt_res" amount 101 reject already passes (unknown string rejects)
    — green now, meaningful once the 8-entry dest_tab lands.

## Verified unchanged
- git status: only the three test files modified (no src/ changes).
- Not run (out of pin scope): full suite, presets rewrite (PR4), DSP
  pin (plan R0g PR5), desktop smoke — all belong to later milestones.

## Subagent 2: IMPLEMENT (owner order 2026-11-07, PLAN_OSC_NAMING.md §2.2 step 2)

Scope: implementation only, presets rewritten, NO commit. The pins were
verified red FIRST against the unmodified src (this session):
- `make build/tests/test_seq_items` — 2 compile errors (ITF_TGT undeclared;
  implicit ui_xsrc_display), matches the pin log.
- `make build/tests/test_mixer` — 2 compile errors (MOD_FRES undeclared;
  xmod_in_t has no member res_mod), matches the pin log.
- `./build/tests/test_json` — 557/560, 3 runtime FAILs (1188 flt_res parses,
  1207 flt_cut parses, 1216 filter rejects), matches the pin log.

### src/ui/seq_items.h
- item_fmt_t gains ITF_TGT (after ITF_DEST): the TARGET selector display —
  "OSC %d" (v+1), R2. Editing needs no change (generic wrap path).
- Declared ui_xsrc_display(int v, char *buf, int cap) and
  ui_dest_display(const char *raw, char *buf, int cap) (both pure,
  SDL-free, testable; R2/R3/R0f).

### src/ui/seq_items.c
- MOD_ITEMS labels SRC/TGT/DEST/AMT/MIX -> SOURCE/OSC/DEST/DEPTH/LEVEL per
  slot (R1); ids and si unchanged. MASTER_ITEMS MIX A/B/C -> OSC A/B/C
  (R4); section title "MIX" stays.
- ITEM_SPECS: IT_XMOD_TGT fmt ITF_INT -> ITF_TGT (min 0/max 2/wrap 1 kept);
  IT_XMOD_DEST max 6 -> 7 (R0e ladder gains flt_res).
- Implemented ui_xsrc_display over the FROZEN 10-entry table
  OFF, OSC 2, OSC 2 SUB 1, OSC 2 SUB 2, OSC 3, OSC 3 SUB 1, OSC 3 SUB 2,
  OSC 1, OSC 1 SUB 1, OSC 1 SUB 2 (id order 0..9; 1..3=B, 4..6=C, 7..9=A
  appended, M2). Out-of-range v -> "".
- Implemented ui_dest_display: UPPERCASE, '_' -> ' ' ("flt_cut" ->
  "FLT CUT"). Raw preset strings stay in the preset layer (S1 stands).

### src/ui/ui_draw.c
- item_value(): ITF_TGT case prints "OSC %d" (v+1); ITF_XSRC now calls
  ui_xsrc_display (was the ui_enum_display/schema_xsrc_name pair);
  ITF_DEST now calls ui_dest_display (was ui_enum_display — '.' separator
  would have shown "FLT.CUT"). schema_xsrc_name/schema_dest_name stay RAW
  for the preset layer.

### src/dsp/types.h
- mod_dest_t gains MOD_FRES AFTER MOD_FM (ids 0..6 never renumbered —
  "flt_res" is the new string mapped to MOD_FRES in dest_tab; table order
  need not match enum order).

### src/dsp/chain.h
- xmod_in_t gains float res_mod (5th member) — FLT_RES accumulation input.

### src/dsp/mixer.c
- mod_apply: MOD_FRES -> acc->res_mod += amount/100 × s (same percent
  convention as AMP/FILTER). Comment header updated (FILTER/FRES).

### src/dsp/chain.c
- Reads xm->res_mod. res_mod != 0: effective res percent =
  clampd(p->res + res_mod×100, 0, 100), pos = (int)(res_eff/2) (same
  truncation as the static p->res/2), Q = 0.5×40^(pos/50), and the biquad
  coefficients recompute EVERY sample via biquad_set_coefs (both stages
  for 24 dB — history preserved, same contract as the ENV-cutoff path);
  the last_fc/mode/res/poles caches are kept coherent each sample so the
  static paths resume cleanly when the slot stops. res_mod == 0: the
  existing cache branches are UNTOUCHED (wrapped in else) — no behavior
  or cost change for static-res chains (R0d/PR5).

### src/preset/schema_tab.c / schema_parse.c / schema_write.c
- dest_tab: 8 entries off/pitch/phase/amp/flt_cut/flt_res/ring/fm; the
  id-4 string renames "filter" -> "flt_cut" (MOD_FILTER); "flt_res" maps
  to MOD_FRES. XB_DEST KT_ENUM count 7 -> 8 (schema_parse.c); the writer's
  enum_name(dest_tab, ...) count 7 -> 8 (schema_write.c) — REQUIRED: with
  MOD_FRES inserted, count 7 would drop "fm" and write "off" for MOD_FM.
- Amount bounds: checked xmod_elem's post-walk range logic
  (schema_parse.c xmod_elem) — only MOD_RING (0..100) and MOD_PITCH (±50)
  are special-cased, so FLT_CUT and FLT_RES fall in the default ±100
  branch as R0c/R0e require. RING 0..100 kept. No code change needed.
- schema.h comment: dest name range note updated to "OFF".."FLT_RES".

### Tests (call sites, not pins)
- tests/test_chain.c: the two xmod_in_t literals gained the 5th member 0.0f
  (xm0, xm_hi — the ask's 4 -> 5 update).
- tests/test_seq_items.c: three OLDER assertions pinned the pre-plan spec
  and now contradicted the new pins (they failed green-run): the MOD-tab
  label table (SRC/TGT/DEST/AMT/MIX -> SOURCE/OSC/DEST/DEPTH/LEVEL), the
  MASTER label table (MIX A/B/C -> OSC A/B/C), the TGT fmt check
  (ITF_INT -> ITF_TGT) and the dest ladder max (6 -> 7). Updated to the
  plan spec; comments cite R1/R2/R4/R0e.

### Presets (ask step 8)
- sed 's/"dest":"filter"/"dest":"flt_cut"/g' presets/*.json (the FILTER
  OBJECT KEY "filter":{...} is untouched — only the dest STRING).
- Canonicalizer, exactly as the ask gives it:
  gcc -Isrc -Ithird_party tools/migrate_presets.c src/preset/schema_parse.c
  src/preset/schema_tab.c src/preset/schema_write.c src/preset/preset.c
  src/dsp/*.c src/seq/*.c -lm -o /tmp/migrate && /tmp/migrate presets/*.json
  — ALL SIX presets/*.json report "migrated ... params verified" (bass-synth,
  drone-220, drone-ambient, drone-evolving, noise-drone, rhythmic-mech).
  git diff shows only the two files that carried the dest changed, one line
  each — the round-trip is byte-comparable apart from the rename.

### Verification run (this session)
- make clean && make test, then every build/tests/test_* executed:
  ALL GREEN — test_adsr 2427/2427, test_autoquit 12/12, test_chain 23/23,
  test_engine 12/12, test_engine_seq 2031/2031, test_filter 23/23,
  test_font 4465/4465, test_fx 25/25, test_json 560/560, test_keymap 46/46,
  test_keymap_events 36/36, test_keymap_events_r36s 31/31,
  test_keymap_r36s 15/15, test_mailbox 18/18, test_mixer 35/35,
  test_osc 32/32, test_presdirs 37/37, test_preset 179/179,
  test_range 80646/80646, test_sanity 1/1, test_seq_items 561/561,
  test_sequencer 221/221, test_theme 18/18, test_transport 35/35,
  test_ui_menu 147/147. (P1-P5 + PR1-PR3 green: harness counts above.)
- Desktop app build (`make`): ophis-drone links clean with the new
  ui_draw.c/seq_items.c — ui_draw.c is only compiled in the app build, so
  this was the check that the new cases compile.
- NOT run here (plan §2.2 step 3, VERIFY subagent): desktop smoke
  (XRUNS 0) and the commit.
