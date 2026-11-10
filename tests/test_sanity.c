/* tests/test_sanity.c — 1-assert sanity check (no DSP, no SDL/ALSA). */
#include "harness.h"

int main(void)
{
    CHECK(1 + 1 == 2);
    HARNESS_REPORT();
}
