/* src/dsp/osc.c — oscillator: sine, tri, saw, square, white, pink.
 * Contract: IMPLEMENTATION.md §2, waveforms per PLAN.md §5.
 * Pure, deterministic (no time()/rand()). No SDL/ALSA. */
#include <math.h>

#include "osc.h"
#include "prng.h"

#define OPHIS_2PI 6.28318530717958647692

/* MS2 half-band FIR decimator (owner ruling 2026-11-10, option b):
 * 9-tap linear-phase half-band, cutoff at the 2x-domain quarter rate
 * (22050 Hz at the 88200 Hz sub rate). Ideal taps h[k] =
 * sin(pi*k/2)/(pi*k) (nonzero only at odd k: +-1 -> 1/pi, +-3 ->
 * -1/(3*pi)), Hamming-windowed over k=-4..4 (w[k] = 0.54 +
 * 0.46*cos(pi*k/4)) and DC-normalized by S = 0.5 + 2*(w1-w3)/pi =
 * 0.91414550543226879. Taps: {a3, 0, a1, 0, c0, 0, a1, 0, a3} with the
 * centre written as 1-2*(a1+a3), which makes the DC sum exactly 1 in
 * floating point. bc-verified: H(220)=1.0001, H(10000)=1.083,
 * H(15000)=0.986, H(22220)=0.534 (fold bin: in-band 21880 leakage
 * x0.560). Derivation in LOG_POLYBLEP.md MS2 entry 3. */
#define HB_A1 0.30129089216171939
#define HB_A3 (-0.074770331172671340)
#define HB_C0 (1.0 - 2.0 * (HB_A1 + HB_A3))

/* wrap a phase into [0, 1) */
static double wrap01(double p)
{
    return p - floor(p);
}

/* PRNG: xoshiro256** seeded via splitmix64 (PLAN §5: fixed seed per
 * instance so tests are reproducible) — shared module dsp/prng.{h,c}
 * (Phase 5 item 1). */

