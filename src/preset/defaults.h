/* src/preset/defaults.h — factory defaults (IMPLEMENTATION.md §2). */
#ifndef OPHIS_DEFAULTS_H
#define OPHIS_DEFAULTS_H

#include "../dsp/types.h"

/* A: 55.0 Hz saw, level 100, vel 100, subs off, A.05/D.2/S100/R.5, LP 800
 * B: 55.5 Hz square, level 50, vel 100, sub1 ratio 0.5 sine -> PITCH
 *    +2.0 Hz, sub2 off, A.05/D.2/S100/R.5, LP 600 Hz
 * C: 110.0 Hz sine, level 30, vel 100, subs off, A.1/D.3/S80/R1.0,
 *    LP 1200 Hz
 * seq: all off, bpm 60, all steps {55.0 Hz, vel 100}
 * xmod: all OFF; master 70; run 0 */
extern const synth_params_t OPHIS_DEFAULTS;

#endif /* OPHIS_DEFAULTS_H */
