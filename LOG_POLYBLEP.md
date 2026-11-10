# LOG_POLYBLEP.md — PLAN_POLYBLEP.md work log

## 2026-11-10 — MS3 COMPLETE (docs + full verification)

### Docs

- MANUAL.md §3: terse note — saw/square are band-limited (edge
  smoothing at twice the sample rate), no alias harshness; sine/tri/
  noise unchanged. Matches MS2 KEEP outcome (2x + half-band FIR).
- docs/manual.html: mirrors the wave list, updated both WAVE rows
  (main osc + SUB sections) with the band-limited note.

### Full verification (clean)

- `make clean && make`: desktop build green, -Werror, no warnings.
- `make test`: **204302/204302 passed** (includes test_osc 56/56).
- `make handheld rg28xx`: both build green; GATE PASS on both —
  build/r36s and build/rg28xx, max required **GLIBC_2.17** (<= 2.28).
  No new glibc symbols, as expected (pure arithmetic only).

### Commit

`Document the band-limited oscillators` (unsigned).

## 2026-11-10 — MS2 COMPLETE (ruling option (a): probe moved to the 150th
## fold; GREEN; keep)

### Ruling (a) applied

- PRIMARY probe moved out of the half-band transition band:
  - SAW: 150th harmonic, 33000 Hz -> folds to 11100 Hz;
    |H(33000)| = 0.0157 (bc-verified), predicted ~4042x energy drop.
  - SQUARE: odd harmonics only, so the 149th, 32780 Hz -> 11320 Hz;
    |H(32780)| = 0.0177, predicted ~3183x.
  - RED threshold unchanged: >= 10x energy vs the in-test MS1 1x
    mirror (1x phase path + MS1-approved window-dt kernel).
- SECONDARY probe KEPT (documented choice): the 101st fold (22220 ->
  21880 Hz) asserted at >= 2x energy — its transition-band floor
  (box 2.02x, FIR 3.50x measured; naive ~1.7x, 1x 1.0x both still
  fail it). It pins the passband-edge behaviour the primary cannot.
- Negative controls re-verified EMPIRICALLY against the new primary
  bins via temporary builds (LOG_POLYBLEP.md protocol): naive osc —
  **45/56**, fails both primary and both secondary probe checks (plus
  the expected MS1-era continuity/edge fails); 1x osc — **52/56**,
  fails all 4 probe checks (ratio 1 vs the reference). Both controls
  red, as ruled.

### Final probe numbers (measured, GREEN)

- Saw, 11100 Hz (150th fold): m2x drops >= 10x vs m1x — pass.
- Square, 11320 Hz (149th fold): drops >= 10x vs m1x — pass.
- 101st fold secondary (saw + square): drops >= 2x — pass.

### GREEN

- `make test_osc`: **56/56**. Full `make test`: **213541/213541**.
  test_engine xruns: **0**.

### Final cost table (bench, 2000 blocks, 256 frames, 44100 Hz, -O2,
### 4 runs each)

| set               | baseline | 1x PolyBLEP | 2x + FIR    | delta vs 1x |
|-------------------|----------|-------------|-------------|-------------|
| defaults          | ~64.6 us | ~63.5       | 97.2-99.2   | ~+34 us     |
| saw/square-heavy  | ~56.9 us | ~61.3       | 117.4-118.8 | ~+56 us     |

Keep/drop rule: block budget 5.8 ms (256/44100); worst measured
118.8 us/block is ~49x inside it, test_engine xruns 0 (MS6-hardened
gate). Default-if-within-budget applies: **KEEP**.

### Final implementation (all owner rulings folded in)

`osc_next` tonal path: o->phase holds the output centre (PM/FM deltas
added by chain.c before the call shift it unchanged — delta-PM
telescoping pinned by the test_chain mod tests, all green); phase
advanced by dt, sub-domain values evaluated as pure functions of
phase. SAW/SQUARE: recentered PolyBLEP (window dt, 4-tap: 2 pre + 2
post sub-steps) synthesized at the 5 live tap phases of the 9-tap
half-band FIR decimator (DC-unity, cutoff 22050 Hz — derivation in
the MS2 entry 3 log section). SINE/TRI: recentered 2-sub-step box
average. Noise: 1x untouched. osc_t unchanged; deterministic; no new
state.


