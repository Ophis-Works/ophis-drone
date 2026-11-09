/* src/audio/f32tos16.h — float→S16 conversion for the ALSA S16_LE
 * fallback (PLAN_RG28XX_BUILD.md M2): devices whose "default" ALSA path
 * lacks the format plug reject SND_PCM_FORMAT_FLOAT_LE, so the write path
 * converts into an int16 scratch buffer. Pure C11, SDL/ALSA-free. */
#ifndef OPHIS_F32TOS16_H
#define OPHIS_F32TOS16_H

#include <stdint.h>

/* Convert frames frames of stereo-interleaved [-1, 1] float samples
 * (src, 2*frames floats) to stereo-interleaved int16 (dst, 2*frames
 * int16): clamp to [-1, 1], scale by 32767, round to nearest. */
void f32_to_s16(const float *src, int16_t *dst, int frames);

#endif
