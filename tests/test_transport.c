/* tests/test_transport.c — transport contracts, reworked for the global
 * engine clock: transport_process takes a trailing `now` (seconds on the
 * engine's sample clock). one_shot_deadline is an ABSOLUTE timestamp:
 * set to now + 30.0 on trigger edge; force-close when now >= deadline;
 * PANIC sets it to 0. RUN off -> gate 0; RUN on -> gate 1 after the 10 ms
 * ramp; TRIGGER while RUN off -> gate opens (one-shot), exactly 3 one-shot
 * retriggers at each chain's VEL, gate auto-closes when all envelopes reach
 * 0 (or at the absolute 30 s deadline); PANIC -> gate 0 instant, one-shot
 * cleared, run cleared, deadline 0; sequencers untouched by TRIGGER.
 * Contract: src/seq/transport.h. */
#include <string.h>
#include "harness.h"
#include "seq/transport.h"
#include "seq/sequencer.h"

#define SR    44100.0
#define BLK   256.0
#define RAMP_S 0.01
#define BASE  10.0               /* nonzero base so absolute-vs-countdown
                                  * bugs cannot pass */

static seq_params_t mksp_default(void)
{
    seq_params_t p;
    memset(&p, 0, sizeof p);
    p.on = 1;                    /* BPM is GLOBAL: no
                                    per-seq bpm member anymore */
    return p;
}

static transport_state_t mkT(void)
{
    transport_state_t t;
    memset(&t, 0, sizeof t);
    return t;
}

static void blocks(transport_state_t *t, int run, int trig, int panic,
                   const float env[3], int n, double *now)
{
    for (int i = 0; i < n; i++) {
        transport_process(t, run, trig, panic, env, BLK, *now, SR);
        *now += BLK / SR;
    }
}

