# PLAN_POLYBLEP.md — band-limited saw/square via PolyBLEP (kill the 8-bit edge)

Single file: design plan + implementation plan. Owner decisions
(2026-11-10): option 1 (PolyBLEP) now; option 2 (2× oscillator
oversampling) as a SECONDARY milestone, adopted only if the measured
render-cost impact is acceptable — every milestone records numbers from a
shared benchmark. Red tests must cover the flagged sign risk (negative
controls).

## System requirements (checked 2026-11-10 — all present)

gcc 15.2 (C23), pkg-config sdl2+alsa, cross toolchain + arm64 libs, readelf
(gate), zip. Nothing missing.

## Design plan

### Requirement

Remove the aliasing caused by the naive discontinuous waveforms in
`src/dsp/osc.c` (saw `2×phase−1`, square duty-compare) by applying a
2-sample PolyBLEP correction at every waveform edge. Sine, triangle, white,
pink are untouched (already continuous / noise). Applies automatically to
the main osc AND both subs — all three call the one `osc_next`.

### Facts

- `osc_next` (`src/dsp/osc.c:45-95`): phase-accumulated path for
  SINE/TRI/SAW/SQUARE; `o->phase += freq/sr`, wrap by floor; naive saw and
  duty-clamped square per PLAN §5.
- All consumers: `chain_render` (main osc, `chain.c:79`) and `subosc_out`
  (`subosc.c:14`) — one function, one fix point.
- Mod inputs (PM phase shift `chain.c:77`, FM via phase_mod) land in
  `o->phase` BEFORE `osc_next` runs — the correction keys off the phase
  value only, so modulated carriers are corrected identically. No
  interaction with the new ADSR/FLT_ENV mod dests.
- RT budget: the correction runs only when an edge is within 2 samples
  (`2×dt` of phase space, `dt = freq/sr`); cost is a compare + a few
  multiplies on those samples. Negligible vs the removed per-sample cost
  elsewhere (compare: MS4 LUT milestone).

### Algorithm (the exact contract)

Per sample, AFTER the naive value `x` is computed and ONLY for SAW/SQUARE
(`dt = freq_hz / sr`, phase in [0,1)):

```
p = o->phase (post-wrap value used for the naive compare)
For each edge e in the wave's edge set (SAW: {0.0};
SQUARE: {0.0, duty/100}):
    d = p - e (wrapped to (-0.5, 0.5] via d -= round(d))
    if |d| < 2*dt:
        t = d / dt                       (in (-2, 2))
        x += delta * polyblep_step(t)
polyblep_step(t):  0                                  t <= -1
                   0.5*(t+1)^2                        -1 < t < 0
                   1 - 0.5*t^2                        0 <= t < 1
                   0.5*(2-t)^2 ... wait — see sign note below
```

Sign note (owner-verifiable math): PolyBLEP corrects a UNIT STEP
`u(n)` by subtracting `polyblep(t) = 0.5*(t+1)^2` for t∈(−1,0),
`1 − 0.5*t^2` for t∈[0,1), 0 outside, where t = distance from the step
in samples. SAW = −1 + 2·ramp (ramp = phase) — its wrap edge is a FALLING
step of −2 → correction `x += 2·polyblep(t)` at phase 0 with t =
phase/dt. SQUARE edges are ±2 steps (rising at duty/100 crossing... the
naive value is +1 for p < duty, −1 after): the p≈0 wrap is a RISING step
of +2 → `x −= 2·polyblep(t)`; the p≈duty crossing is a FALLING step of −2
→ `x += 2·polyblep(t)`. The implementer must re-derive signs from these
step directions and pin them with the continuity test below — a wrong
sign INCREASES the jump and fails the test loudly.

Guard: if `dt >= 0.5` (freq ≥ Nyquist/2) skip the correction entirely
(window wider than the domain; naive output is noise there anyway) —
prevents a division-driven negative-t invalid region.

Determinism: pure arithmetic on existing state, no new PRNG/state fields —
`osc_t` unchanged.

### Design decisions

- In-place in `osc_next` (one fix point for main + subs), not a wrapper —
  callers unchanged.
- No lookup tables; the polynomial is 2 branches + 2 multiplies on edge
  samples only.
- Tests verify by STRUCTURE, not FFT (no FFT dep in the harness):
  1. Bit-exact: samples farther than 2·dt from every edge are identical
     to the naive formula (pin against the pre-change formula).
  2. Continuity (the alias killer): over many periods of a low-freq saw
     and square, every per-sample jump |x(n)−x(n−1)| ≤ 4·dt + 1e-6
     (naive saw has a jump of 2 at the wrap; PolyBLEP removes it).
  3. Square duty preserved: mean over a period ≈ 2·duty/100 − 1
     (tolerance 1/(period)); both edges corrected.
  4. Sine/tri/white/pink: bit-identical to before (untouched paths).
  5. DC bias unchanged for saw (mean ≈ 0 ± 1/(2·period)).
- MANUAL wording: "band-limited (PolyBLEP) saw/square" note in the wave
  section; no UI change (no new params).

## Design addition — 2× oscillator oversampling (secondary)

