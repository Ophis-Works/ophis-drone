/* tests/test_engine.c — M4 integration test (PLAN §10, IMPLEMENTATION.md
 * §8 M4). The ONLY test linking ALSA: renders through alsa_pcm on device
 * "null", 44100 Hz, period 256.
 * Checks:
 *   - engine running (RUN on) -> non-silent output (RMS > 1e-4)
 *   - TRIGGER one-shot while RUN off -> audible burst, then silence
 *   - PANIC -> output instantly silent
 *   - alsa_xruns() must be EXACTLY 0 — any xrun fails the test
 * Prints "N/M passed" as the last line (harness contract). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "audio/alsa_pcm.h"
#include "audio/engine.h"
#include "audio/mailbox.h"
#include "preset/defaults.h"

#define SR        44100
#define PERIOD    256
#define BLOCKS(n) ((n) * SR / PERIOD)

static double rms_mono(const float *buf, int frames)
{
    double s = 0.0;
    for (int i = 0; i < frames; i++) {
        double m = (buf[2 * i] + buf[2 * i + 1]) * 0.5;
        s += m * m;
    }
    return sqrt(s / frames);
}

/* Render `blocks` blocks with the given flags; returns the RMS of the last
 * block rendered. */
static double run_blocks(engine_t *e, alsa_pcm_t *pcm, int blocks, int run,
                         int trigger, int panic)
{
    float block[PERIOD * 2];
    double rms = 0.0;
    for (int b = 0; b < blocks; b++) {
        e->p.run = run;
        engine_render_block(e, block, PERIOD, trigger && b == 0,
                            panic && b == 0, 0);
        if (pcm)
            alsa_write(pcm, block, PERIOD);
        rms = rms_mono(block, PERIOD);
    }
    return rms;
}

int main(void)
{
    alsa_pcm_t *pcm = alsa_open("null", SR, PERIOD);
    CHECK(pcm != NULL);
    if (!pcm)
        HARNESS_REPORT();

    static engine_t e;                       /* big object off the stack */
    engine_init(&e, (double)SR);
    e.p = OPHIS_DEFAULTS;

    float win[PERIOD * 2];
    double r;
    (void)win;

    /* 1. RUN on for 1 s -> non-silent (factory defaults: A + B + C drones) */
    r = run_blocks(&e, pcm, BLOCKS(1), 1, 0, 0);
    CHECK(r > 1e-4);

    /* RUN off: gate closes (10 ms) + envelopes release (max R = 1 s);
       after 2 s the output must be silent again. */
    run_blocks(&e, pcm, BLOCKS(2), 0, 0, 0);
    r = run_blocks(&e, pcm, BLOCKS(1), 0, 0, 0);
    CHECK(r < 1e-5);

    /* 2. TRIGGER one-shot while RUN off, with sustain 0 so the envelopes
       reach zero and the one-shot auto-clears (factory S=100 would hold
       the gate open until the 30 s safety deadline — by design). */
    engine_init(&e, (double)SR);
    e.p = OPHIS_DEFAULTS;
    for (int c = 0; c < 3; c++)
        e.p.chain[c].adsr.s = 0;
    run_blocks(&e, pcm, BLOCKS(1), 0, 0, 0);                /* settle */
    r = run_blocks(&e, pcm, 4, 0, 1, 0);                    /* ~23 ms burst */
    CHECK(r > 1e-4);
    run_blocks(&e, pcm, BLOCKS(2), 0, 0, 0);
    r = run_blocks(&e, pcm, BLOCKS(1), 0, 0, 0);
    CHECK(r < 1e-5);

    /* 3. RUN on, then PANIC -> instantly silent (no ramp, no release). */
    run_blocks(&e, pcm, BLOCKS(1), 1, 0, 0);
    r = run_blocks(&e, pcm, 1, 1, 0, 1);
    CHECK(r < 1e-5);
    CHECK(e.p.run == 0);                     /* panic clears RUN */

    /* 3a. H3: RUN belongs to the mailbox (the UI owns RUN). A panic block
     * zeroes e.p.run as a ONE-BLOCK guard only; the next block's run is
     * exactly what the mailbox snapshot holds — the engine never persists
     * a forced run-off (after PANIC the UI commits draft.run = 0, so the
     * drone stays silent unless the UI itself sets run back on). */
    {
        static mailbox_t mb;
        synth_params_t p;
        ui_status_t st;

        mailbox_init(&mb);
        engine_init(&e, (double)SR);
        p = OPHIS_DEFAULTS;
        p.run = 1;
        mailbox_write(&mb, &p);

        /* block 1: panic — guard zeroes run for this block */
        mailbox_read(&mb, &e.p);
        engine_render_block(&e, win, PERIOD, 0, 1, 0);
        st.run = e.p.run;
        CHECK(st.run == 0);

        /* block 2: UI committed draft.run = 0 on PANIC -> still silent */
        p.run = 0;
        mailbox_write(&mb, &p);
        mailbox_read(&mb, &e.p);
        engine_render_block(&e, win, PERIOD, 0, 0, 0);
        st.run = e.p.run;
        CHECK(st.run == 0);

        /* block 3: a UI draft with run = 1 plays again — the engine
         * reflects the mailbox, it does not force run off */
        p.run = 1;
        mailbox_write(&mb, &p);
        mailbox_read(&mb, &e.p);
        engine_render_block(&e, win, PERIOD, 0, 0, 0);
        st.run = e.p.run;
        CHECK(st.run == 1);
    }

    /* 3b. FX present + xruns stay 0: delay mix 100 (impulse-free drone
     * through the delay) must not change the xrun count. */
    e.p.delay.mix = 100;
    run_blocks(&e, pcm, BLOCKS(1), 1, 0, 0);
    e.p.delay.mix = 0;

    /* 3c. safety on attenuates a 60 Hz drone end-to-end (owner-ordered
     * FX amendment: safety = HP 60 Hz + LP 18 kHz in the master path). */
    {
        double amp_on, amp_off;
        engine_init(&e, (double)SR);
        e.p = OPHIS_DEFAULTS;
        e.p.chain[0].freq_hz = 60.0f;
        e.p.chain[1].level = 0;
        e.p.chain[2].level = 0;
        for (int s = 0; s < 3; s++) e.p.seq[s].on = 0;
        e.p.safety = 1;
        run_blocks(&e, pcm, BLOCKS(1), 1, 0, 0);        /* settle */
        amp_on = run_blocks(&e, pcm, BLOCKS(1), 1, 0, 0);
        e.p.safety = 0;
        run_blocks(&e, pcm, BLOCKS(1), 1, 0, 0);        /* settle */
        amp_off = run_blocks(&e, pcm, BLOCKS(1), 1, 0, 0);
        CHECK(amp_on < amp_off);        /* safety on -> quieter at 60 Hz */
    }

    /* 4. XRUN GATE: cumulative ALSA xrun count must be exactly 0. */
    CHECK(alsa_xruns(pcm) == 0);
    printf("xruns: %ld\n", alsa_xruns(pcm));

    alsa_close(pcm);
    HARNESS_REPORT();
}