## 2026-11-10 — MS2 (third blocker: option (b) FIR implemented as ruled;
## the ruled 10x probe target was unreachable at that bin —
## transition-band geometry, bc-verified; superseded by ruling (a) above)

### Ruling (b) implemented

- PLAN_POLYBLEP.md "cheap 2-tap box average (no extra filter)" amended
  per the ruling (rescinded for the PolyBLEP-corrected waves).
- Decimator: 9-tap linear-phase half-band FIR, cutoff at the 2x-domain
  quarter rate (22050 Hz at the 88200 Hz sub rate). Ideal taps
  h[k] = sin(pi*k/2)/(pi*k) — nonzero only at odd k (+-1 -> 1/pi,
  +-3 -> -1/(3*pi)); half-band zero taps at even k (skip +-2, +-4).
  Hamming window over k=-4..4: w[k] = 0.54 + 0.46*cos(pi*k/4)
  (w1 = 0.86526911934581186, w3 = 0.21473088065418814), DC-normalized
  by S = 0.5 + 2*(w1-w3)/pi = 0.91414550543226879:
    a1 = (w1/pi)/S = +0.30129089216171939
    a3 = (w3/pi)/S = -0.074770331172671340   [sign: -1/(3*pi) tap]
    c0 = 1 - 2*(a1+a3) = 0.54695887802190391
  The centre written as 1-2*(a1+a3) makes the DC sum exactly 1 in fp
  (bc-verified: 1.0000000000000000000000000). Applied to SAW/SQUARE
  only; sine/tri keep the recentered 2-tap average; noise 1x.
- The FIR is evaluated BY SYNTHESIS: the corrected sub-domain signal is
  a pure function of phase, so the 5 live taps are computed at the tap
  phases around the output centre — no history, no latency, no osc_t
  change.
- Contract 1 edge classifier re-pinned 2*dt -> 3.5*dt (the FIR mixes
  +-3h = +-1.5*dt taps around each sample; corrected sub-values reach
  samples up to 2*dt + 1.5*dt of an edge). Everything else green.

### bc-verified response (coefficients above)

H(220)=1.0001  H(5000)=1.0397  H(10000)=1.0832  H(15000)=0.9863
**H(22220)=0.5342**  H(21880)=0.5597

### Probe measurement (21880 Hz, 101st fold — matches the bc prediction)

```
MS1 1x reference   saw 4.790068e-3   square 9.578707e-3
2x + FIR           saw 2.559015e-3   square 5.116735e-3
energy drop        3.50x  (ruled target: >= 10x)  -> RED, test 492/493
```

The 3.50x equals |H(22220)|^-2 as predicted: the fold bin (22220 Hz)
sits 170 Hz above the 22050 Hz cutoff — inside the transition band of
ANY short (or even ~500-tap) FIR whose passband reaches the output
Nyquist. A half-band at 22050 cannot notched it: reaching |H| <=
0.3162 (10x energy) at cutoff+170 Hz needs a transition width under
~340 Hz, i.e. thousands of taps. This is geometry, not filter quality:
the ruled probe bin at 0.995*output-Nyquist will always sit in the
decimator's transition band. By contrast, folds just ~1 kHz+ above the
cutoff are crushed (e.g. the 150th harmonic: 33000 Hz folds to 11100,
|H(33000)| = 0.0151 -> ~4400x energy drop) — the FIR works exactly as
designed everywhere except within a few hundred Hz of the cutoff.

### Everything else (full `make test`)

**147548/147550** — only the 2 ruled probe checks fail. test_engine
xruns: 0. Saw-DC at the original tolerance: PASS (recentered-kernel
symmetric-FIR argument holds: a symmetric DC-unity FIR of the
sub-domain signal preserves the sub-domain mean). MS1 continuity
strict < 2.0: PASS. Untouched waves: bit-exact. PM/FM chain tests:
green.

### Bench (2000 blocks, 256 frames, 44100 Hz, desktop -O2)

