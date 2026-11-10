/* tools/bench_render.c — PLAN_POLYBLEP.md MS1: render benchmark.
 * Times ITERS engine_render_block calls (256 frames, 44100 Hz) with
 * OPHIS_DEFAULTS and a saw/square-heavy param set; prints µs/block.
 * The saw/square-heavy set maximizes edge density (220 Hz carriers,
 * duty 5/95 edge cases, subs on saw/square) so the PolyBLEP edge path
 * is exercised on nearly every period. Desktop-only, SDL/ALSA-free. */
#include <stdio.h>
#include <time.h>

#include "../src/audio/engine.h"
#include "../src/preset/defaults.h"

#define SR    44100.0
#define FRAMES 256
#define ITERS 2000

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

/* warm-up (envs to sustain), then time ITERS blocks */
static double run(const synth_params_t *p, const char *label)
{
    static engine_t eng;              /* big object, not on the stack */
    static float block[FRAMES * 2];
    engine_init(&eng, SR);
    eng.p = *p;
    for (int i = 0; i < 8; i++)       /* first call triggers (gate on) */
        engine_render_block(&eng, block, FRAMES, i == 0, 0, 0);

    double t0 = now_us();
    for (int i = 0; i < ITERS; i++)
        engine_render_block(&eng, block, FRAMES, 0, 0, 0);
    double per = (now_us() - t0) / (double)ITERS;
    printf("%-18s %8.1f us/block\n", label, per);
    return per;
}

int main(void)
{
    synth_params_t heavy = OPHIS_DEFAULTS;
    /* saw/square-heavy: 220 Hz carriers, duty edge cases 5/95, subs on */
    heavy.chain[0].freq_hz = 220.0f;
    heavy.chain[0].wave = WAVE_SAW;
    heavy.chain[1].freq_hz = 220.0f;
    heavy.chain[1].wave = WAVE_SQUARE;
    heavy.chain[1].duty = 5;
    heavy.chain[2].freq_hz = 220.0f;
    heavy.chain[2].wave = WAVE_SQUARE;
    heavy.chain[2].duty = 95;
    heavy.chain[0].sub[0].div = 2;
    heavy.chain[0].sub[0].wave = WAVE_SAW;
    heavy.chain[0].sub[0].level = 50;
    heavy.chain[1].sub[0].div = 2;
    heavy.chain[1].sub[0].wave = WAVE_SQUARE;
    heavy.chain[1].sub[0].duty = 95;
    heavy.chain[1].sub[0].level = 50;
    heavy.chain[2].sub[1].div = 4;
    heavy.chain[2].sub[1].wave = WAVE_SAW;
    heavy.chain[2].sub[1].level = 50;

    (void)run(&OPHIS_DEFAULTS, "defaults");
    (void)run(&heavy, "saw/square-heavy");
    return 0;
}
