# PLAN_MOD_FENV_ADSR_DIV.md — FLT_ENV + ADSR_{A,D,S,R} mod dests; SEQ DIV display

Single file: design plan + implementation plan. Owner decisions (2026-11-10):
ADSR A/D/R mod = RATIO scaling; SEQ DIV display = "/n". No other feature work.

## System requirements (checked 2026-11-10 — all present)

gcc 15.2 (C23), pkg-config sdl2+alsa, aarch64-linux-gnu-gcc + arm64
SDL2/ALSA, zip, readelf. Nothing missing.

## Design plan

### Requirements

1. MOD matrix gains five dests, appended to `mod_dest_t` (house rule:
   ids 0..7 frozen, new ids appended — same as MOD_FRES):
   - `MOD_FENV = 8` ("flt_env") — sweeps the target chain's filter
     ENV AMT: `envamt_eff = clamp(envamt + amount/100 × s × 100, -100, +100)`
     (additive percent points, like RES; amount ±100).
   - `MOD_ADSR_A = 9`, `MOD_ADSR_D = 10`, `MOD_ADSR_S = 11`,
     `MOD_ADSR_R = 12` ("adsr_a/d/s/r") — modulate the target chain's ADSR:
     - A/D/R (ratio, owner decision): `t_eff = clamp(t × (1 + amount/100 × s), 0, 60)` s
       (amount +100 → ×2 max, −100 → 0 s).
     - S (additive points): `s_eff = clamp(s + amount/100 × s × 100, 0, 100)`.
     - amounts ±100; `s` is the normalized source (−1..1).
2. SEQ clock divider displays "/n", not "x n" (matches the sub-oscillator
   DIV display "/n").

### Facts (where things live today)

- `mod_dest_t`: `src/dsp/types.h:9-10`; dest strings: `src/preset/schema_tab.c`
  `dest_tab[]` (8 entries); parser count: `src/preset/schema_parse.c:440-441`
  (KT_ENUM, lo = 8); writer count: `src/preset/schema_write.c:254` (8);
  spec ladder max: `src/ui/seq_items.c` `[IT_XMOD_DEST] ... 0, 7` (precedent:
  "max 6 -> 7" when FLT_RES was appended).
- Routing: `mod_apply` (`src/dsp/mixer.c:29-65`) accumulates per-dest into
  `xmod_in_t` (`src/dsp/chain.h:9-26`); consumption in `chain_render`
  (`src/dsp/chain.c`): filter envamt at step 5 (line 101-103), ADSR at
  step 4 (line 94).
- AMT ranges/display are automatic for the new dests: `amt_min/amt_max`
  (`src/ui/ui.c:58-69`) already return ±100 for every non-PITCH/non-RING
  dest; `ITF_AMT` shows "%+.0f %%" for non-PITCH (`src/ui/ui_draw.c:108-111`);
  `ui_dest_display` uppercases with '_'→' ' ("flt_env" → "FLT ENV",
  "adsr_a" → "ADSR A") — all table-driven, zero UI code changes for labels.
- Schema amount validation (`schema_parse.c:696-699`): RING 0..100,
  PITCH ±50, else ±100 — new dests fall into the else branch automatically.
- SEQ DIV display: `src/ui/ui_draw.c:122` `case ITF_XDIV: "x%d"`.
- Preset format: no new keys — only new VALUES of the existing "dest" enum.

### Design decisions

- **Schema stays version 2** (append-only enum extension, house precedent:
  MOD_FRES appended without a version bump). Old presets parse unchanged;
  presets using the new strings are rejected by OLD binaries with the
  "xmod[i].dest" field-path error — acceptable, documented in MANUAL.
- **ADSR effective params built per sample** in `chain_render` as a local
  `adsr_params_t` (clamped doubles; S cast to int percent — 1-point sweep
  granularity, matching the knob). `adsr_next` signature unchanged.
  Edge: A modulated to 0 mid-ATTACK — `stage_n` floor of 1 sample snaps
  the stage closed next sample (existing behavior, no new code).
- **env_mod consumed at the existing fc formula** — the fc-only coefficient
  cache path already handles per-sample cutoff changes (S1/S2 fixes);
  no new cache invalidation needed.
- `xmod_in_t` grows by `double env_mod; double adsr_mod[4];` (A,D,S,R
  index order) — DSP-side struct only; `synth_params_t`, mailbox, preset
  arena counts unchanged.
- XDIV edit behavior (wrap 1..16) unchanged — display only.

### UI/UX description