After PolyBLEP, the remaining aliasing is from curvature (the correction
removes steps, not all images). 2× oversampling of the osc stage halves the
fold-down of whatever remains: run `osc_next` at 88200 Hz internally, with
sub-steps RECENTERED on the output phase (offsets ±dt/4; owner ruling
2026-11-10 — kills the DC midpoint shift), and decimate. AMENDED (owner
ruling 2026-11-10, option b): the 2-tap box average is replaced by a 9-tap
half-band FIR (DC-unity, linear phase, cutoff 22050 Hz) on the
PolyBLEP-corrected waves (SAW/SQUARE — "no extra filter" rescinded);
sine/tri keep the recentered 2-tap average (no PolyBLEP residual to
decimate); noise waves stay 1× (already white). Cost: ~4.5× osc-stage
arithmetic on saw/square, 2× on sine/tri; the PolyBLEP window stays at dt
(4-tap: 2 pre + 2 post sub-steps).

Keep/drop rule (measured, not guessed): MS2 records µs/block before and
after; if the oversampled render exceeds the MS1 render by more than the
MS1 measurement headroom against the 5.8 ms block budget (numbers go in
the log; the block budget at 44100/256 is 5.8 ms and MS6-hardened builds
must stay xrun-free), report to the owner with both numbers — the owner
decides keep or drop. Default if within budget: keep.

## Implementation plan

Method: TDD per milestone — RED → GREEN → full `make test` → commit.
Sequential subagents; log to `LOG_POLYBLEP.md` (dated entries: change,
rationale, test results, MEASURED numbers). Commits imperative, unsigned.

### MS1 — PolyBLEP in osc_next + negative-control red tests
(src/dsp/osc.c, tests/test_osc.c, tools/bench_render.c)

- Benchmark first (RED where applicable): add `tools/bench_render.c` +
  `make bench` target — times N=2000 `engine_render_block` calls
  (OPHIS_DEFAULTS + saw/square-heavy params) and prints µs/block. Baseline
  number recorded in the log BEFORE any DSP change (naive reference for
  MS1/MS2 deltas).
- Negative controls (the flagged-sign risk) — RED, and must STAY
  demonstrably red:
  1. Continuity test vs naive (primary red): naive saw/square jump 2.0
     at edges — assertion |x(n)−x(n−1)| ≤ 4·dt + 1e-6 fails loudly.
  2. Sign-flip control: the same continuity + duty tests must ALSO fail
     against a deliberately sign-inverted correction — proven once in the
     RED phase by temporarily compiling the correction with flipped signs
     and showing the suite fails (evidence in the log, then reverted).
     This proves the test detects the flagged risk, not just naive code.
  3. Golden-pin rule test: a test asserting the ONLY samples that differ
     from the naive wave are within 2·dt of an edge (sample-distance
     bound) — directly encodes the "edge-region only" stop-rule of the
     old plan text so a broader output change is caught automatically
     instead of relying on eyeballing golden diffs.
- Plus the five structural contracts from the design section (bit-exact
  far-from-edge, continuity, duty mean, untouched waves, saw DC).
  Parameters: ~220 Hz, sr 44100, duty 50 and the 5/95 edge cases, ≥ 4
  periods.
- Implement: edge-correction block per the Algorithm section; guard
  dt ≥ 0.5; sine/tri/noise paths untouched.
- Verify: all new tests GREEN; full `make test` GREEN; bench re-run,
  µs/block delta vs baseline recorded in the log.
- Commit: `Band-limit the saw and square oscillators with PolyBLEP`

### MS2 — 2× oscillator oversampling + measured keep/drop
(src/dsp/osc.c, tests/test_osc.c)

- Tests first (RED): alias-floor proxy — after MS1 the per-sample jump
  bound tightens; with 2× oversampling assert the same continuity bound
  at the OUTPUT rate still holds and additionally that the residual
  edge-region deviation from the ideal continuous waveform (integral of
  slope) is smaller than MS1's (exact bound: the MS1 test's 4·dt constant
  tightens to 2·dt — encode the tighter bound as the new red test).
  Untouched-wave and bit-exact contracts re-pinned for the new path.
- Implement: for tonal waves run the phase/correction path twice per
  output sample at dt/2 and average; noise paths untouched; PM/FM mod
  deltas applied once per output sample (phase jump feeds the first
  sub-step; the delta-PM telescoping in chain.c must stay correct — pin
  with an existing mod test).
- Measure: bench µs/block MS2 vs MS1 vs baseline, all three in the log;
  apply the keep/drop rule above and REPORT the decision with numbers.
- Full `make test` GREEN; commit: `Oversample the tonal oscillator stage
  2x` (or `Reject 2x oscillator oversampling: measured cost` + log
  rationale if dropped).

### MS3 — docs + full verification

- MANUAL.md wave note ("band-limited saw/square" + oversampling status
  per MS2 outcome); docs/manual.html if it mirrors the wave list; dated
  log entry.
- Verify: `make clean && make && make test && make handheld rg28xx` all
  green, both GATE PASS (no new glibc symbols expected; gate failure →
  STOP and report).

### Commit sequence

1. `Add a render benchmark and band-limit saw/square with PolyBLEP`
2. `Oversample the tonal oscillator stage 2x` (or the measured-rejection
   commit per the keep/drop rule)
3. `Document the band-limited oscillators`

## Verification summary

- MS1: negative controls (naive + sign-flipped both provably red), five
  contracts green, bench baseline + delta recorded.
- MS2: tighter red bound green; three-number cost table in the log;
  keep/drop decided by the stated rule.
- MS3: clean build + tests + both handheld gates.
- Math verification (rule): step-direction signs re-derived in the log
  entry and pinned by the continuity + duty + sign-flip-control tests.
