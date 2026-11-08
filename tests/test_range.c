/* test_range.c — ladder sanity for the pure position helpers
 * (src/ui/range.c). PLAN_HZ_EDIT.md P5/R9: the Hz-ladder pins
 * (pos_to_hz/hz_to_pos, pos 0..212) are DELETED — the 1/12-octave edit
 * ladder is going away with the odometer-box rework; the ADSR seconds
 * ladder (D6) is untouched and stays pinned here.
 * SDL-free: pure math module src/ui/range.c. */
#include "harness.h"
#include "../src/ui/range.h"

int main(void)
{
    /* ADSR seconds ladder (D6, R9 untouched): pos 0 = 0 s;
     * 1..50 = 0.001*60000^((pos-1)/49), so pos 1 = 0.001 s and
     * pos 50 = 60 s (0.001 * 60000^1). */
    CHECK(fexact(adsr_pos_to_s(0), 0.0));      /* exact ladder endpoints */
    CHECK(fexact(adsr_pos_to_s(1), 0.001));
    CHECK(fexact(adsr_pos_to_s(50), 60.0));
    {
        double prev = -1.0;                /* strictly monotonic */
        for (int pos = 0; pos <= 50; pos++) {
            double t = adsr_pos_to_s(pos);
            CHECK(t > prev);
            prev = t;
        }
    }
    /* exact round-trip for every nonzero rung; 0 s and hostile values
     * floor at pos 0 */
    for (int pos = 1; pos <= 50; pos++)
        CHECK(adsr_s_to_pos(adsr_pos_to_s(pos)) == pos);
    CHECK(adsr_s_to_pos(0.0) == 0);
    CHECK(adsr_s_to_pos(-1.0) == 0);

    HARNESS_REPORT();
}
