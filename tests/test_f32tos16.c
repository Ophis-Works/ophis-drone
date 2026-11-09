/* tests/test_f32tos16.c — the float→S16 conversion helper used by the
 * ALSA S16 fallback (PLAN_RG28XX_BUILD.md M2). SDL/ALSA-free pure math:
 * stereo-interleaved, clamp to [-1, 1], scale by 32767, round-to-nearest. */
#include "harness.h"
#include "audio/f32tos16.h"

int main(void)
{
    int16_t out[8];

    /* zero maps to zero (both channels of one frame) */
    const float z[2] = { 0.0f, 0.0f };
    f32_to_s16(z, out, 1);
    CHECK(out[0] == 0 && out[1] == 0);

    /* unity maps to full scale */
    const float u[2] = { 1.0f, -1.0f };
    f32_to_s16(u, out, 1);
    CHECK(out[0] == 32767 && out[1] == -32767);

    /* out-of-range input clamps, does not overflow */
    const float big[4] = { 2.0f, -2.0f, 100.0f, -100.0f };
    f32_to_s16(big, out, 2);
    CHECK(out[0] == 32767 && out[1] == -32767);
    CHECK(out[2] == 32767 && out[3] == -32767);

    /* rounding: 0.5f * 32767 = 16383.5 -> nearest-even 16384 */
    const float h[2] = { 0.5f, -0.5f };
    f32_to_s16(h, out, 1);
    CHECK(out[0] == 16384 && out[1] == -16384);

    /* multi-frame ordering is preserved (interleaved L,R,L,R) */
    const float seq[8] = { 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f };
    f32_to_s16(seq, out, 4);
    CHECK(out[0] == 32767 && out[1] == 0);
    CHECK(out[2] == 0 && out[3] == 32767);
    CHECK(out[4] == -32767 && out[5] == 0);
    CHECK(out[6] == 0 && out[7] == -32767);

    /* tiny value truncates to the nearest quantization step, stays small */
    const float tiny[2] = { 1.0f / 32767.0f, 0.0f };
    f32_to_s16(tiny, out, 1);
    CHECK(out[0] == 1 && out[1] == 0);

    HARNESS_REPORT();
}
