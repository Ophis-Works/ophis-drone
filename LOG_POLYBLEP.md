# LOG_POLYBLEP.md — PLAN_POLYBLEP.md work log

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
