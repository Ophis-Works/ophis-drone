/* src/dsp/osc.c — oscillator: sine, tri, saw, square, white, pink.
 * Contract: IMPLEMENTATION.md §2, waveforms per PLAN.md §5.
 * Pure, deterministic (no time()/rand()). No SDL/ALSA. */
#include <math.h>

#include "osc.h"
#include "prng.h"

#define OPHIS_2PI 6.28318530717958647692

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
 * crossing = FALLING -2 (A=-2). */
static double polyblep_correct(double x, const osc_t *o, double freq_hz,
                               double sr)
{
    double dt = freq_hz / sr;
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
        double d = o->phase - edges[i];
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

double osc_next(osc_t *o, double freq_hz, double sr)
{
    double x;

    switch (o->wave) {
    case WAVE_WHITE:
        return white_sample(o);
    case WAVE_PINK:
        return pink_sample(o);
    case WAVE_SINE:                    /* tonal: fall through to the */
    case WAVE_TRIANGLE:                /* phase-accumulated path below */
    case WAVE_SAW:                     /* (PLAN §5) */
    case WAVE_SQUARE:
    default:
        break;
    }

    /* tonal waves: phase-accumulated (PLAN §5) */
    o->phase += freq_hz / sr;
    if (o->phase >= 1.0)
        o->phase -= floor(o->phase);

    switch (o->wave) {
    case WAVE_SINE:
        x = sin(OPHIS_2PI * o->phase);
        break;
    case WAVE_TRIANGLE:
        x = 4.0 * fabs(o->phase - 0.5) - 1.0;
        break;
    case WAVE_SAW:
        x = 2.0 * o->phase - 1.0;
        break;
    case WAVE_SQUARE:
        /* DUTY (approved plan change eacb368+fix): pulse width percent
         * 5..95 — two-thresholds-per-period phase compare against the
         * duty fraction. Out-of-range duty clamps to 5 / 95. */
        {
            int d = o->duty;
            if (d < 5)   d = 5;
            if (d > 95)  d = 95;
            x = (o->phase < (double)d / 100.0) ? 1.0 : -1.0;
        }
        break;
    case WAVE_WHITE:                   /* unreachable: returned above */
    case WAVE_PINK:
    default:
        x = 0.0;
        break;
    }

    if (o->wave == WAVE_SAW || o->wave == WAVE_SQUARE)
        x = polyblep_correct(x, o, freq_hz, sr);
    return x;
}