| set               | baseline | 1x PolyBLEP | 2x + FIR    |
|-------------------|----------|-------------|-------------|
| defaults          | ~64.6 us | ~63.5       | 98.8-101.3  |
| saw/square-heavy  | ~56.9 us | ~61.3       | 117.7-117.8 |

FIR adds ~+37 us/block on the saw/square-heavy set (7 extra
tonal_evals) on top of the box-average's ~+18. Still ~70x inside the
5.8 ms block budget; test_engine xruns 0 — the keep/drop rule points
to KEEP, but the decision stays pending GREEN.

### Ruling needed (fourth): the probe bin vs the cutoff

The 10x target at the 101st fold cannot be met by any decimator with
its passband reaching 22050 Hz (transition-band geometry above).
Options:
- (a) move the probe to a fold outside the transition band — e.g. the
  150th (33000 -> 11100 Hz: |H| = 0.0157, expected ~4042x energy) or
  the 125th (27500 -> 16600 Hz: |H| = 0.1815, ~30.4x energy); the
  naive and 1x negative controls still fail any such probe (naive
  carries the full harmonic, 1x keeps it ~unattenuated);
- (b) keep the 101st bin and re-set the threshold to the achievable
  drop (>= 3.5x energy at the 101st fold; FIR 3.50x measured vs box
  2.02x — the FIR is a real, bc-explained improvement; naive ~1.73x
  and 1x 1.0x still fail);
- (c) lower the decimator cutoff below 22050 (audible band loss — not
  recommended, and it degrades the 101st fold only marginally unless
  cutoff < ~20 kHz, which costs the top octave outright).

Suite state at this stop: full `make test` 147548/147550 (only the 2
ruled probe checks fail); nothing committed.

## 2026-11-10 — MS2 (second blocker, after owner rulings 1+2 — see the
## earlier MS2 entry at the end of this file for the first round)

### Owner rulings applied

1. **Recentered kernel**: sub-steps now evaluated at centre dt/4
   (`c - dt/4`, `c + dt/4`, uniform dt/2 sub-step spacing across
   output boundaries; `o->phase` holds the output centre, PM/FM deltas
   shift the centre unchanged — test_chain mod tests all green).
   Result: **saw-DC contract PASSES at the ORIGINAL plan tolerance**
   (1/(2*period)); the midpoint shift is gone as ruled — no tolerance
   change needed, no residual to measure over whole periods.
2. **Alias probe replaces the impossible max-jump tightening**: test
   6 is now a Goertzel ENERGY check at 21880 Hz (the 101st harmonic
   fold: 101*220 = 22220 > Nyquist 22050 -> 44100-22220 = 21880).
   Reference arm: an in-test MS1 1x mirror (1x phase path + the
   MS1-approved window-dt kernel). GREEN requires the 2x path to drop
   that bin's energy >= 10x vs the mirror. Renders are 44100 samples =
   exactly 220 periods, so all true harmonics sit on bins k*220 and
   leak nothing into the non-harmonic probe bin (confirmed: measured
   numbers are clean, see below). Negative controls hold: naive
   (101st alias ~ 2/(101*pi) = 6.3e-3, ABOVE the 1x reference) and the
   1x path itself (ratio 1) both fail the 10x probe.

### Second blocker: the ruled 10x target is unachievable with the
### plan-mandated 2-tap box decimator (measured + derived)

Measured at the probe bin (saw and square, duty 50):

```
MS1 1x reference magnitude   saw 4.790e-3   square 9.579e-3
2x (recentered, window=dt)   saw 3.374e-3   square 6.743e-3
drop ratio                   1.42x magnitude = 2.02x energy
```

The 1.42x is EXACTLY the box-average response at that frequency:
|H(22220)| = cos(pi*22220/88200) = 0.703 -> 1/0.703 = 1.4236. The 2x
sub-domain residual at 22220 equals the 1x residual (the PolyBLEP
kernel error is set by kernel-vs-wavelength geometry, not by the
sample rate), so the entire measured gain is the decimator's own
attenuation. The 2-tap box is provably optimal among DC-unity real
2-tap decimators (|H| minimised at a=1/2), so no 2-sub-step averaging
scheme can reach 10x energy at this bin. Notably the full suite
otherwise passes: **213536/213539** — only the 2 probe checks are red;
saw-DC, continuity (MS1 strict < 2.0: passes with the recentered
window=dt kernel; the earlier 1.69 max was the non-recentered variant),
bit-exact/averaging pins, and all test_chain PM-delta
mod tests are green.

