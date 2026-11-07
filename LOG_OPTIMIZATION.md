# LOG_OPTIMIZATION.md — implementation log.
# Plan: PLAN_OPTIMIZATION.md (owner order 2026-11-07; DP1 = integer
# dividers 1..16, DP2 = sub DEST/AMT deleted).

## O1 — mechanical de-dup: A3 (SB bits), S4 (const params), D2 (src table)

- A3: schema.c seen-bits are INDEX-derived now — req_t lost its bit
  field; missing_keys walks SB(i) by index; every table has a parallel
  enum (RB_/CB_/SB2_/AB_/FB_/QB_/TB_/XB_/DB_/VB_); handlers set
  SB(<CONST>). The optional mixvol is CB_MIXVOL (bit 8, beyond the 8
  required keys); chain_elem passes CBCOUNT-1 as the required count.
  No hand-numbered bits remain (grep-verified).
- S4: mixer_params_t = { const synth_params_t *synth } — the mixer
  reads sp->xmod / sp->chain[t].mixvol directly; engine passes &e->p;
  the per-block memcpy/mixvol copy loop is GONE (sync-drift class
  eliminated). test_mixer/test_chain construct synth_params_t + point
  at it.
- D2: modsrc_t {chain, sub} table (mixer.h/c) is the ONE source of
  truth for ids 1..9 (frozen: 1..6 = B/C, 7..9 = A); engine walks it;
  test_json pins every id against a frozen table AND against schema's
  src names (parse -> id -> modsrc cross-check), 63 new checks.
- Verification: suite all green (forced rebuild; test_json 548,
  test_mixer 29, test_chain 27, test_engine_seq 2030).

## O2 — D1 sub redesign: Spice/Subharmonicon voices (owner spec, DP1 =
## integer dividers 1..16, DP2 = sub DEST/AMT deleted)

- Research (§0 of the plan): Subharmonicon subs = subharmonic
  oscillators, parent f divided by a selectable integer, sound sources
  with level controls; Behringer Spice = 2 subs per VCO with own freq
  + level in the mixer. Owner spec: 3 oscs, 2 subs each, ratio-only
  (dividers), sin/tri/saw/square, per-sub LEVEL.
- types.h: subosc_params_t = { div 1..16, wave, duty, level }; HZ mode
  and freq_mode_t DELETED; dest/amount gone from subs. Subs are
  AUDIBLE voices: level 0..100 mixes them into the chain's OSC SUM
  (pre-mod/ADSR/filter — Spice mixer arrangement). raw_osc_out stays
  the MAIN osc only; raw_sub_out stays raw pre-level (matrix taps are
  level-independent, SP3).
- chain.c: the sub dest switch and the RING loop DELETED (the six-dest
  mapping now lives ONLY in the matrix — review-D1 duplication
  dissolved at the root); sub_sum = Σ level/100 × raw.
- subosc.c: f = parent_f / div (clamped 1..16 defensively).
- schema: sub = { div, wave, duty, level } ALL required; fm_tab
  deleted; div 0/17 and level out-of-range reject; old sub keys are
  unknown (lenient skip). Writer emits the new set.
- UI: SUB sections = DIV · WAVE · DUTY · LEVEL (8 items, sections
  5/4/4, OSC page 13); IT_SUB_DIV (ITF_DIV 1..16 wrap, "/n" display),
  IT_SUB_LEVEL (ITF_PCT); IT_SUB_FMODE/FREQ/DEST/AMT and ITF_FMODE/
  ITF_SUBFREQ deleted; the ratio ladder (pos_to_ratio/ratio_to_pos)
  deleted from range.c — no consumer.
