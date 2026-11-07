# LOG_MOD_REVAMP.md — implementation log.
# Plan: PLAN_MOD_REVAMP_SEQ_MARKER_RETRIG.md (item 2).
# Owner decisions 2026-11-06: ring mod BOTH (matrix A.RING + per-sub
# RING), FM = true operator FM (audio-rate source into carrier phase,
# DX-style index), chaining = Sub → osc → next chain (M3b, pending).

## M3a — RING + FM destinations

- types.h: mod_dest_t gains MOD_RING (carrier × source; AMT 0..100
  dry/wet, negative AMT inverts the modulator polarity) and MOD_FM
  (audio-rate source drives the carrier phase with DX-style index
  AMT/25 cycles — ±4 cycles at AMT 100; deeper than the ±pi PHASE dest).
- schema.c: dest_tab gains "ring"/"fm" (val_enum 5→7 at both parse
  sites, enum_name at both writer sites — display names and JSON
  round-trip inherit the new values); amount bounds per dest
  (sub_elem: RING 0..100, PITCH ±50, others ±100; xmod_elem: RING
  0..100, PITCH ±50, others ±100).
- seq_items.c: DEST ladder max 4→6 for IT_SUB_DEST/IT_XMOD_DEST
  (test_seq_items spec pins updated).
- ui.c: amt_min/max — RING clamps 0..100 (dry/wet mix, no negative
  half); the re-clamp-on-dest-switch path inherits the bounds.
- DSP: chain.c — sub MOD_FM accumulates into the phase channel
  (amount/25 × m, same telescoping PM mechanism); sub MOD_RING applies
  in step 2 per sub in slot order: x × ((1−w) + w×(m×polarity)) using
  raw_sub_out[i] (already computed — a second subosc_out call would
  double-advance the modulator phase). mixer.c — xmod MOD_RING slots
  are collected per call (locals, no mixer state) and applied to A's
  output in slot order before the tanh sum; xmod MOD_FM shares the
  phase channel with PHASE (both accumulate).
- Pins: test_chain — RING 100: sidebands 170/230 over carrier (2×);
  RING 0: dry (200 Hz); FM ratio 1 index 4: sideband at 600 Hz.
  test_mixer — xmod RING 100: out = tanh(1.5×(0.5+2)) exact; RING 0:
  tanh(4.5) exact; FM: phase_mod = amount/25 × src. test_json —
  ring/fm parse + roundtrip identity; RING −1 and FM 101 reject;
  xmod ring parses. test_seq_items — ladder spec pins 4→6.
- Verification: suite 99606/99606. Docs: manual DEST/AMT rows, PLAN §5.
- M3b (chaining) follows.

## M3b — serial chain FM (sub → osc → next chain)

- chain_params_t gains `chain_fm` (percent 0..100, default 0 = off);
  chain_render gains `fm_in`/`fm_amt` — the FEEDING chain's osc output
  (post-sub-modulation) drives this carrier's phase with operator index
  chain_fm/25 cycles (same convention as the MOD_FM dest), through the
  existing telescoping PM mechanism.
- engine.c: chains render in order 0,1,2 per sample — B is fed A's and
  C is fed B's SAME-frame osc output; the wrap (A fed by C) uses C's
  previous frame (one-frame delay, standard for feedback loops);
  chfm_prev[3] state, zeroed on init and PANIC. All chain_render test
  callers updated (+2 args).
- schema: per-chain OPTIONAL "chfm" (0..100) — j.p is memset before the
  walk, so old presets parse with chfm 0; out-of-range rejects. Writer
  emits it always.
- UI: OSC MAIN section gains "CHFM" (6th item; sections 6/6/6,
  OSC_ITEMS 18) — IT_CHFM, ITT_CHAIN, ITF_PCT 0..100.
- Pins: direct chain_render spectral pin — B (200 Hz sine) fed a 15 Hz
  full-scale sine at index 4: 215 Hz sideband 5.6× the dry case (a ±1
  SQUARE feed was a degenerate first choice: at index 4 it shifts B by
  WHOLE cycles — spectrum unchanged); engine wiring pin — identical
  runs with chfm 100 vs 0 leave different last raw samples in B (A's
  boundary sample −0.229 → B −0.964 vs +0.988). test_seq_items MAIN
  table/section pins updated (6/6/6, OSC_ITEMS 18).
- Verification: suite 99616/99616; desktop relink + capped smoke XRUNS 0;
  handheld gate OK; zip rebuilt.

## M3c — MIX dest: the "usual" mixing (owner follow-up: "you never did
## anything about the mixing")