Bench cost of the recentered 2x (for the record; keep/drop is moot
until GREEN): defaults ~80.9-83.7 us/block (MS1 ~64.6), heavy
~77.5-81.0 us/block (MS1 ~61.3) — ~+18 us/block (~+29%), far inside
the 5.8 ms block budget.

### Ruling needed (third): reconcile the 10x target with the plan's
### "cheap 2-tap box average (no extra filter)"

- (a) Keep the box decimator and set the probe threshold to the
  physically-achievable drop (>= 2x energy, measured 2.016x — tight
  but a real discriminator: naive 1.73x and 1x 1.0x both fail).
- (b) Swap the decimator to a real half-band FIR (e.g. 4-tap) with a
  stopband null near 22220 Hz — kills that fold almost entirely, meets
  10x trivially; costs a few multiplies/sample and amends the plan's
  "no extra filter" text.
- (c) Move the probe to a higher harmonic fold where the box decimator
  is naturally deep — e.g. the 199th: 199*220 = 43780 Hz folds to
  320 Hz, |H| = cos(pi*43780/88200) = 0.0109 -> expected drop ~92x,
  met by the current implementation as-is.

Suite state at this stop: full `make test` 213536/213539 (only the 2
ruled-probe checks fail); nothing committed.

## 2026-11-10 — MS2 (IN PROGRESS — BLOCKED, pending owner rulings)

### State at the stop (uncommitted, mirrors the MS1 stop protocol)

- `tests/test_osc.c`: MS2 section added — tightened output-rate
  continuity test (line 412, bound `1.0 + 2*dt + 1e-6`) + re-pinned
  contracts 1/4 (far-from-edge deviation bound `slope*dt/2`, square
  far region still bit-exact; noise waves still bit-exact; sine/tri
  within the averaging bound). Suite: **51/56** — 4 continuity fails
  (RED) + 1 saw-DC fail (see ruling 2 below).
