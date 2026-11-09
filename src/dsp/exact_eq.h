/* src/dsp/exact_eq.h — EXACT float equality, on purpose. The -Wfloat-equal
 * convention for the pins and cache checks that must hold BIT-exactly
 * (biquad coefficient cache invalidation, sample-exact envelopes,
 * round-trip bit equality, determinism): a tolerance would either thrash
 * the cache or weaken the pin, so equality is expressed through the
 * integer representation (-Wfloat-equal-clean). fexactf: float operands;
 * fexact: double (a float argument promotes exactly, so it stays
 * injective). -0.0f/+0.0f differ in representation; treating that as
 * "changed" (chain.c fc cache) only forces one harmless extra coefficient
 * pass. */
#ifndef OPHIS_EXACT_EQ_H
#define OPHIS_EXACT_EQ_H

#include <string.h>

static inline int __attribute__((unused)) fexactf(float a, float b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}
static inline int __attribute__((unused)) fexact(double a, double b)
{
    return memcmp(&a, &b, sizeof a) == 0;
}

#endif /* OPHIS_EXACT_EQ_H */