- Owner follow-up 2026-11-06: the mixing-type control was missing. The
  slot's DEST is now the mixing-type selector: MIX (the usual mixing —
  the source's AUDIO blended into A: A' = (1−w)×A + w×src, AMT 0..100
  wet) / RING (multiplicative) / FM (operator chaining) — plus the
  control-rate dests (PITCH/PHASE/AMP/FILTER) unchanged.
- types.h MOD_MIX; schema dest_tab "mix" (count 8: two parse sites, two
  writer sites), amount bounds 0..100 like RING (sub_elem + xmod_elem);
  seq_items DEST ladders max 7 (spec pins updated); ui.c amt bounds
  RING|MIX → 0..100.
- mixer.c: RING/MIX slots collected per call (w, src, mode) and applied
  to A's output in slot order — MIX crossfades, RING multiplies.
- Pins: test_mixer — MIX 100 (A'=0.5 exact), MIX 0 (dry exact), MIX 50
  (exact blend); test_json — mix parses, negative rejects. Docs:
  manual DEST row, PLAN §5.
- Verification: suite 99626/99626.

## M3d — separate the mixer: independent per-slot MIX knob

- Owner follow-up: "make the mixer separate so I can both add the osc
  to the src and modulate (or any of the 2 alone)." The slot is now
  {src, dest, amount, mix} — `mix` (0..100) is the ADD function
  (source audio blended into A), INDEPENDENT of the dest: a slot can
  add AND modulate, or either alone; dest OFF + mix > 0 is a plain
  audio mixer.
- mixer.c: the mix application moved BEFORE the dest-continue check
  (it works with dest OFF — the first placement after the switch was
  skipped whenever dest was OFF, caught by the exact-value pins);
  ring/mix slots apply in slot order, each slot's modulation before
  its own mix.
- Compat: the stage-3 `dest: "mix"` parses into the mix knob with
  dest OFF; the editing ladder no longer offers MOD_MIX (max 6); the
  writer emits the `mix` field. Schema mix bounds 0..100.
- UI: MOD page per slot SRC · DEST · AMT · MIX (MOD_ITEMS 12, sections
  4/4/4); IT_XMOD_MIX (ITT_XMOD, ITF_PCT 0..100); MIX never dims (it
  works with dest OFF).
- Pins: test_mixer — knob alone (100/0/50 exact), RING+MIX together
  (ring then blend, exact); test_json — mix parses, out-of-range
  rejects, stage-3 dest compat documented; test_seq_items MOD table
  (12 items, sections 4/4/4).
- Verification: suite 99640/99640; ASan+UBSan test_mixer clean;
  desktop relink + capped smoke XRUNS 0; handheld gate OK; zip rebuilt.

## M1 — matrix TARGET routing (PLAN_MOD_ROUTING.md)

- Owner order 2026-11-06: each matrix slot routes to its own TARGET
  chain (A/B/C) instead of hardcoded A. Slot = SRC · TGT · DEST ·
  AMT · MIX; the ADD (mix) knob follows the TARGET too.
- types.h: xmod_params_t gains `tgt` (0..2, 0 = A).
- schema.c: xmod slot gains optional "tgt" (0..2, default 0 via the
  memset; out-of-range rejects, reject-style); writer emits it always.
- mixer.c: xm accumulation keyed by t = clampi(x->tgt,0,2); RING/MIX
  slots collected PER TARGET (ring_w/s/mode become [3][3]) and applied
  to the target's output in slot order; master sum unchanged.
- engine.c: chain c receives &mx.xm[c] (was: xm[0] for A, ZERO_XM for
  B/C); ZERO_XM removed. xm[] is computed per sample from the previous
  sample's sources — the existing one-sample cross-chain delay,
  identical for every target.
- UI: MOD page per slot SRC · TGT · DEST · AMT · MIX (MOD_ITEMS 15,
  sections 5/5/5); IT_XMOD_TGT (ITT_XMOD, ITF_INT 0..2, wraps); never
  dims (routing is meaningful with dest OFF via the ADD knob). PLAN
  §6 said 4 rows/slot; §4b keeps the mix KNOB in the UI (owner ruling
  "its knob is UI") — 5 rows resolved the conflict; flagged to owner.
- Pins: test_mixer — PITCH tgt=1 lands in xm[1] (xm[0/2] zero), RING
  tgt=2 exact tanh(1.5×2.5), ADD-knob tgt=1 exact blend; amp/filter/
  detune tests reset tgt (state-leak fix). test_json — absent tgt → 0
  (P5), tgt 2 parses + roundtrip, tgt 3 / −1 reject (P7). test_seq_items
  — table 15/5-5-5, TGT spec pin, wrap list 11. test_engine_seq — P2
  end-to-end: B.osc FM→C gives 310 Hz sideband 0.051 vs 0.0011 (45×)
  when targeted at C; targeting A (level 0) leaves C clean.
- Verification: suite 99657/99657 (forced rebuild; an earlier stale
  binary masked the P2 NaN from an out-of-bounds sink copy — fixed
  with a per-block copy).

## M2 — SRC extension to A sources + self-routing (PLAN_MOD_ROUTING.md)

- Owner order 2026-11-06 (D3 = yes): any osc can drive any slot;
  self-routing (SRC chain == TARGET chain) is legal — behaves like the
  chain's own sub modulation, one sample late.
- Compat decision: the A sources are APPENDED as ids 7..9
  (a_osc/a_sub1/a_sub2) — ids 1..6 keep their B/C meaning, so old
  presets are untouched (a renumber-to-A-first would have broken them;
  plan §5 requires defaults = today).
- mixer.h: mixer_state.src 6 -> 9 entries. engine.c: src[6..8] =
  ch[0].raw_osc_out / raw_sub_out[0..1] — same raw pre-everything taps
  as B/C, consumed one sample late.
- schema.c: src_tab 7 -> 10 entries; both count sites (writer
  enum_name, parser val_enum) 7 -> 10; schema.h name-range comment.
- mixer.c: src bounds 1..6 -> 1..9 at both the ADD-knob and the
  modulation paths.
- UI: IT_XMOD_SRC ladder max 6 -> 9 (wraps; display names come from
  schema_xsrc_name -> OFF/B.OSC/.../A.OSC/A.SUB1/A.SUB2).
- Pins: test_json — a_osc -> 7, a_sub2 -> 9, unknown name rejects;
  test_seq_items — XSRC spec max 9; test_mixer — src 7 reads src[6]
  (A.osc 0.25 × amt 4 -> detune 1.0); test_engine_seq — P3: A.osc RING
  into B gives the 310 Hz (200+110) sideband at 0.196 vs 0.0013
  (155×) with the slot off.
- Verification: suite all green (forced rebuild).

## M3 — MIX VOLUME (PLAN_MOD_ROUTING.md)

- Owner order 2026-11-06: per-chain master send on the MASTER page —
  scales the chain's post-ADSR/FILTER/LEVEL signal into the master
  sum; NEVER touches modulation. Master sum =
  tanh(1.5 × Σ (mixvol[c]/100 × chain_out[c])), applied AFTER the
  slot RING/MIX so a slot's effect on its target scales with the
  target's send.
- types.h: chain_params_t.mixvol (0..100, default 100). mixer.h:
  mixer_params_t.mixvol[3]; engine copies it per block. mixer.c: the
  sum scales each target's post-RING/MIX output. Raw mod sources are
  untouched — mixvol 0 silences at master but the chain still drives
  mod slots (P4's contract).
- schema.c: optional "mixvol" (0..100) per chain, seen-bit 256,
  post-walk default 100 (j.p memset would otherwise make absent =
  SILENT); writer emits it always.
- defaults.c: all three chains mixvol 100.
- UI: MASTER page gains the MIX section — MIX A / MIX B / MIX C
  (MASTER_ITEMS 14, sections MASTER/MIX/DELAY/REVERB = 3/3/4/4);
  IT_MIXVOL with a new ITT_MIXVOL si-target (si IS the chain index —
  the MASTER page edits all chains regardless of the OSC tab).
- Writer arena: W_STR_N 224 -> 256 (190 key strings + 37 value strings
  = 227 overflowed at 224 — found via an instrumented copy; test_json
  segfaulted on a truncated doc, strstr(seq_at=NULL) at the "no bpm in
  seq" pin), W_NUM_N 128 -> 160, W_VAL_N 416 -> 448, W_EL_N 192 -> 224.
- Pins: test_mixer — mixvol B=0 exact tanh(1.5×2), A=50 exact
  tanh(1.5×1.5); test_json — absent mixvol -> 100 (P5), 0 parses,
  101/-1 reject (P7); test_seq_items — MASTER 14/4 sections, MIXVOL
  spec, last-section pin made index-based; test_engine_seq — P4:
  mixvol B=0 leaves g110 = 0.002 (B silent) while B.osc still drives
  A's vibrato (g310 0.146 vs 0.002, 73×).
- Verification: suite all green (forced rebuild); ASan+UBSan clean on
  test_json/test_mixer/test_seq_items/test_engine_seq/test_preset.

## M4 — dest "mix" gone (new format only) + CHFM removal (PLAN_MOD_ROUTING.md)

- Owner orders 2026-11-06: "remove CHFM" (supersedes the keep-ruling);
  dest "mix" leaves the format, on-disk presets rewritten at dev time
  (M4b) so nothing is rejected; the parser knows the NEW format only —
  NO compat mapping.
- CHFM: chain_params_t loses chain_fm; chain_render loses the
  fm_in/fm_amt arguments; engine.c loses the fm_in wiring and
  chfm_prev[3]; schema loses the "chfm" writer ADD and parser case (a
  preset carrying "chfm" now falls to the generic unknown-key skip —
  P6 pin); UI loses the CHFM row (MAIN back to 5 items, sections
  5/6/6, OSC page 17 items); IT_CHFM deleted.
- dest "mix": MOD_MIX deleted from mod_dest_t; dest_tab 8 -> 7 (two
  parse sites, two writer sites); the stage-3 compat mapping
  (dest "mix" -> mix knob + OFF) DELETED from the xmod parser; amount
  range checks RING|MIX -> RING (sub + xmod); mixer.c loses the
  MOD_MIX case; DEST ladders max 7 -> 6; ui.c amt bounds RING only.
  The per-slot `mix` PARAMETER (the ADD knob, IT_XMOD_MIX) STAYS —
  it is a parameter, its knob is UI.
- P1 (signal-flow order env -> filter): already pinned by
  test_chain "ENV AMT drives cutoff end-to-end" (fc = 500×(1+env))
  and the filter_mod × envamt spectral-equivalence pin — cited, no
  new code; the order cannot move without those failing.
- Pins: test_json — dest "mix" REJECTS (sub site; xmod parse site
  flipped too), P6 chfm-skip pin; test_seq_items — MAIN 5 no CHFM,
  OSC sections 5/6/6, dest ladder max 6; chain_render callers in
  test_chain/test_mod/test_mixer updated (-2 args); test_engine_seq —
  the M3b serial-FM block REMOVED (feature gone).
- Verification: suite all green (forced rebuild); ASan+UBSan clean on
  test_json/test_chain/test_engine_seq/test_seq_items/test_preset;
  desktop binary links.

## M4b — dev-time preset rewrite (PLAN_MOD_ROUTING.md)

- Owner orders 2026-11-06: "existing presets should be rewritten in
  new format", "all old presets incl. dest mix", "rewrite now during
  development".
- tools/migrate_presets.c (dev tool, NOT shipped): parse with the
  CURRENT (new-format-only) parser -> write -> re-parse -> memcmp the
  two param sets -> rewrite the file in place. A file the new parser
  rejects (chfm / dest "mix" carriers) FAILS LOUDLY instead of being
  silently migrated — none did: no on-disk preset carried chfm or
  dest "mix".
- All 6 repo presets (drone-220, preset-1..5) rewritten: every xmod
  slot now carries "tgt": 0, every chain "mixvol": 100; writer emits
  the canonical compact form. Verified a second time independently
  (python json: tgt present in all slots, mixvol in all chains, no
  chfm, no dest "mix"). Pre-migration copies untouched as *.orig
  (existing backup convention) plus /tmp/presets_backup_m4b.
- Parameter deltas: NONE beyond the documented defaults (tgt 0 = A,
  mixvol 100 = today's level) — memcmp against the pre-rewrite parse
  is exact because both defaults are identity values.

## M5 — docs + full verification (PLAN_MOD_ROUTING.md)

- docs/manual.html: overview — slots modulate ANY chain (TARGET),
  MIX VOLUME noted; SUB table MIX/CHFM rows removed; MOD tab rewritten
  (SRC 0..9 incl. A, TGT, DEST off..fm, AMT per-type units, MIX = ADD
  knob, one-sample source delay, modulation at the target's OSC
  stage, mixvol master-sum formula); MASTER path text — chains scaled
  by MIX VOLUME; MASTER items table gains MIX A/B/C.
- PLAN.md: matrix slot spec = {source 0..9, target, dest off..fm,
  amount, mix}; CHFM-removal note; mixer formula
  tanh(1.5 × Σ (mixvol[c]/100 × out[c])) at both sites.
- Verification: suite all green (forced rebuild); desktop relink OK;
  capped desktop smoke XRUNS 0; handheld build + GLIBC gate PASS;
  zip rebuilt — the zipped presets verified as new format (tgt in
  every slot, mixvol in every chain, no chfm, no dest "mix").