- MOD page DEST ladder gains: `OFF · PITCH · PHASE · AMP · FLT CUT ·
  FLT RES · FLT ENV · RING · FM · ADSR A · ADSR D · ADSR S · ADSR R`
  (table order = dest_tab order; wraps with LEFT-VAL/RIGHT-VAL as before).
- DEPTH on the new dests: "±n %" (same as AMP/FLT CUT/FLT RES); dimmed
  while DEST = OFF (unchanged); re-clamped to ±100 on dest switch
  (automatic via `amt_clamp`).
- SEQ page DIV row shows "/1".."/16"; sub-oscillator DIV unchanged ("/n") —
  the two divider rows now read the same way.

## Implementation plan

Method per milestone: TDD — failing tests first (all RED), implement,
all GREEN, full `make test`, commit. Sequential subagent per milestone;
subagent appends changes + rationale to `LOG_MOD_FENV_ADSR_DIV.md`.
Commits imperative, unsigned.

### MS1 — DSP: enum, routing, consumption

- Tests first (RED):
  - `tests/test_mixer.c`: slot {src=b_osc, dest=MOD_FENV, amount=50} →
    `xm[t].env_mod == source×0.5` for each of the four adsr_mod slots
    (amount ±100 both signs); MOD_OFF/PITCH/RING regressions untouched.
  - `tests/test_chain.c`:
    - env_mod: envamt 0 + env_mod +0.5 (i.e. +50 points) shifts fc exactly
      as envamt 50 does without mod (same coefficient path, compare
      outputs); clamp at ±100.
    - a_mod +1.0 (amount 100) → attack reaches level 1.0 in ~2× the
      samples of the unmodulated case; a_mod −1.0 → 1-sample attack.
    - d_mod, r_mod: analogous duration scaling; s_mod +1.0 on s=50 →
      sustain level 1.0; s_mod clamps at 0/100.
    - Regression: all mod fields 0 → output bit-identical to pre-change
      (pin with the existing golden cases).
- Implement: `types.h` enum append + doc comment; `xmod_in_t` fields;
  `mod_apply` five new cases; `chain_render` envamt clamp + per-sample
  effective ADSR struct.
- Verify: new tests GREEN; `make test` GREEN (test_engine_seq pins
  sequencer behavior; test_fx untouched).

### MS2 — schema + UI: strings, counts, ladder, DIV display

- Tests first (RED):
  - `tests/test_json.c`: preset with all five new dest strings round-trips
    byte-comparable; unknown "adsr_x" → reject naming "xmod[i].dest";
    every file in `presets/` still parses; amount 101 on a new dest →
    reject; amount −1 on adsr_s → reject (±100 range).
  - `tests/test_seq_items.c`: `[IT_XMOD_DEST]` spec max == 12.
  - `tests/test_ui_menu.c`: `item_value` of the SEQ DIV item == "/4" at
    div 4; DEST row value for a slot with dest=MOD_FENV reads "FLT ENV".
- Implement: `schema_tab.c` dest_tab append ("flt_env", "adsr_a",
  "adsr_d", "adsr_s", "adsr_r"); parser KT_ENUM lo 8 → 13; writer
  `enum_name(dest_tab, 13, ...)`; `seq_items.c` IT_XMOD_DEST max 7 → 12
  (comment: "max 7 -> 12, FLT_ENV + ADSR ladder"); `ui_draw.c` ITF_XDIV
  "x%d" → "/%d".
- Verify: new tests GREEN; `make test` GREEN.

### MS3 — docs + full verification

- Update `MANUAL.md` MOD dest ladder (line ~53) and any DIV "×n" display
  wording; `docs/manual.html` updated if it mirrors MANUAL.
- Verify: `make clean && make && make test && make handheld rg28xx` all
  green (handheld gates PASS — no new glibc symbols expected: plain math
  + tables only); shipped presets round-trip (`test_json` covers).

### Commit sequence

1. `Add FLT_ENV and ADSR A/D/S/R modulation destinations to the DSP chain`
2. `Expose the new mod dests in presets and the UI; show SEQ DIV as /n`
3. `Document the FLT_ENV and ADSR mod destinations`

## Verification summary

- Per milestone: RED → GREEN → full `make test`.
- Math verification (rule): MS1 tests pin the exact scaling identities
  (×2 / ÷2 attack durations, ±100-point sweeps, clamps) against the
  formulas above.
- Final: clean build + tests + both handheld gates green; three commits;
  `LOG_MOD_FENV_ADSR_DIV.md` complete.
