/* src/dsp/filter.h — RBJ biquad, modes LP/HP/BP/BR.
 * Contract: IMPLEMENTATION.md §2. Declarations only. */
#ifndef OPHIS_FILTER_H
#define OPHIS_FILTER_H

#include "types.h"

typedef struct {
  double b0, b1, b2, a1, a2;   /* coefficients */
  double z1, z2;               /* state */
} biquad_t;

/* compute + normalize the coefficients for m/fc/q; z1/z2 are NOT
 * touched (S1, PLAN_CODE_REVIEW_FIXES.md Phase 1): the per-sample
 * ENV-cutoff path updates coefficients while the filter keeps its
 * history */
void   biquad_set_coefs(biquad_t *b, filt_mode_t m, float fc, double sr,
                        float q);
/* set_coefs + state clear (z1/z2 = 0) — use when the filter topology
 * changed or the history should restart */
void   biquad_set(biquad_t *b, filt_mode_t m, float fc, double sr, float q);
double biquad_next(biquad_t *b, double x);

/* poles=24 (PLAN §5): two identical biquads sharing one cutoff/Q,
 * cascaded. Sets both stages and zeroes their state; render with
 * y = biquad_next(&s2, biquad_next(&s1, x)). */
void   biquad_cascade_set(biquad_t *s1, biquad_t *s2, filt_mode_t m,
                          float fc, double sr, float q);

#endif /* OPHIS_FILTER_H */
