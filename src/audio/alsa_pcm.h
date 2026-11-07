/* src/audio/alsa_pcm.h — thin ALSA PCM wrapper (IMPLEMENTATION.md §2).
 * The ONLY ALSA user (besides test_engine, which uses device "null").
 * S32_FLOAT (SND_PCM_FORMAT_FLOAT_LE), 44100 Hz, stereo, period 256.
 * On XRUN: snd_pcm_recover + drop the block + count it (alsa_xruns).
 * DESKTOP GATE: the cumulative count must stay exactly 0 on desktop
 * (PLAN §2/§10). */
#ifndef OPHIS_ALSA_PCM_H
#define OPHIS_ALSA_PCM_H

typedef struct alsa_pcm alsa_pcm_t;

/* Open device for interleaved stereo float playback at `rate`,
 * soft period `period_frames`. NULL on failure. */
alsa_pcm_t *alsa_open(const char *dev, int rate, int period_frames);

/* Write one block of stereo interleaved float frames.
 * Returns frames written, or -1 if the block was dropped (xrun —
 * recovered + counted — or unrecoverable error). */
int alsa_write(alsa_pcm_t *a, const float *stereo_interleaved, int frames);

/* Cumulative xrun count (recovered underruns/overruns this session). */
long alsa_xruns(const alsa_pcm_t *a);

void alsa_close(alsa_pcm_t *a);

#endif /* OPHIS_ALSA_PCM_H */