/* uniform double in [-1, 1) from the PRNG */
static double white_sample(osc_t *o)
{
    return (double)(prng_next(o->rng) >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

void osc_init(osc_t *o, wave_t w, uint64_t seed)
{
    o->phase = 0.0;
    o->wave = w;
    o->duty = 50;
    prng_seed(o->rng, seed);
    o->pink[0] = o->pink[1] = o->pink[2] = 0.0;
    o->pink_init = 1;
}

/* Paul Kellet 3-stage filtered pink noise (PLAN §5), driven by xoshiro,
 * state in o->pink. Scaled to stay within -1..1 (test_osc range gate). */
static double pink_sample(osc_t *o)
{
    double white = white_sample(o);
    o->pink[0] = 0.99765 * o->pink[0] + white * 0.0990460;
    o->pink[1] = 0.96300 * o->pink[1] + white * 0.2965164;
    o->pink[2] = 0.57000 * o->pink[2] + white * 1.0526913;
    double out = (o->pink[0] + o->pink[1] + o->pink[2] + white * 0.1848) * 0.2;
    if (out > 1.0) out = 1.0;
    if (out < -1.0) out = -1.0;
    return out;
}

/* PolyBLEP unit-step residual (PLAN_POLYBLEP.md). Verified against the
 * Si-symmetric ideal band-limited step: the sample just BEFORE a rising
 * unit step at fractional distance s (0..1) gains +0.5*(1-s)^2, the
 * sample just AFTER loses 0.5*s^2 — the step value diff of 1 is spread
 * over the pair (max adjacent jump 1.5 for a +-2 step, owner ruling
 * 2026-11-10 bound < 2.0). For a step of amplitude A at edge e
 * (d = p - e wrapped to (-0.5, 0.5], |d| < dt): pre-side x += A*0.5*u^2,
 * post-side x -= A*0.5*s^2, u = 1 - s. Step directions: SAW wrap =
 * FALLING -2 (A=-2); SQUARE wrap p~0 = RISING +2 (A=+2); SQUARE duty
 * crossing = FALLING -2 (A=-2). `phase` is the evaluated phase, dt the
 * correction window (MS2 owner ruling: window = output dt at the dt/2
 * sub-step rate — a 4-tap kernel: 2 pre + 2 post sub-steps; guard
 * dt >= 0.5, freq >= sr/2, naive output is noise there). */
static double polyblep_correct(double x, const osc_t *o, double phase,
                               double dt)
{
    if (dt >= 0.5)                   /* window wider than the domain: */
        return x;                    /* naive output is noise there   */
    double edges[2] = { 0.0, -1.0 }; /* -1 = absent (never in window) */
    double amps[2]  = { -2.0, 0.0 }; /* SAW: one falling edge at wrap */
    int ne = 1;
    if (o->wave == WAVE_SQUARE) {
        int d = o->duty;
        if (d < 5)  d = 5;
        if (d > 95) d = 95;
        edges[1] = (double)d / 100.0;
        amps[0]  = 2.0;              /* wrap: RISING +2 */
        amps[1]  = -2.0;             /* duty crossing: FALLING -2 */
        ne = 2;
    }
    for (int i = 0; i < ne; i++) {
        double d = phase - edges[i];
        d -= round(d);               /* wrap to (-0.5, 0.5] */
        double ad = fabs(d);
        if (ad < dt) {
            double s = ad / dt, u = 1.0 - s;
            if (d < 0.0)
                x += amps[i] * 0.5 * u * u;   /* pre-side  */
            else
                x -= amps[i] * 0.5 * s * s;   /* post-side */
        }
    }
    return x;
}

/* naive tonal value at an arbitrary phase (MS2: sub-steps are evaluated
 * around the output centre, so the value is a pure function of phase) */
static double tonal_eval(const osc_t *o, double phase, double window)
{
    double x;
    switch (o->wave) {
    case WAVE_SINE:
        x = sin(OPHIS_2PI * phase);
        break;
    case WAVE_TRIANGLE:
        x = 4.0 * fabs(phase - 0.5) - 1.0;
        break;
    case WAVE_SAW:
        x = 2.0 * phase - 1.0;
        break;
    case WAVE_SQUARE:
        /* DUTY (approved plan change eacb368+fix): pulse width percent
         * 5..95 — two-thresholds-per-period phase compare against the
         * duty fraction. Out-of-range duty clamps to 5 / 95. */
        {
            int d = o->duty;
            if (d < 5)   d = 5;
            if (d > 95)  d = 95;
            x = (phase < (double)d / 100.0) ? 1.0 : -1.0;
        }
        break;
    case WAVE_WHITE:                   /* unreachable: tonal callers only */
    case WAVE_PINK:
    default:
        x = 0.0;
        break;
    }

    if (o->wave == WAVE_SAW || o->wave == WAVE_SQUARE)
        x = polyblep_correct(x, o, phase, window);
    return x;
}

double osc_next(osc_t *o, double freq_hz, double sr)
{
    switch (o->wave) {
    case WAVE_WHITE:
        return white_sample(o);
    case WAVE_PINK:
        return pink_sample(o);
    case WAVE_SINE:                    /* tonal: oversampled below */
    case WAVE_TRIANGLE:
    case WAVE_SAW:
    case WAVE_SQUARE:
    default:
        break;
    }

    /* tonal waves: 2x oversampled (MS2, owner ruling 2026-11-10) — the
     * sub-domain signal lives at dt/2 sub-step spacing, recentered on
     * the output phase; o->phase holds the centre of the last output;
     * mod inputs (PM/FM) land in it BEFORE osc_next and shift the
     * centre unchanged. Pure arithmetic: osc_t unchanged. */
    double dt = freq_hz / sr;
    double c = o->phase + dt;
    if (c >= 1.0)
        c -= floor(c);
    o->phase = c;

    if (o->wave == WAVE_SAW || o->wave == WAVE_SQUARE) {
        /* half-band FIR decimation (owner ruling option b): the 9-tap
         * kernel is evaluated BY SYNTHESIS at the tap phases (the
         * corrected sub-domain signal is a pure function of phase), so
         * there is no history, no latency, no osc_t change. The
         * half-band zero taps at +-2, +-4 sub-steps are skipped. */
        double h = 0.5 * dt;
        double y = HB_C0 * tonal_eval(o, c, dt);
        y += HB_A1 * (tonal_eval(o, wrap01(c - h), dt)
                      + tonal_eval(o, wrap01(c + h), dt));
        y += HB_A3 * (tonal_eval(o, wrap01(c - 3.0 * h), dt)
                      + tonal_eval(o, wrap01(c + 3.0 * h), dt));
        return y;
    }

    /* sine/tri: no PolyBLEP residual to decimate — recentered 2-sub-step
     * box average (symmetric pair = value at the centre, no DC shift) */
    double q = 0.25 * dt;
    return 0.5 * (tonal_eval(o, wrap01(c - q), dt)
                  + tonal_eval(o, wrap01(c + q), dt));
}
