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
