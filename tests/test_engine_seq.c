/* tests/test_engine_seq.c — engine-level sequencer application (PLAN §6
 * amendment): a fired step's pitch PERSISTS — the chain keeps playing it
 * until the next step event; vel scales the sustained level; sequencer
 * OFF returns the chain to the oscillator's frequency parameter.
 * Pure: engine + DSP, no ALSA (alsa_pcm.c is not linked). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"

#include "../src/audio/engine.h"
#include "../src/preset/defaults.h"
#include "test_render_helper.h"

#define SR 44100.0
#define BLK 256
#define NB2C 44100            /* 1 s */
static double bbuf_on[NB2C], bbuf_off[NB2C];
static double last_b_raw_on = 0.0;

static engine_t e;

static void setup(int step_vel)
{
    engine_init(&e, SR);
    e.p = OPHIS_DEFAULTS;
    e.p.run = 1;
    e.p.bpm = 60;                       /* GLOBAL tempo (PLAN §6) */
    for (int c = 0; c < 3; c++)
        e.p.chain[c].level = (c == 0) ? 100 : 0;   /* mute B and C */
    e.p.chain[0].wave = WAVE_SINE;
    e.p.chain[0].adsr.a = 0.005;       /* fast attack for clean windows */
    e.p.chain[0].adsr.d = 0.01;
    e.p.chain[0].adsr.s = 100;
    e.p.seq[0].on = 1;
    e.p.seq[0].div = 1;                /* step period = 60/60/4 = 0.25 s */
    for (int s = 0; s < 8; s++) {
        e.p.seq[0].steps[s].pitch_hz = (s % 2 == 0) ? 100.0f : 200.0f;
        e.p.seq[0].steps[s].vel = step_vel;
    }
}

static void render_mono(float *mono, int n)
{
    static float stereo[2 * 60000];
    for (int b = 0; b * BLK < n; b++)
        engine_render_block(&e, stereo + 2 * BLK * b, BLK, 0, 0, 0);
    for (int i = 0; i < n; i++)
        mono[i] = stereo[2 * i];
}

static double freq_of(const float *m, int i0, int i1)
{
    int cross = 0;
    for (int i = i0 + 1; i < i1; i++)
        if ((m[i - 1] < 0.0f && m[i] >= 0.0f) ||
            (m[i - 1] >= 0.0f && m[i] < 0.0f))
            cross++;
    return (double)cross / 2.0 / ((double)(i1 - i0) / SR);
}

static double rms_of(const float *m, int i0, int i1)
{
    double acc = 0.0;
    for (int i = i0; i < i1; i++)
        acc += (double)m[i] * (double)m[i];
    return sqrt(acc / (double)(i1 - i0));
}

