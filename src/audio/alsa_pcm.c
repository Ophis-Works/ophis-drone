/* src/audio/alsa_pcm.c — thin ALSA PCM wrapper (IMPLEMENTATION.md §2).
 * No malloc/printf in the steady-state write path. Pure C11. */
#include <stdlib.h>
#include <alsa/asoundlib.h>

#include "alsa_pcm.h"

struct alsa_pcm {
    snd_pcm_t *pcm;
    int        rate;
    int        period_frames;
    long       xruns;      /* cumulative recovered xrun count */
};

alsa_pcm_t *alsa_open(const char *dev, int rate, int period_frames)
{
    alsa_pcm_t *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    a->rate = rate;
    a->period_frames = period_frames;

    if (snd_pcm_open(&a->pcm, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        free(a);
        return NULL;
    }
    /* Buffer = 4 periods (soft period = period_frames): 256 frames @ 44100
     * ≈ 5.8 ms period, ≈ 23 ms total buffer. period_frames/rate are
     * positive (main.c SR/PERIOD constants), so the casts above are
     * value-preserving. */
    unsigned int latency_us =
        (unsigned int)((unsigned long long)period_frames * 4ull
                       * 1000000ull
                       / (unsigned long long)(unsigned int)rate);
    int rc = snd_pcm_set_params(a->pcm, SND_PCM_FORMAT_FLOAT_LE,
                                SND_PCM_ACCESS_RW_INTERLEAVED,
                                2, (unsigned int)rate, 1, latency_us);
    if (rc < 0) {
        snd_pcm_close(a->pcm);
        free(a);
        return NULL;
    }
    return a;
}

int alsa_write(alsa_pcm_t *a, const float *stereo_interleaved, int frames)
{
    snd_pcm_sframes_t w =
        snd_pcm_writei(a->pcm, stereo_interleaved, (snd_pcm_uframes_t)frames);
    if (w < 0) {
        /* XRUN (or suspended stream): recover, DROP the block, count it. */
        if (snd_pcm_recover(a->pcm, (int)w, 1) < 0)
            return -1;              /* unrecoverable — report as dropped */
        a->xruns++;
        return -1;                  /* block dropped */
    }
    return (int)w;
}

long alsa_xruns(const alsa_pcm_t *a)
{
    return a->xruns;
}

void alsa_close(alsa_pcm_t *a)
{
    if (!a)
        return;
    snd_pcm_drain(a->pcm);
    snd_pcm_close(a->pcm);
    free(a);
}
