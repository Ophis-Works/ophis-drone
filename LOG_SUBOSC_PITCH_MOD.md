# LOG_SUBOSC_PITCH_MOD.md — implementation log. Plan: PLAN_SUBOSC_PITCH_MOD.md.

## M1 — RED (pins rewritten to vibrato semantics)

- Audited (owner's "check your work"): BOTH PITCH dests computed the
  modulator and discarded it — chain.c `f += amount` (comment "static
  Hz detune"), mixer.c `detune_hz += amount`. PHASE/AMP/FILTER all use
  amount × m; PITCH was the dead one, in either fmode — exactly the
  owner's report ("frequency affects nothing on pitch").
- RED: three pins rewritten to vibrato semantics — test_mod.c
  (sub 2 Hz ±50: zc windows ≈250/≈150), test_chain.c (same end-to-end),
  test_mixer.c (unit: detune_hz = amount × src = 2.0 for amount 4,
  src 0.5). Static code fails them (down-window stays 250).

## M2 — GREEN + docs + verification

- chain.c: `f += amount * m` (depth Hz × normalized modulator; rate =
  sub freq in EVERY fmode; main clamp 0.1..20 kHz unchanged). mixer.c:
  `detune_hz += amount * s` (same semantics as the other dests). The
  sub stays modulation-only.
- Pin-tuning during green (measurement, not semantics): zc resolution
  is 10 Hz per count over 0.05 s windows — windows widened to 0.1 s
  and centered symmetrically on the peak/trough (averages 246.8/153.2,
  quantized 245–250/150–155, tolerance 10). The old "10 kHz spectral
  equality" pin (static 205 == vibrato-averaged spectrum) is invalid
  under FM and was replaced by: average rate back at 200 (not 205) +
  RMS unchanged.
- Kept valid: test_mixer end-to-end 202 Hz pin (src constant 1.0 →
  amount 2 × 1.0 static); test_chain PHASE-only pin (detune_hz == 0).
- Docs: PLAN §5 sub dest + xmod matrix wording (PITCH = vibrato, depth
  Hz, rate = sub freq in every fmode, owner order); manual.html DEST
  row. The sub never sums to out (unchanged).
- Verification: suite 98898/98898; desktop relink + capped smoke
  XRUNS 0; handheld link + gate OK; zip rebuilt. Scratch debug harness
  in /tmp, deleted after use.
