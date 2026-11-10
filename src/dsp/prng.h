/* src/dsp/prng.h — shared deterministic PRNG: xoshiro256** seeded via
 * splitmix64 — the
 * one copy of the algorithm previously duplicated in dsp/osc.c and
 * seq/sequencer.c. Pure, deterministic (no time()/rand()). No SDL/ALSA.
 * The identical generators+seeds guarantee identical numbers everywhere
 * (pinned byte-identical by tests/test_osc.c and tests/test_sequencer.c). */
#ifndef OPHIS_PRNG_H
#define OPHIS_PRNG_H

#include <stdint.h>

/* seed a 4-word xoshiro256** state from `seed` via splitmix64 */
void prng_seed(uint64_t s[4], uint64_t seed);

/* advance the state one step; returns the xoshiro256** output */
uint64_t prng_next(uint64_t s[4]);

#endif /* OPHIS_PRNG_H */
