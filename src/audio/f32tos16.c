/* src/audio/f32tos16.c — float→S16 conversion (see f32tos16.h). */
#include <math.h>   /* lrintf */

#include "f32tos16.h"

void f32_to_s16(const float *src, int16_t *dst, int frames)
{
    for (int i = 0; i < 2 * frames; i++) {
        float s = src[i];
        if (s > 1.0f)
            s = 1.0f;
        else if (s < -1.0f)
            s = -1.0f;
        dst[i] = (int16_t)lrintf(s * 32767.0f);
    }
}
