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

double osc_next(osc_t *o, double freq_hz, double sr)
{
    double x;

    switch (o->wave) {
    case WAVE_WHITE:
        return white_sample(o);
    case WAVE_PINK:
        return pink_sample(o);
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
    default:
        x = 0.0;
        break;
    }
    return x;
}