- Presets: JSON transform (subs -> div/wave/duty/level, level 0;
  HZ-mode sub freqs dropped — no parent-relative equivalent) then the
  canonical rewrite tool (parse-write-reparse-memcmp verified, all 6).
  Sub DEST/AMT found in preset-1/4/5 (6 slots) migrated to matrix
  slots {src a_sub1/a_sub2, tgt 0, same dest/amount} — the first
  transform pass had a WRONG chain->src order (B/C/A) and was redone
  from the backup with the correct A/B/C mapping. Zero noise subs.
- tests: test_chain — SP1 (unison level-100 sum ~2x RMS; level-0 wild
  sub byte-identical to bare chain) + SP2 (div 2 -> 100 Hz, div 4 ->
  50 Hz on a 200 Hz saw; sub owns the probed bins); test_engine_seq —
  SP3 (inaudible B sub0 still rings A at full depth via SRC B.SUB1,
  g310 0.201 vs 0.001); test_json — SP4 (new key set, old keys skip,
  div/level bounds, missing keys; xmod-site dest bounds moved off the
  dead sub site); test_seq_items — SP5; test_mod DELETED (every pin
  modulated via sub dests — capability gone; its matrix-side coverage
  lives in test_mixer/test_chain/test_engine_seq; O3 repurposes the
  slot for the mod_apply equivalence pin); test_preset fills distinct
  divs/levels; test_range ratio pins removed.
- Verification: suite all green (forced rebuild); ASan+UBSan clean on
  test_json/test_chain/test_seq_items/test_engine_seq/test_range/
  test_preset; desktop smoke XRUNS 0.

## O3 — S2 matrix/master split + A2 single mod_apply site

- mixer.h/.c: matrix_render (xm[] + RING/MIX slot functions -> post[3])
  and master_sum (pure tanh of the mixvol-scaled sum) are separate
  public functions; mixer_render is their builder composition (same
  external contract, all existing pins untouched).
