/* src/dsp/subosc.h — sub-oscillator: an AUDIO voice at parent_f / div
 * (Spice/Subharmonicon style); the chain sums
 * it by its LEVEL. */
#ifndef OPHIS_SUBOSC_H
#define OPHIS_SUBOSC_H

#include <stdint.h>
#include "types.h"
#include "osc.h"

typedef struct { osc_t o; } subosc_state_t;

/* Runs at main_freq_hz / p->div; returns the normalized (-1..1) sample
 * (the RAW output — the LEVEL scaling happens in chain.c, and the MOD
 * matrix taps this raw value regardless of level). */
double subosc_out(subosc_state_t *s, const subosc_params_t *p,
                  double main_freq_hz, double sr);

#endif /* OPHIS_SUBOSC_H */
