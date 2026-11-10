/* src/dsp/osc.h — oscillator: sine, tri, saw, square, white, pink.
 * Declarations only. */
#ifndef OPHIS_OSC_H
#define OPHIS_OSC_H

#include <stdint.h>
#include "types.h"

typedef struct {
  double   phase;
  wave_t   wave;
  int      duty;        /* square pulse width percent 5..95 (default 50);
                         read only by WAVE_SQUARE, clamped on use */
  uint64_t rng[4];      /* xoshiro256** state, seeded per instance */
  double   pink[3];     /* Paul Kellet 3-stage filter state */
  int      pink_init;
} osc_t;

/* phase=0, duty=50, reset PRNG with the given seed (deterministic per
 * instance). */
void   osc_init(osc_t *o, wave_t w, uint64_t seed);

/* advance one sample; returns -1..1. Noise ignores freq_hz. */
double osc_next(osc_t *o, double freq_hz, double sr);

#endif /* OPHIS_OSC_H */