- A2: mod_apply (static, mixer.c) is THE single site of the dest
  mapping — PITCH amount×s, PHASE/AMP/FILTER amount/100×s, FM
  amount/25×s. RING/MIX deliberately live beside the slot loop (they
  act on the target's OUTPUT, not the accumulator). The per-dest exact
  pins in test_mixer ARE mod_apply's spec; noted: with subs no longer
  modulating (O2), the matrix is the only consumer, so a separate
  dsp/mod.c TU would be over-modularization — the static function is
  the abstraction.
- Pins: test_mixer — matrix_render exact post outputs (C ringed to
  0.5, A/B untouched), master_sum exact tanh(0.75) with only C's send
  open, mixer_render == master_sum(matrix_render) composition equality
  (caught a real pin bug: the fresh state needs the same source
  samples). test_engine_seq/test_chain green unchanged.
- Verification: suite all green (forced rebuild).

## O4 — D3 item-kind descriptors + D6 ladder consolidation + A1 pages

- D3: item_spec_t gains param_kind_t (PK_*, one per stored member);
  param_get/param_set (ui.c, exposed via ui_internal.h) are ONE switch
  pair on the kind — the ~180-line mirrored item_get/item_set ladders
  collapsed to two ~10-line item-level wrappers (slot+kind mapping,
  ITT_UI special case). item_dim keeps its 4 rule entries.
  Pin: test_ui_menu round-trips 41 kind/slot/value cases (93 -> 134).
- D6: adsr_pos_to_s/adsr_s_to_pos moved to ui/range.c (public, with
  the hz ladder); ui.c's static copies deleted.
- A1: page_t {spans, sections} + page_get(tab, sub) in seq_items.c —
  the ONE layout source; ui.c content_count/content_item are span
  walks. Pin: coverage over every tab+sub (counts 13/9/7, MOD 15,
  MASTER 14, section counts, out-of-range NULLs).
- Verification: suite all green (forced rebuild); ASan+UBSan clean on
  test_seq_items/test_ui_menu/test_range/test_json.

## O5 — D4 table-driven keys + M2 schema split

- D4: EVERY regular scalar preset key now lives in a key_spec_t table
  ({key, kind, offset, range/enum-tab, custom}) consumed by ONE
  generic walker (walk_keys) — kinds: PERCENT / INT / ENUM / DUTY /
  BOOL / FNUM (float + range) / CUSTOM (irregular keys: app/version
  checks, arrays/objects, the post-walk xmod amount). The req_t tables
  are GONE — missing_keys walks the key tables themselves (the seen
  bit stays the index, A3); the *_key functions collapsed to one
  walk_keys call + a small residual handler each. The 548-check
  test_json suite passed the rewrite UNMODIFIED (bounds made double so
  the 0.1 Hz floors survive).
- M2: schema.c (1152 lines) split into schema_tab.c (enum tables +
  name accessors, 95), schema_write.c (arena writer, 286),
  schema_parse.c (walker + tables + parse entry, 751), shared
  declarations in schema_priv.h (20). PRESET_SRC is a wildcard — no
  Makefile change.
- Verification: suite all green (forced rebuild); ASan+UBSan clean on
  test_json/test_preset/test_presdirs.

## O6 — M1 valrepeat TU + S1 display separation + S3 input-policy gate

- M1: the value-key edit + synthesized hold-repeat state machine moved
  to ui/valrepeat.c (val_edit + valrepeat_tick, no state of its own —
  the hold lives in ui_t); ui.c routes VAL actions and the tick into
  it; edit_item became the cross-TU interface. No behavior change (the
  valkey hold/release tests pass untouched).
- S1 (strict separation): schema_*_name return the RAW preset strings
  ("off".."a_sub2") — ent_display DELETED from the preset layer; the
  display transform (UPPERCASE, '_' -> '.') is ui_enum_display in
  ui/seq_items.c, used by ui_draw's five enum cases.
  Pin: the transformed forms ("B.SUB1", "A.OSC", "SAW", "PINGPONG",
  "12DB") and the raw layer ("saw", "a_osc", "ring").
- S3: the U2 text-entry input-policy gate moved out of main.c's event
  loop into ui_event_filter (ui.c, declared in ui.h) — the UI owns its
  input policy; main.c polls -> filter -> keymap -> handle.
  Pin: KEYDOWN swallowed during MENU_NAME except ESC/ENTER, KEYUP and
  no-entry cases pass.
- Makefile: valrepeat.c added to UI_SRC and the test_ui_menu rule; the
  stale src/preset/schema.c prereqs replaced with the three split TUs
  (the stale prereq had silently frozen the test binary — found by the
  pin count).
- Verification: suite all green (forced rebuild; test_ui_menu 147);
  desktop smoke XRUNS 0; ASan+UBSan clean on
  test_ui_menu/test_seq_items/test_json.

## O7 — D5 render helper + docs + full verification

- D5: tests/test_render_helper.h — rh_render_mono (engine -> contiguous
  mono buffer, the per-block copy done ONCE correctly); the four
  per-pin render loops in test_engine_seq (P2/P3/P4/SP3) collapsed
  onto it; their static sink buffers deleted.
- Docs: manual SUB sections = DIV/WAVE/DUTY/LEVEL (13-item OSC page),
  subs are Spice/Subharmonicon voices summing into the OSC SUM before
  ADSR/filter with no dest knobs; preset-format list updated (sub
  div/wave/duty/level; xmod src 0..9 + tgt; dest "mix" gone; chain
  mixvol). PLAN.md sub paragraph rewritten; IMPLEMENTATION.md —
  subosc_params_t, subosc_out contract, mixer matrix_render/
  master_sum contract, schema TU split, defaults note.
- Verification: suite all green (forced rebuild); ASan+UBSan clean on
  test_json/test_engine_seq/test_chain/test_ui_menu/test_seq_items/
  test_range/test_preset/test_mixer; valgrind clean on
  test_engine_seq/test_mixer; desktop smoke XRUNS 0; handheld GLIBC
  gate PASS; zip rebuilt with the new-format presets verified inside.