int main(void)
{
    static float mono[60000];
    int total = (int)(0.8 * SR);      /* covers window C [0.56, 0.71] */

    /* step window semantics (owner order): EVERY step's pitch applies
     * DURING ITS OWN WINDOW — including vel-0 steps (no retrigger, the
     * envelope sustains, but the pitch follows the step). Step 0 fires
     * at t=0 (100 Hz, vel 100), step 1 at t=0.25 (200 Hz, vel 0 — NO
     * retrigger, pitch must STILL be 200), step 2 at t=0.50 (300 Hz,
     * vel 100). */
    setup(100);
    e.p.seq[0].steps[0].pitch_hz = 100.0f;
    e.p.seq[0].steps[1].pitch_hz = 200.0f;
    e.p.seq[0].steps[1].vel = 0;      /* silent step: pitch still applies */
    e.p.seq[0].steps[2].pitch_hz = 300.0f;
    render_mono(mono, total);
    double fa = freq_of(mono, (int)(0.06 * SR), (int)(0.21 * SR));
    double fb = freq_of(mono, (int)(0.31 * SR), (int)(0.46 * SR));
    double fc = freq_of(mono, (int)(0.56 * SR), (int)(0.71 * SR));
    printf("  freq A=%.1f Hz  B=%.1f Hz  C=%.1f Hz\n", fa, fb, fc);
    CHECK(fa > 85.0 && fa < 115.0);           /* step 0 window: 100 Hz */
    CHECK(fb > 170.0 && fb < 230.0);           /* step 1 window (vel 0): 200 */
    CHECK(fc > 255.0 && fc < 345.0);           /* step 2 window: 300 Hz */
    double rms100 = rms_of(mono, (int)(0.31 * SR), (int)(0.46 * SR));

    /* vel 40 on every step -> sustained level ~0.4x */
    setup(40);
    render_mono(mono, total);
    double rms40 = rms_of(mono, (int)(0.31 * SR), (int)(0.46 * SR));
    double ratio = rms40 / rms100;
    printf("  rms ratio=%.2f\n", ratio);
    CHECK(ratio > 0.30 && ratio < 0.55);      /* step vel scales level */

    /* seq OFF -> direct mode: freq returns to the osc param (55 Hz) */
    e.p.seq[0].on = 0;
    render_mono(mono, (int)(0.35 * SR));
    double fd = freq_of(mono, (int)(0.15 * SR), (int)(0.30 * SR));
    printf("  direct freq=%.1f Hz\n", fd);
    CHECK(fd > 45.0 && fd < 65.0);             /* back at the osc param */

    /* S3 (Phase 1, PLAN_CODE_REVIEW_FIXES.md): after a seq off->on
     * restart the FIRST rendered pitch is step 0's — the stale
     * pre-restart step window (cur_step/cur_valid) must not survive
     * seq_reset(). Play into step 1's window (200 Hz), toggle the seq
     * off and back on, then measure the post-restart window. */
    setup(100);
    e.p.seq[0].steps[0].pitch_hz = 100.0f;
    for (int s = 1; s < 8; s++)
        e.p.seq[0].steps[s].pitch_hz = 200.0f;
    render_mono(mono, (int)(0.35 * SR));       /* inside step-1 window */
    e.p.seq[0].on = 0;
    render_mono(mono, (int)(0.10 * SR));       /* direct-mode window */
    e.p.seq[0].on = 1;                         /* RESTART */
    render_mono(mono, (int)(0.25 * SR));       /* first step-0 window */
    double fr = freq_of(mono, (int)(0.03 * SR), (int)(0.22 * SR));
    printf("  restart freq=%.1f Hz\n", fr);
    CHECK(fr > 85.0 && fr < 115.0);            /* step 0 (100 Hz), not 200 */

    /* B-SEQ1 (owner order 2026-11-05): the LIVE step the UI is fed is
     * mode-INDEPENDENT — the engine's step window (cur_step/cur_valid),
     * never the sequencer's ORDER cursor (fwd/pingpong/oddeven only;
     * random/markov emit from prev and leave cursor at 0 — the old
     * status froze the marker at step 0 in those modes). Pins, per
     * PLAN_SEQ_LIVE_MARKER.md: fwd matches the predicted order; random
     * and markov CHANGE over time and equal the sequencer's own
     * emitted-step state (sq.prev) after the first emission — internals
     * readable here in the test, never in UI code; seq off -> -1;
     * PANIC -> -1 (panic_reset clears cur_valid). */
    {
        int win[256], prv[256];    /* live window + emitted-step state,
                                    * one entry per block (fixed size) */
        const int nblk = 200;                  /* 200*256/44100 ≈ 1.16 s */
        CHECK(nblk <= (int)(sizeof win / sizeof win[0]));
        static float sink[2 * BLK];
        int tmp[3];
        for (int mode = 0; mode <= 4; mode++) {
            setup(100);
            e.p.seq[0].mode = mode;
            /* div = 1: 0.25 s windows at 60 bpm = 43.07 blocks each */
            for (int b = 0; b < nblk; b++) {
                engine_render_block(&e, sink, BLK, 0, 0, 0);
                engine_seq_steps(&e, tmp);
                win[b] = tmp[0];               /* chain A (B/C seqs off) */
                prv[b] = e.sq[0].prev;         /* PER-BLOCK snapshot —
                                                * after the loop prev is
                                                * the LAST emission only */
            }
            /* the window only ever holds a valid step or "nothing" */
            for (int b = 0; b < nblk; b++)
                CHECK(win[b] >= -1 && win[b] <= 7);
            /* first emission at t=0 (boundary now >= 0): from the first
             * block on, something plays */
            CHECK(win[0] >= 0);
            if (mode == 0) {
                /* fwd: predicted order 0,1,2,... — one window per
                 * 43.07 blocks; block b is inside window floor(b/43.07) */
                int bpw = (int)(0.25 * SR / BLK);       /* 43 */
                CHECK(win[10] == 0);
                CHECK(win[bpw + 5] == 1);
                CHECK(win[2 * bpw + 5] == 2);
            } else if (mode == 3 || mode == 4) {
                /* random/markov: NOT frozen at step 0 (B-SEQ1
                 * regression pin) and every window equals the
                 * sequencer's own emitted-step state (sq.prev) —
                 * internals readable HERE, never in UI code */
                int distinct = 0;
                for (int b = 1; b < nblk; b++)
                    if (win[b] != win[0]) { distinct = 1; break; }
                CHECK(distinct == 1);
                for (int b = 0; b < nblk; b++)
                    CHECK(win[b] == prv[b]);
            }
            /* modes 1/2: their ORDER is test_sequencer's contract; here
             * the range pin above plus win[0] >= 0 suffice — prev is
             * only the emitted-step state of modes 3/4, so the equality
             * pin does not apply to them */
        }
        /* seq OFF clears the window: -1, not a stale step; chains B and
         * C have their sequencers OFF (defaults) -> -1 as well */
        e.p.seq[0].on = 0;
        engine_render_block(&e, sink, BLK, 0, 0, 0);
        int off[3];
        engine_seq_steps(&e, off);
        CHECK(off[0] == -1 && off[1] == -1 && off[2] == -1);
        /* PANIC clears the window too */
        e.p.seq[0].on = 1;
        e.p.run = 1;
        engine_render_block(&e, sink, BLK, 0, 0, 0);
        int live[3];
        engine_seq_steps(&e, live);
        CHECK(live[0] >= 0);
        engine_render_block(&e, sink, BLK, 0, 1, 0);   /* panic_req */
        engine_seq_steps(&e, live);
        CHECK(live[0] == -1);
    }

    /* Item 1 (owner report 2026-11-06, PLAN_MOD_REVAMP_SEQ_MARKER_
     * RETRIG.md): the live step must be PER-CHAIN — all three
     * sequencers run at once with distinct modes, and EACH chain's
     * engine_seq_steps must move and match its own sq[c] state. The
     * B-SEQ1 pins drove chain A only; chains B/C were never pinned
     * (the device report: the marker works for OSC 1 only). */
    {
        engine_init(&e, SR);
        e.p = OPHIS_DEFAULTS;
        e.p.run = 1;
        e.p.bpm = 60;
        for (int c = 0; c < 3; c++) {
            e.p.seq[c].on = 1;
            e.p.seq[c].div = 1;
            e.p.seq[c].mode = (c == 1) ? 4 : 3;   /* prev-tracked modes */
            for (int s = 0; s < 8; s++)
                e.p.seq[c].steps[s].pitch_hz = 110.0f * (float)(s + 1);
        }
        const int nblk3 = 200;                 /* ≈ 1.16 s, 4+ windows */
        static float sink[2 * BLK];
        int tmp[3];
        int win[3][256], prv[3][256];
        for (int b = 0; b < nblk3; b++) {
            engine_render_block(&e, sink, BLK, 0, 0, 0);
            engine_seq_steps(&e, tmp);
            for (int c = 0; c < 3; c++) {
                win[c][b] = tmp[c];
                prv[c][b] = e.sq[c].prev;
            }
        }
        for (int c = 0; c < 3; c++) {
            int distinct = 0;
            for (int b = 1; b < nblk3; b++)
                if (win[c][b] != win[c][0]) { distinct = 1; break; }
            CHECK(distinct == 1);              /* each marker moves */
            for (int b = 0; b < nblk3; b++)
                CHECK(win[c][b] == prv[c][b]); /* per-chain window truth */
        }
    }

    /* Item 3 (owner order 2026-11-06, PLAN_MOD_REVAMP_SEQ_MARKER_
     * RETRIG.md): the value-key retrig is PER-CHAIN — retrig_mask bit c
     * retrigs chain c at its own vel; the other chains' envelopes stay
     * at 0 (the old val_edit fired trigger_req = ALL chains). */
    {
        engine_init(&e, SR);
        e.p = OPHIS_DEFAULTS;
        e.p.run = 0;                           /* transport gate closed */
        for (int c = 0; c < 3; c++) {
            e.p.chain[c].level = 100;
            e.p.chain[c].vel = 100;
            e.p.chain[c].adsr.a = 0.05f;
            e.p.chain[c].adsr.s = 100;
        }
        static float sink2[2 * BLK];
        for (int b = 0; b < 10; b++)
            engine_render_block(&e, sink2, BLK, 0, 0, 1 << 1); /* chain B */
        CHECK(e.ch[1].env.level > 0.0f);       /* B retrigs, rises */
        CHECK(e.ch[0].env.level == 0.0f);      /* A untouched */
        CHECK(e.ch[2].env.level == 0.0f);      /* C untouched */
    }

    /* ---- P2 (PLAN_MOD_ROUTING.md M1): TARGET routing end-to-end —
     * slot SRC B.osc TARGET C TYPE FM AMT 100 gives C FM sidebands at
     * 200±110k; TARGET A gives none (A's level is 0: the modulation
     * dies with the target's own post-LEVEL output). Probe 310 =
     * 200+110 — tanh is odd-symmetric, so B/C intermod hits only
     * odd-order products (290/330/420/510...), never 310. ---- */
    {
        static double mbuf[2][NB2C];
        for (int variant = 0; variant < 2; variant++) {
            engine_init(&e, SR);
            e.p = OPHIS_DEFAULTS;
            e.p.run = 0;
            e.p.chain[0].level = 0;              /* A silent at master */
            e.p.chain[0].freq_hz = 700.0f;       /* distinct carrier */
            e.p.chain[1].freq_hz = 110.0f;       /* mod source */
            e.p.chain[2].freq_hz = 200.0f;       /* FM target */
            for (int c = 0; c < 3; c++) {
                e.p.chain[c].wave = WAVE_SINE;
                e.p.chain[c].vel = 100;
                e.p.chain[c].adsr.a = 0.001f;
                e.p.chain[c].adsr.d = 0.001f;
                e.p.chain[c].adsr.s = 100;
                e.p.chain[c].adsr.r = 0.001f;
            }
            e.p.chain[1].level = 100;
            e.p.chain[2].level = 100;
            e.p.xmod[0].src = 1;                 /* B.osc */
            e.p.xmod[0].dest = MOD_FM;
            e.p.xmod[0].amount = 100.0f;
            e.p.xmod[0].tgt = (variant == 0) ? 2 : 0;
            rh_render_mono(&e, mbuf[variant], NB2C);
        }
        double g310_on = h_goertzel_mag(mbuf[0] + 5000, NB2C - 5000,
                                        310.0, SR);
        double g310_off = h_goertzel_mag(mbuf[1] + 5000, NB2C - 5000,
                                         310.0, SR);
        printf("  g310 tgt=C=%.5f tgt=A=%.5f\n", g310_on, g310_off);
        CHECK(g310_on > 5.0 * g310_off);
    }

    /* ---- P3 (PLAN_MOD_ROUTING.md M2): SRC extension — slot SRC
     * A.osc TARGET B TYPE RING AMT 100: B's output carries A×B
     * sidebands at 110±200 (90/310); A unaffected. Probe 310 =
     * 200+110 (odd-order tanh intermod hits 290/330/420, never 310).
     * Variant 0: slot on; variant 1: dest OFF mix 0 -> no sideband. */
    {
        static double mbuf[2][NB2C];
        for (int variant = 0; variant < 2; variant++) {
            engine_init(&e, SR);
            e.p = OPHIS_DEFAULTS;
            e.p.run = 0;
            e.p.chain[0].freq_hz = 110.0f;       /* ring source */
            e.p.chain[1].freq_hz = 200.0f;       /* ring target */
            for (int c = 0; c < 3; c++) {
                e.p.chain[c].wave = WAVE_SINE;
                e.p.chain[c].vel = 100;
                e.p.chain[c].adsr.a = 0.001f;
                e.p.chain[c].adsr.d = 0.001f;
                e.p.chain[c].adsr.s = 100;
                e.p.chain[c].adsr.r = 0.001f;
            }
            e.p.chain[0].level = 100;
            e.p.chain[1].level = 100;
            e.p.chain[2].level = 0;
            e.p.xmod[0].src = 7;                 /* A.osc */
            e.p.xmod[0].dest = (variant == 0) ? MOD_RING : MOD_OFF;
            e.p.xmod[0].amount = 100.0f;
            e.p.xmod[0].tgt = 1;                 /* B */
            rh_render_mono(&e, mbuf[variant], NB2C);
        }
        double g310_on = h_goertzel_mag(mbuf[0] + 5000, NB2C - 5000,
                                        310.0, SR);
        double g310_off = h_goertzel_mag(mbuf[1] + 5000, NB2C - 5000,
                                         310.0, SR);
        printf("  g310 ring-on=%.5f off=%.5f\n", g310_on, g310_off);
        CHECK(g310_on > 5.0 * g310_off);
    }

    /* ---- P4 (PLAN_MOD_ROUTING.md M3): MIX VOLUME — mixvol B = 0
     * makes B inaudible at master, but a slot SRC B.osc still drives
     * A's PITCH (vibrato) at full depth. A 200 Hz carrier vibrato
     * ±50 Hz at B's 110 Hz rate -> sidebands at 90/310 (J1(0.45)≈0.22).
     * Variant 0: slot on; variant 1: dest OFF. 110 Hz itself is clean
     * of A's products (200±110k = 90/310/420...), so g110 ≈ 0 proves
     * B is silent. ---- */
    {
        static double mbuf[2][NB2C];
        double g110 = 0.0;
        for (int variant = 0; variant < 2; variant++) {
            engine_init(&e, SR);
            e.p = OPHIS_DEFAULTS;
            e.p.run = 0;
            e.p.chain[0].freq_hz = 200.0f;       /* vibrato carrier */
            e.p.chain[1].freq_hz = 110.0f;       /* mod source, muted */
            for (int c = 0; c < 3; c++) {
                e.p.chain[c].wave = WAVE_SINE;
                e.p.chain[c].vel = 100;
                e.p.chain[c].adsr.a = 0.001f;
                e.p.chain[c].adsr.d = 0.001f;
                e.p.chain[c].adsr.s = 100;
                e.p.chain[c].adsr.r = 0.001f;
            }
            e.p.chain[0].level = 100;
            e.p.chain[1].level = 100;
            e.p.chain[2].level = 0;
            e.p.chain[1].mixvol = 0;             /* B: no master send */
            e.p.xmod[0].src = 1;                 /* B.osc */
            e.p.xmod[0].dest = (variant == 0) ? MOD_PITCH : MOD_OFF;
            e.p.xmod[0].amount = 50.0f;
            e.p.xmod[0].tgt = 0;                 /* A */
            rh_render_mono(&e, mbuf[variant], NB2C);
            if (variant == 0)
                g110 = h_goertzel_mag(mbuf[0] + 5000, NB2C - 5000,
                                      110.0, SR);
        }
        double g310_on = h_goertzel_mag(mbuf[0] + 5000, NB2C - 5000,
                                        310.0, SR);
        double g310_off = h_goertzel_mag(mbuf[1] + 5000, NB2C - 5000,
                                         310.0, SR);
        printf("  g110=%.5f g310 on=%.5f off=%.5f\n",
               g110, g310_on, g310_off);
        CHECK(g110 < 0.02);                      /* B silent at master */
        CHECK(g310_on > 5.0 * g310_off);         /* vibrato at full depth */
    }

    /* ---- SP3 (PLAN_OPTIMIZATION.md O2): a sub's LEVEL never scales
     * what the matrix taps — B's sub0 is INAUDIBLE (level 0) but a slot
     * SRC B.SUB1 RING into A at amount 100 still produces the A×sub
     * sidebands at 200±110 (probe 310 = 200+110; tanh odd-symmetry
     * keeps B/C intermod off 310). Variant 1: slot OFF. ---- */
    {
        static double mbuf[2][NB2C];
        for (int variant = 0; variant < 2; variant++) {
            engine_init(&e, SR);
            e.p = OPHIS_DEFAULTS;
            e.p.run = 0;
            e.p.chain[0].freq_hz = 200.0f;       /* ring target */
            e.p.chain[1].freq_hz = 110.0f;       /* sub's parent */
            for (int c = 0; c < 3; c++) {
                e.p.chain[c].wave = WAVE_SINE;
                e.p.chain[c].vel = 100;
                e.p.chain[c].adsr.a = 0.001f;
                e.p.chain[c].adsr.d = 0.001f;
                e.p.chain[c].adsr.s = 100;
                e.p.chain[c].adsr.r = 0.001f;
            }
            e.p.chain[0].level = 100;
            e.p.chain[1].level = 100;
            e.p.chain[2].level = 0;
            /* B's sub0: div 1 -> 110 Hz, INAUDIBLE */
            e.p.chain[1].sub[0].div = 1;
            e.p.chain[1].sub[0].wave = WAVE_SINE;
            e.p.chain[1].sub[0].level = 0;
            e.p.xmod[0].src = 2;                 /* B.sub1 */
            e.p.xmod[0].dest = (variant == 0) ? MOD_RING : MOD_OFF;
            e.p.xmod[0].amount = 100.0f;
            e.p.xmod[0].tgt = 0;                 /* A */
            rh_render_mono(&e, mbuf[variant], NB2C);
        }
        double g310_on = h_goertzel_mag(mbuf[0] + 5000, NB2C - 5000,
                                        310.0, SR);
        double g310_off = h_goertzel_mag(mbuf[1] + 5000, NB2C - 5000,
                                         310.0, SR);
        printf("  g310 subring on=%.5f off=%.5f\n", g310_on, g310_off);
        CHECK(g310_on > 5.0 * g310_off);
    }

    HARNESS_REPORT();
}