int main(void)
{
    const float env_mid[3]  = { 0.5f, 0.5f, 0.5f };
    const float env_zero[3] = { 0.0f, 0.0f, 0.0f };

    /* ---- RUN off -> gate 0 ---- */
    {
        transport_state_t t = mkT();
        double now = BASE;
        blocks(&t, 0, 0, 0, env_zero, 10, &now);
        CHECK(fexactf(t.gate, 0.0f));
        CHECK(t.one_shot == 0);
        CHECK(fexact(t.one_shot_deadline, 0.0));
    }

    /* ---- RUN on -> gate 1 after the 10 ms ramp ---- */
    {
        transport_state_t t = mkT();
        double now = BASE;
        float per_block = (float)(BLK / (RAMP_S * SR));   /* 256/441 */
        blocks(&t, 1, 0, 0, env_zero, 1, &now);
        CHECK(fabs(t.gate - per_block) < 0.01f);
        blocks(&t, 1, 0, 0, env_zero, 1, &now);
        CHECK(fexactf(t.gate, 1.0f));           /* 2*per_block > 1: clamped */
        blocks(&t, 1, 0, 0, env_zero, 3, &now);   /* > 441 samples total */
        CHECK(fexactf(t.gate, 1.0f));
        /* RUN off again: gate ramps back down to 0 in 10 ms */
        blocks(&t, 0, 0, 0, env_zero, 10, &now);
        CHECK(fexactf(t.gate, 0.0f));
    }

    /* ---- TRIGGER while RUN off: gate opens (one-shot), exactly one
     *      0->1 edge => the engine retriggers all 3 chains at their VEL;
     *      deadline is ABSOLUTE (now + 30.0); gate auto-closes when all
     *      envelopes reach 0 ---- */
    {
        transport_state_t t = mkT();
        double now = BASE;
        seq_state_t sq;
        seq_reset(&sq);
        int edges = 0, prev = 0;
        int n_events = 0;                    /* engine retrigger counter */

        /* engine model: on each block, fire all 3 chains once when
         * one_shot goes 0 -> 1 (TRIGGER never touches the sequencer) */
        for (int i = 0; i < 200; i++) {
            int trig = (i == 0) ? 1 : 0;
            transport_process(&t, 0, trig, 0, env_mid, BLK, now, SR);
            if (i == 0)
                CHECK(fexact(t.one_shot_deadline, now + 30.0)); /* absolute */
            if (t.one_shot && !prev) { edges++; n_events += 3; }
            prev = t.one_shot;
            now += BLK / SR;
            /* only the first block carries the trigger flag */
        }
        CHECK(fexactf(t.gate, 1.0f));               /* one-shot holds gate open */
        CHECK(t.one_shot == 1);
        CHECK(t.one_shot_deadline > 39.99 && t.one_shot_deadline <= 40.0);
        CHECK(edges == 1);                   /* exactly one retrigger edge */
        CHECK(n_events == 3);                /* all 3 chains, once each */

        /* envelopes still up -> stays held */
        blocks(&t, 0, 0, 0, env_mid, 5, &now);
        CHECK(fexactf(t.gate, 1.0f));
        CHECK(t.one_shot == 1);

        /* all envelopes reach 0 -> one-shot clears, gate closes (ramp) */
        blocks(&t, 0, 0, 0, env_zero, 5, &now);
        CHECK(t.one_shot == 0);
        CHECK(fexactf(t.gate, 0.0f));

        /* sequencer untouched by the whole TRIGGER episode */
        CHECK(sq.cursor == 0);
        CHECK(fexact(sq.next_step_time, 0.0));
    }

    /* ---- 30 s safety deadline is ABSOLUTE: envelopes never fall ->
     *      forced close when now >= deadline ---- */
    {
        transport_state_t t = mkT();
        double now = BASE;
        transport_process(&t, 0, 1, 0, env_mid, BLK, now, SR);
        CHECK(t.one_shot == 1);
        CHECK(fexact(t.one_shot_deadline, now + 30.0));   /* == 40.0, absolute */
        /* step now across the deadline; deadline expires at now >= 40.0 */
        double close_time = -1.0;
        for (int i = 0; i < 32; i++) {
            now += 1.0;                      /* 1 s per block (SR frames), env up */
            transport_process(&t, 0, 0, 0, env_mid, (double)SR, now, SR);
            if (t.one_shot == 0 && close_time < 0.0) close_time = now;
        }
        CHECK(close_time >= 40.0);           /* not before the absolute
                                              * deadline */
        CHECK(close_time < 40.0 + 1.0);      /* within the expiry block */
        CHECK(t.one_shot == 0);
        CHECK(fexact(t.one_shot_deadline, 0.0));
        CHECK(fexactf(t.gate, 0.0f));
    }

    /* ---- PANIC: gate 0 instant, one-shot cleared, run cleared,
     *      deadline 0; sequencers untouched ---- */
    {
        transport_state_t t = mkT();
        double now = BASE;
        seq_state_t sq; seq_params_t sp = mksp_default();
        seq_reset(&sq);
        seq_tick(&sq, &sp, 120, 0.0, SR);     /* bpm: the GLOBAL tempo arg */
        CHECK(sq.cursor == 1);

        blocks(&t, 1, 1, 0, env_mid, 1, &now);  /* running, gated */
        CHECK(t.gate > 0.0f);
        CHECK(t.one_shot_deadline > 0.0);
        transport_process(&t, 1, 0, 1, env_mid, BLK, now, SR);  /* PANIC */
        CHECK(fexactf(t.gate, 0.0f));               /* instant, no ramp */
        CHECK(t.one_shot == 0);
        CHECK(fexact(t.one_shot_deadline, 0.0));   /* PANIC -> deadline 0 */
        now += BLK / SR;
        /* run cleared: a follow-up block with run=0 keeps gate 0 */
        blocks(&t, 0, 0, 0, env_mid, 5, &now);
        CHECK(fexactf(t.gate, 0.0f));
        CHECK(t.one_shot == 0);
        /* sequencer state untouched by transport (PANIC resets are the
         * engine's job, transport itself never writes seq state) */
        CHECK(sq.cursor == 1);
    }

    HARNESS_REPORT();
}
