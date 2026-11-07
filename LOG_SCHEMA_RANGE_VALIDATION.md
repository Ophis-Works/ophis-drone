# LOG_SCHEMA_RANGE_VALIDATION.md — implementation log.
# Plan: PLAN_SCHEMA_RANGE_VALIDATION.md.

## M1 — RED (range-rejection pins)

22 red pins in tests/test_json.c section 8 (out-of-range and 1e999 rejects,
boundaries inclusive).

## M2 — GREEN + verification

val_range() helper added in src/preset/schema.c; range checks on
chain.freq_hz / sub.freq (by fmode, 0..20000 HZ, 0..4 RATIO — 0.0 legal as
the OFF state) / sub.amount and xmod.amount (by dest, ±50 PITCH, ±100
others) / adsr a,d,r 0..60 / filter.fc 20..20000 / step pitch_hz
0.1..20000. Test data in tests/test_json.c fill_params corrected (RATIO
4.25 was out of the documented ladder). Verification: full suite, desktop
smoke XRUNS 0, handheld gate, zip.
