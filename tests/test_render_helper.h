/* tests/test_render_helper.h — D5 (PLAN_OPTIMIZATION.md O7): the ONE
 * engine-render loop for the engine-level pins — per-block copy into a
 * contiguous mono buffer, done once, correctly (the per-pin copies
 * duplicated this and one read out of bounds before M1's fix).
 * Usage: double buf[N]; engine_init(...); set params;
 *        rh_render_mono(&e, buf, N); (gate on the first block only) */
#ifndef OPHIS_TEST_RENDER_HELPER_H
#define OPHIS_TEST_RENDER_HELPER_H

#include <string.h>
#include "../src/audio/engine.h"

#define RH_BLK 256

static void rh_render_mono(engine_t *e, double *mono, int n)
{
    static float stereo[2 * RH_BLK];
    int k = 0;
    for (int b = 0; k < n; b++) {
        engine_render_block(e, stereo, RH_BLK, b == 0, 0, 0);
        for (int i = 0; i < RH_BLK && k < n; i++, k++)
            mono[k] = stereo[2 * i];
    }
}

#endif /* OPHIS_TEST_RENDER_HELPER_H */
