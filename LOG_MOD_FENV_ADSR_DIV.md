# LOG_MOD_FENV_ADSR_DIV.md

## 2026-11-10 — MS1 (DSP: enum, routing, consumption)

### What changed
- `src/dsp/types.h`: `mod_dest_t` appended with `MOD_FENV`, `MOD_ADSR_A`,
  `MOD_ADSR_D`, `MOD_ADSR_S`, `MOD_ADSR_R` (ids 8..12; 0..7 frozen) +
  doc comment.
- `src/dsp/chain.h`: `xmod_in_t` grew `double env_mod;` and
  `double adsr_mod[4];` (A,D,S,R index order) + doc comments.
- `src/dsp/mixer.c`: `mod_apply` gained five cases — FLT_ENV accumulates
  `amount/100 × s` into `env_mod`; ADSR A/D/S/R accumulate into
  `adsr_mod[dest - MOD_ADSR_A]`.
- `src/dsp/chain.c`: `chain_render` builds a per-sample effective
  `adsr_params_t` (A/D/R ratio-scaled seconds clamped 0..60 s, S
  additive points clamped 0..100 int percent) and an effective ENV AMT
  (`envamt + 100×env_mod`, clamped ±100) in the fc formula.
- `tests/test_mixer.c`: FLT_ENV pin (amount ±50 → ±0.25), four ADSR
  pins (amount ±100 → ±0.5, siblings untouched), OFF/PITCH regression.
- `tests/test_chain.c`: env_mod identity (envamt 0 + mod 0.5 ≡ envamt
  50; clamps at ±100), a/d/r ×2 duration scaling and a −1.0 → 1-sample
  attack, s_mod clamps at 0/100; two positional `xmod_in_t`
  initializers converted (missing-field-initializers is -Werror).

### Why
PLAN_MOD_FENV_ADSR_DIV.md MS1: DSP-side enum, routing and consumption
of the five new modulation destinations. Schema/UI deliberately
untouched (MS2).

### Test results
- RED: compile red — `MOD_FENV`/`env_mod`/`adsr_mod` did not exist
  (same precedent as the MOD_FRES pin).
- GREEN: test_mixer 77/77, test_chain 35/35, full `make test`
  TOTAL 11390/11390 passed.

## 2026-11-10 — MS2 (schema + UI: dest strings, enum counts, ladder max, SEQ DIV display)

### What changed
- `src/preset/schema_tab.c`: `dest_tab` grows 8 -> 13 entries — "flt_env"
  (MOD_FENV), "adsr_a"/"adsr_d"/"adsr_s"/"adsr_r" (MOD_ADSR_A..R)
  appended after the frozen ids 0..7 (same precedent as MOD_FRES).
- `src/preset/schema_parse.c`: XMOD "dest" KT_ENUM count 8 -> 13
  (unknown dest strings, e.g. "adsr_x", reject naming "xmod[i].dest").
- `src/preset/schema_write.c`: writer `enum_name(dest_tab, 13, ...)`.
- `src/ui/seq_items.c`: `IT_XMOD_DEST` spec max 7 -> 12 (comment: FLT_ENV
  + ADSR ladder appended). `src/ui/seq_items.h`: ITF_XDIV doc comment
  "x%d" -> "/%d".
- `src/ui/ui_draw.c`: `ITF_XDIV` display "x%d" -> "/%d" — SEQ DIV now
  reads "/1".."/16", matching the sub-oscillator DIV display. Display
  only; wrap 1..16 edit behavior unchanged.
- Amount bounds need no code: new dests fall into `xmod_elem`'s
  non-RING/non-PITCH else branch (±100); labels/DEPTH display are
  table-driven via `schema_dest_name`/`ui_dest_display`.
- Tests: `tests/test_json.c` §12 (five new dest strings write + parse +
  byte-comparable rewrite; "adsr_x" rejects naming "xmod[0].dest";
  flt_env amount 101 rejects; adsr_s amount -1 parses / -101 rejects;
  every shipped `presets/*.json` still parses); `tests/test_seq_items.c`
  (DEST spec max == 12, both pin sites); `tests/test_ui_menu.c`
  (DEST row shows "FLT ENV" / "ADSR A"; SEQ DIV at 4 shows "/4").

### Plan deviation (stated, not silent)
The MS2 test bullet "amount -1 on adsr_s -> reject (±100 range)"
contradicts the plan's own DESIGN section ("amounts ±100; new dests fall
into the else branch automatically") — -1 is in range. Followed the
design: adsr_s accepts -1, rejects -101 (boundaries tested both sides).

### Why
PLAN_MOD_FENV_ADSR_DIV.md MS2: presets and the UI expose the five new
mod destinations; SEQ DIV display unified with the sub-oscillator "/n"
convention. No new preset keys — only new VALUES of the existing "dest"
enum; schema stays version 2.

### Test results
- RED (before implementation): test_json 633/641 (writer emitted
  "off" fallbacks, parse rejected), test_seq_items 539/541
  (max == 12 pins), test_ui_menu 257/260 ("FLT ENV"/"ADSR A"/"/4").
- GREEN: test_json 641/641, test_seq_items 541/541, test_ui_menu
  260/260; full `make test` TOTAL 11432/11432 passed.