- `src/dsp/osc.c`: 2x oversampling implemented the plan-literal way
  (tonal waves run the phase+PolyBLEP path twice per output sample at
  dt/2, box average; noise 1x; mod deltas unchanged — they land in
  `o->phase` before the first sub-step; `osc_t` unchanged, no new
  state). PolyBLEP window kept at the sub-step spacing (plan: "the
  polyBLEP window halves too").

### RED evidence (against MS1 code = PolyBLEP 1x)

The tightened bound test fails all 4 cases (saw + square 50/5/95):
**52/56**. Naive negative control (correction temporarily disabled):
**45/56** — fails the tightened bound 4/4 (naive saw wrap 2-2dt ~1.9902
> 1.010 — note it even passes MS1's strict-2.0 test, so the new bound
was the only naive saw discriminator), naive square 2.0, plus the
expected edge-region/continuity fails. Both controls demonstrably red.

### Why the plan's tightened bound (4*dt -> 2*dt, or any 1 + O(dt)) is
### mathematically unachievable at the output rate (derived + measured)

Mechanism (verified with a phase dump at 220 Hz): when the wrap edge
falls at fractional position s ~ 0.91 of the sub-step grid, the
Si-symmetric 2-sample kernel weights are (1-s)^2 ~ 0.008 (pre) and
s'^2 ~ 0.008 (post) — the -2 step passes essentially uncorrected
(naive sub-step pair jump 1.995) and the box average preserves it:

```
out[197]=0.973016 phase=0.98775510
out[198]=0.982993 phase=0.99274376
out[199]=0.988838 phase=0.99773243   sub-phases .99524/.99773:
                                     corr -0.0083 on s=0.909 pre only
out[200]=-0.992920 phase=0.00272109  first sub-step WRAPPED to
                                     0.00022676: corr +0.0083 only
out[201]=-0.987075 phase=0.00770975
out[202]=-0.977098 phase=0.01269841
```

Measured max output jumps (1000 samples, 220 Hz, 5 periods):

| variant                          | saw 50 | sq 50 | sq 5  | sq 95 |
|----------------------------------|--------|-------|-------|-------|
| naive                            | 1.9902 | 2.0   | 2.0   | 2.0   |
| PolyBLEP 1x (MS1, owner-ruling)  | < 2.0 (measured 1.97-1.999 at
|                                  | the same adversarial alignments)
| 2x, window = h (plan-literal)    | 1.9818 | 1.9917| 1.9979| 1.9917|
| 2x, window = dt (kept at output  | 1.6904 | 1.7004| 1.7262| 1.7004|
| spacing; MEASURED, then rejected)|        |       |       |       |

The window=dt variant lowers the max to ~1.7 but is NOT DC-neutral
(kernel correctness/DC symmetry requires window == spacing): measured
saw mean -0.00272 vs tolerance 0.00249, i.e. it trades a contract
failure for a smaller max. Widening the window further would soften
the edge (a different waveform contract). So the achievable max-norm
statement for the plan-literal 2x is exactly the MS1-inherited strict
< 2.0 — which does not discriminate (naive saw 1.9902 passes it).
A real red needs a different discriminator family; that is an owner
decision, not a free choice (MS1 precedent).

### Second blocker: saw-DC contract at 1/(2*period) under 2x

Measured saw mean **-0.00284** vs tolerance **0.00249** (line 388).
Not a correction bug: the per-edge kernel net is exactly 0 (pre
-(1-a)^2, post +(1-a)^2 mirror); the offset is the box-average
midpoint shift — far from edges the output equals the naive value at
phase + h/2, i.e. +h on the ramp, which shifts the partial-period
residue of the non-integer-period test window by up to ~2h. The same
tolerance was alignment-fragile in MS1 too; MS1's window happened to
pass. The DC tolerance needs an owner-adjusted form for the oversampled
path (e.g. h + 1/(2*period)) or a whole-period-averaged measurement.

### Rulings needed from the owner (both are MS1-style contract changes)

1. Continuity discriminator for MS2: keep strict < 2.0 (no tightening
   possible in max-norm for this kernel family) and add a different
   alias proxy — candidates: (a) count of output samples with
   |jump| > 1.2 over many periods (naive ~1/period, 1x ~1/period
   typical-1.5 edges, 2x only at adversarial alignments — measured
   2-5 of 5 periods at 220 Hz/1000 samples; needs a longer window for
   a stable bound), or (b) a spectral/Goertzel residual-energy proxy.
   The current failing 1+2*dt test stays red until ruled.
2. Saw-DC tolerance under 2x (midpoint shift h): adjust to
   h + 1/(2*period) or equivalent, or pin DC via whole periods.

Bench and keep/drop are NOT run yet (per plan: measure after GREEN).



## 2026-11-10 — MS1 (IN PROGRESS — BLOCKED, see blocker at the end)

### Benchmark baseline (BEFORE any DSP change, naive osc)

`tools/bench_render.c` + `make bench` added: times 2000
`engine_render_block` calls (256 frames, 44100 Hz) for OPHIS_DEFAULTS and
a saw/square-heavy set (all chains saw/square at 220 Hz, duty 5/95 edge
cases, saw/square subs on). Runs (desktop, -O2):

- defaults:          64.6 / 64.5 / 67.4 / 64.5 µs/block  → ~65 µs/block
- saw/square-heavy:  56.9 / 56.1 / 58.0 / 57.1 µs/block  → ~57 µs/block

Baseline pinned for MS1/MS2 deltas: **defaults ~64.6 µs/block, heavy
~56.9 µs/block**.

### RED evidence (naive osc, before implementation)

`tests/test_osc.c` MS1 section: test-local NAIVE mirror (identical phase
accumulation, naive formulas, identical PRNG path) pins the five
structural contracts + the golden-pin rule at 220 Hz, sr 44100, duty
50/5/95, ~4.99 periods (1000 samples). Result against naive `osc_next`:

```
FAIL tests/test_osc.c:278: near_diff > 0    (x4 — saw + square 50/5/95:
    edge region not yet corrected)
FAIL tests/test_osc.c:297: over == 0        (x4 — continuity: naive edge
    jump 2.0 > 4*dt + 1e-6)
44/52 passed
```

RED as planned: continuity fails loudly; the bit-exact-far-from-edge,
golden-pin (trivially, zero differences on naive), duty-mean, untouched
waves and saw-DC contracts pass on naive.

### Negative-control evidence (sign-FLIPPED correction)

Temporarily compiled the correction with `x -= delta * p` instead of the
derived signs; suite result: **42/52 passed** with:

```
FAIL tests/test_osc.c:186: over == 0   (x2 — |x|>1 gate: flipped edge
    values exceed 1)
FAIL tests/test_osc.c:297: over == 0   (x4 — continuity fails loudly)
FAIL tests/test_osc.c:311: duty mean   (x3 — 50/5/95 all off)
FAIL tests/test_osc.c:341: saw DC mean (x1)
```

The flagged-sign risk is demonstrably detected (continuity + duty + DC all
red under the flipped sign). Reverted to naive, then implemented the real
correction.

### Sign derivation (owner-verifiable math)

Correction form: a step of amplitude A at an edge is corrected by
`x -= A * polyblep_step(t)`, `t = (p - e) / dt` (d wrapped to (-0.5,0.5]
via `d -= round(d)`). Step directions re-derived:

- SAW `2p-1`: wrap p:0→1 resets the value +1→−1 = FALLING step A=−2 →
  `x += 2*poly`.
- SQUARE `+1 for p<duty, −1 after`: wrap p≈0: −1→+1 = RISING A=+2 →
  `x -= 2*poly`; duty crossing: +1→−1 = FALLING A=−2 → `x += 2*poly`.

Implemented as `polyblep_correct()` in `src/dsp/osc.c` (per-edge amps
{−2} for saw, {+2,−2} for square), guard `dt >= 0.5`, sine/tri/white/pink
untouched, `osc_t` unchanged.

### OWNER RULINGS (2026-11-10) + MS1 COMPLETION

Both blockers ruled on by the owner:

1. **Continuity bound replaced**: max per-sample jump strictly
   `< 2.0` (was 4·dt + 1e-6). Re-verified negative controls against the
   NEW bound:
   - NAIVE osc: 45/52 — square continuity fails all 3 duty cases
     (naive jump exactly 2.0, fails strict <2.0); the naive SAW wrap is
     2−2dt ≈ 1.9902 and mathematically cannot fail a 2.0 bound, as the
     ruling's "exactly 2.0" reasoning implies (it holds for the square);
     edge-region `near_diff > 0` fails 4/4 (nothing corrected yet).
   - SIGN-FLIPPED correction: 48/52 — continuity fails all 4 cases
     (flipped sign widens the edge pair to ±3-level jumps); duty-mean,
     saw-DC and `near_diff` unaffected by the flip remain green (the
     flip is a bounded local error — the OLD 4-piece flip run had also
     failed duty/DC loudly; recorded earlier in this log).
2. **Overshoot ruled allowed**: the all-waves `no |x| > 1` assertion is
   now `|x| ≤ 2.0` for saw/square (PolyBLEP edge smoothing; downstream
   tanh + master safety filter contain it), still 1.0 for
   sine/tri/white/pink. With the final residual (below) the measured
   peak is ≤ 1.0 — the bound is headroom, not headroom being used.

**Saw-DC red cause**: NOT the bound family — the first implementation's
post-side residual had a constant +2-per-period pair-sum (DC shifted
+2/period ≈ 0.01 ≫ 1/(2·period) tolerance). The residual was re-derived
from the Si-symmetric ideal band-limited step (verified: unit step
corrections pre = +0.5(1−s)², post = −0.5·s², edge diff spread over the
pair with max adjacent jump 1.5·(|A|/2) — worst measured 1.97–1.999 at
the naive-unchanged endpoints, mid-window 1.49) and kept the plan's
±1/(2·period) DC tolerance unchanged; it now passes with mean ≤ 0.002.

**Final implementation** (`polyblep_correct` in `src/dsp/osc.c`): per
edge (SAW wrap A=−2; SQUARE wrap A=+2, duty A=−2), d wrapped to
(−0.5,0.5], |d| < dt: pre-side `x += A·0.5·(1−s)²`, post-side
`x −= A·0.5·s²`, s = |d|/dt. Guard dt ≥ 0.5. Sine/tri/white/pink
untouched; `osc_t` unchanged; no new state.

**One test_chain assertion adapted (not loosened)**: the ENV-AMT filter
test measured the saw's 50th harmonic (10 kHz). Band-limiting reduces
that bin to the numerical floor (raw saw 10 kHz energy 5.8e-3 →
post-filter base 1.5e-5, fc250 2.6e-5 — inverted, floor-dominated), so
the bin moved to the 5th harmonic (1 kHz) with identical factors
(up > base·1.4, down < base/1.4). Same property, same strictness; the
time-domain golden-pin rule test passes, so the underlying waveform
change remains edge-region only.

### GREEN + measured cost

- `make test_osc`: **52/52**. Full `make test`: **199955/199955 passed**.
- Bench (2000 blocks, 256 frames, 44100 Hz), 4 runs each:
  - defaults: baseline ~64.6 µs/block → post 62.6–64.3 (≈ −1 µs,
    within run-to-run noise)
  - saw/square-heavy: baseline ~56.9 µs/block → post ~61.2–61.5
    (**Δ ≈ +4.5 µs/block, +7.9%**) — PolyBLEP edge cost on the
    edge-dense set. Far inside the 5.8 ms block budget
    (256/44100); keep/drop margin for MS2 recorded.


### Interim blocker report (superseded by the rulings above — kept as the
### analysis record that triggered them)

Contract 2 says: every per-sample jump must satisfy
`|x(n)−x(n−1)| ≤ 4·dt + 1e-6`. With the real PolyBLEP implemented
exactly per the Algorithm section, measured max jumps at 220 Hz:

- saw:    max|dx| = 1.486   (bound would be 0.01996)
- square: max|dx| = 1.822   (bound would be 0.01996)

Verified math: `4·dt` equals the naive RAMP slope (saw per-sample rise
`2·dt`; square slope 0) — it is the bound far from edges, not at them.
PolyBLEP's purpose is removing the DISCONTINUITY (instantaneous jump 2.0),
replacing it with a ~2-sample transition whose per-sample diff is O(1),
independent of dt. No band-limited saw/square can satisfy `4·dt + 1e-6`
either: by the Bernstein inequality a signal bandlimited to Nyquist may
change by up to `2π·(fs/2)/fs·max|x| ≈ 3.14` per sample, and the ideal
bandlimited 220 Hz saw's edge transition itself has adjacent diffs of
O(1) (~1.2–1.5). Meeting `4·dt ≈ 0.02` would require spreading the 2-unit
descent over ~1/(2·dt) ≈ 100 samples — half a period — which is not a
band-limited saw.

Per the plan-deviation rule (and the golden-pin stop-rule spirit: never
make a test green by loosening it), MS1 was STOPPED at this point,
uncommitted, pending owner rulings (delivered — see the rulings section
above; the 4·dt analysis below was the justification, the ±3 worst-case
measurements under the FIRST residual led to the re-derived final form).
State at the stop: bench + baseline recorded, RED tests in
`tests/test_osc.c`, first PolyBLEP in `src/dsp/osc.c`, suite 45/52
(4 continuity, 2 `|x|>1` overshoot — PolyBLEP legitimately exceeds ±1 at
edges, and 1 saw-DC shift from the residual's DC bias, see rulings
section).

Decision needed from the owner: replace the contract-2 bound with a
mathematically sound one — e.g. `|x(n)−x(n−1)| ≤ 4·dt + 1.0 + 1e-6`
(allows the O(1) smoothed edge, still fails the naive 2.0 jump? NO —
2.0 < 4dt+1.0+eps would PASS on naive; the discriminator must instead be
the golden-pin + continuity-AT-edge-shape) — or bound the edge diff
directly, e.g. `max|dx| ≤ 2.0` (naive hits exactly 2.0; any smoothing
reduces it below) plus the existing far-from-edge bit-exact pin. The
`no |x| > 1` contract (contract set predates this plan) also conflicts
with PolyBLEP's legitimate overshoot and needs an owner ruling
(wavefold at ±1 after the osc vs. relaxing the check for saw/square).
