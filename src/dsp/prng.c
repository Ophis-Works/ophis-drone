/* src/dsp/prng.c — shared deterministic PRNG (Phase 5 item 1): the
 * xoshiro256** / splitmix64 code moved verbatim out of dsp/osc.c and
 * seq/sequencer.c (identical numbers guaranteed by their tests). */
#include "prng.h"

/* splitmix64 stream generator (also the seeder) */
static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline uint64_t rotl(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

void prng_seed(uint64_t s[4], uint64_t seed)
{
    uint64_t sm = seed;
    for (int i = 0; i < 4; i++)
        s[i] = splitmix64(&sm);
}

uint64_t prng_next(uint64_t s[4])
{
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}
