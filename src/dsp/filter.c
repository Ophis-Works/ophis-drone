/* src/dsp/filter.c — RBJ biquad (Audio EQ Cookbook), modes LP/HP/BP/BR.
 * Contract: IMPLEMENTATION.md §2. Q comes in via biquad_set_coefs's q
 * param (RES mapping lives in chain.c, PLAN §5). Pure. No SDL/ALSA. */
#include <math.h>

#include "filter.h"

#define OPHIS_2PI 6.28318530717958647692

void biquad_set_coefs(biquad_t *b, filt_mode_t m, float fc, double sr,
                      float q)
{
    double w0 = OPHIS_2PI * (double)fc / sr;
    double cw = cos(w0), sw = sin(w0);
    double alpha = sw / (2.0 * (double)q);
    double a0;

    b->b0 = b->b1 = b->b2 = 0.0;
    switch (m) {
    case FILT_LP:
        b->b0 = (1.0 - cw) / 2.0;
        b->b1 = 1.0 - cw;
        b->b2 = (1.0 - cw) / 2.0;
        break;
    case FILT_HP:
        b->b0 = (1.0 + cw) / 2.0;
        b->b1 = -(1.0 + cw);
        b->b2 = (1.0 + cw) / 2.0;
        break;
    case FILT_BP:            /* constant 0 dB peak gain */
        b->b0 = alpha;
        b->b1 = 0.0;
        b->b2 = -alpha;
        break;
    case FILT_BR:            /* zeros on the unit circle at ±w0: exact notch */
        b->b0 = (1.0 + cw) / 2.0;
        b->b1 = -cw * (1.0 + cw);   /* = -2*cw * b0 (zero condition) */
        b->b2 = (1.0 + cw) / 2.0;
        break;
    }
    a0 = 1.0 + alpha;
    b->a1 = -2.0 * cw;
    b->a2 = 1.0 - alpha;

    /* normalize by a0 */
    b->b0 /= a0; b->b1 /= a0; b->b2 /= a0;
    b->a1 /= a0; b->a2 /= a0;
    /* z1/z2 untouched (S1): per-sample coefficient sweeps keep the
     * filter history; use biquad_set to clear it */
}

void biquad_set(biquad_t *b, filt_mode_t m, float fc, double sr, float q)
{
    biquad_set_coefs(b, m, fc, sr, q);
    b->z1 = b->z2 = 0.0;
}

void biquad_cascade_set(biquad_t *s1, biquad_t *s2, filt_mode_t m,
                        float fc, double sr, float q)
{
    biquad_set(s1, m, fc, sr, q);
    biquad_set(s2, m, fc, sr, q);   /* identical pair, shared cutoff/Q */
}

double biquad_next(biquad_t *b, double x)
{
    /* transposed direct form II */
    double y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}
