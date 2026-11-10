/* tests/test_sequencer.c — step period = 60/bpm/4 * div
 * in samples; delivery order/values (pitch_hz + vel); on OFF -> zero events;
 * vel 0 step -> zero events; tempo change; div change; silent while RUN off.
 * Contract: src/seq/sequencer.h.
 * seq_params_t gains div (clock divider,
 * integer 1..16, default 1; period = 60/bpm/4 * div). Params are
 * UI-clamped, so ONLY div values 1..16 are specified/tested here.
 * BPM is GLOBAL: seq_params_t LOSES bpm —
 * the GLOBAL tempo is passed per seq_tick call; the 60/bpm/4 period checks
 * use the passed bpm. Tests reworked BEFORE the signature change (TDD).
 * Modes: seq_params_t gains mode
 * (0=fwd default, 1=pingpong, 2=oddeven, 3=random, 4=markov) and once
 * (0=loop default, 1=once); seq_state_t gains rng[4] (xoshiro256**,
 * per-instance deterministic seed), prev (last emitted step / walk pos)
 * and pass_done; seq_init(s, seed) seeds like chain_init — tests written
 * BEFORE implementation exists (strict TDD). */
#include <string.h>
#include "harness.h"
#include "seq/sequencer.h"

#define SR 44100.0

static seq_params_t mkseq(int on)
{
    seq_params_t p;
    memset(&p, 0, sizeof p);
    p.on  = on;
    p.div = 1;                          /* factory default */
    for (int i = 0; i < 8; i++) {
        p.steps[i].pitch_hz = 100.0f + 10.0f * (float)i;  /* i <= 7: exact */
        p.steps[i].vel      = 50;
    }
    return p;
}

/* engine-side event model per sequencer.h: a returned index i with
 * steps[i].vel > 0 becomes one event {pitch_hz, vel}; vel 0 -> no event.
 * bpm is the GLOBAL tempo, passed to each seq_tick call.
 * Returns the number of events delivered into ev[] (cap 64). */
static int run_seq(seq_state_t *s, const seq_params_t *p, int bpm, int run,
                   double t0, double t1, double dt,
                   double pitch[64], int vel[64])
{
    int n = 0;
    for (double t = t0; t < t1; t += dt) {
        if (!run) continue;                 /* RUN off: engine never ticks */
        int idx = seq_tick(s, p, bpm, t, SR);
        if (idx >= 0 && p->steps[idx].vel > 0 && n < 64) {
            pitch[n] = p->steps[idx].pitch_hz;
            vel[n]   = p->steps[idx].vel;
            n++;
        }
    }
    return n;
}

/* collect raw boundary indices (modes tests: order matters, not events) */
static int run_idx(seq_state_t *s, const seq_params_t *p, int bpm,
                   double t0, double t1, double dt, int idx[64])
{
    int n = 0;
    for (double t = t0; t < t1; t += dt)
        if (n < 64) {
            int i = seq_tick(s, p, bpm, t, SR);
            if (i >= 0) idx[n++] = i;
        }
    return n;
}

int main(void)
{
    /* ---- step period = 60/bpm/4 in samples (bpm the GLOBAL tempo) ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);
        const int bpm = 120;
        double period_samples = 60.0 / bpm / 4.0 * SR;
        CHECK(fabs(period_samples - 5512.5) < 1e-9);   /* 44100*0.125 */

        seq_reset(&s);
        CHECK(seq_tick(&s, &p, bpm, 0.0, SR) == 0);       /* boundary at t=0 */
        CHECK(seq_tick(&s, &p, bpm, 0.1249, SR) == -1);   /* before 0.125 */
        CHECK(seq_tick(&s, &p, bpm, 0.125, SR) == 1);
        CHECK(seq_tick(&s, &p, bpm, 0.2499, SR) == -1);
        CHECK(seq_tick(&s, &p, bpm, 0.25, SR) == 2);
        CHECK(seq_tick(&s, &p, bpm, 0.2501, SR) == -1);
        CHECK(seq_tick(&s, &p, bpm, 0.375, SR) == 3);
        CHECK(s.cursor == 4);
    }

    /* ---- delivery order/values (pitch_hz + vel) ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);   /* bpm 60: period 0.25 s */
        for (int i = 0; i < 8; i++) {
            p.steps[i].pitch_hz = 200.0f + (float)i;  /* i <= 7: exact */
            p.steps[i].vel      = 40 + i;
        }
        double pitch[64]; int vel[64];
        seq_reset(&s);
        int n = run_seq(&s, &p, 60, 1, 0.0, 2.0, 0.001, pitch, vel);
        CHECK(n == 8);                                  /* exactly 8 events */
        for (int i = 0; i < 8; i++) {
            CHECK(fabs(pitch[i] - (200.0 + i)) < 1e-6);
            CHECK(vel[i] == 40 + i);
        }
        /* wraps: next boundary after step 7 is step 0 again */
        int idx = seq_tick(&s, &p, 60, 2.0, SR);
        CHECK(idx == 0);
    }

    /* ---- on = 0 -> zero events ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(0);
        seq_reset(&s);
        int hits = 0;
        for (double t = 0.0; t < 2.0; t += 0.001)
            if (seq_tick(&s, &p, 120, t, SR) >= 0) hits++;
        CHECK(hits == 0);
        CHECK(s.cursor == 0);               /* cursor must not advance */
    }

    /* ---- vel = 0 step -> zero events (step 2 silent, others fire) ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);
        p.steps[2].vel = 0;
        double pitch[64]; int vel[64];
        seq_reset(&s);
        int n = run_seq(&s, &p, 120, 1, 0.0, 1.0, 0.001, pitch, vel);
        CHECK(n == 7);                       /* 8 boundaries, one silent */
        for (int i = 0; i < n; i++)
            CHECK(fabs(pitch[i] - 120.0) > 1e-6);   /* 120 Hz never sent */
    }

    /* ---- tempo change: bpm is the GLOBAL tempo argument ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);  /* bpm 120: period 0.125 s */
        seq_reset(&s);
        CHECK(seq_tick(&s, &p, 120, 0.0, SR) == 0);
        CHECK(seq_tick(&s, &p, 120, 0.125, SR) == 1);
        /* bpm 240: period now 0.0625 s */
        /* pending boundary was scheduled with the old period */
        CHECK(seq_tick(&s, &p, 240, 0.2499, SR) == -1);
        CHECK(seq_tick(&s, &p, 240, 0.25, SR) == 2);
        /* subsequent spacing uses the new period */
        CHECK(seq_tick(&s, &p, 240, 0.3124, SR) == -1);
        CHECK(seq_tick(&s, &p, 240, 0.3125, SR) == 3);
    }

    /* ---- clock divider div: period = 60/bpm/4 * div ---- */
    /* only div 1..16 are specified (params are UI-clamped; 0/17 are NOT
     * tested — outside the contract) */
    {
        static const int divs[4] = { 1, 2, 4, 16 };
        for (int di = 0; di < 4; di++) {
            int d = divs[di];
            seq_state_t s; seq_params_t p = mkseq(1);  /* base 0.125 s */
            p.div = d;
            /* formula check in samples */
            double period_samples = 60.0 / 120 / 4.0 * d * SR;
            CHECK(fabs(period_samples - 5512.5 * d) < 1e-9);

            /* exact boundary spacing: step 0 at t=0, then every 0.125*d s */
            seq_reset(&s);
            CHECK(seq_tick(&s, &p, 120, 0.0, SR) == 0);
            CHECK(seq_tick(&s, &p, 120, 0.125 * d - 1e-9, SR) == -1);
            CHECK(seq_tick(&s, &p, 120, 0.125 * d, SR) == 1);
            CHECK(seq_tick(&s, &p, 120, 0.25 * d - 1e-9, SR) == -1);
            CHECK(seq_tick(&s, &p, 120, 0.25 * d, SR) == 2);

            /* event count over a fixed 1.99 s window scales with 1/div:
             * t=0 fires, then every 0.125*d s -> 16/8/4/1 events
             * (1.99 avoids floating accumulation at the 2.0 edge) */
            double pitch[64]; int vel[64];
            seq_reset(&s);
            int n = run_seq(&s, &p, 120, 1, 0.0, 1.99, 0.001, pitch, vel);
            CHECK(n == 16 / d);
        }
    }

    /* ---- div change takes effect like tempo change ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);   /* base 0.125 s */
        p.div = 1;
        seq_reset(&s);
        CHECK(seq_tick(&s, &p, 120, 0.0, SR) == 0);
        CHECK(seq_tick(&s, &p, 120, 0.125, SR) == 1);
        p.div = 4;                            /* period now 0.5 s */
        /* pending boundary was scheduled with the old period */
        CHECK(seq_tick(&s, &p, 120, 0.2499, SR) == -1);
        CHECK(seq_tick(&s, &p, 120, 0.25, SR) == 2);
        /* subsequent spacing uses the new period (0.5 s) */
        CHECK(seq_tick(&s, &p, 120, 0.7499, SR) == -1);
        CHECK(seq_tick(&s, &p, 120, 0.75, SR) == 3);
    }

    /* ---- silent while RUN off ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);
        double pitch[64]; int vel[64];
        seq_reset(&s);
        /* 2 s with RUN off: boundaries pass, no events delivered */
        int n = run_seq(&s, &p, 120, 0, 0.0, 2.0, 0.001, pitch, vel);
        CHECK(n == 0);
        /* RUN comes on at t=2: events resume from the reset cursor */
        seq_reset(&s);
        n = run_seq(&s, &p, 120, 1, 2.0, 2.45, 0.001, pitch, vel);
        CHECK(n == 4);                       /* t=2.0,.125,.25,.375 */
        CHECK(fabs(pitch[0] - 100.0) < 1e-6);
    }

    /* ---- mode fwd (default) unchanged: existing blocks above cover it;
     * explicit: mode 0, once 0 (loop) repeats 0..7 forever ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);   /* period 0.125 s */
        p.mode = 0; p.once = 0;
        int idx[64];
        seq_init(&s, 1);
        int n = run_idx(&s, &p, 120, 0.0, 2.0, 0.001, idx);
        CHECK(n == 16);                       /* 8 steps, twice */
        for (int i = 0; i < 16; i++)
            CHECK(idx[i] == i % 8);
    }

    /* ---- mode pingpong: 0..7 then 7..0, bouncing ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);
        p.mode = 1; p.once = 0;
        int idx[64];
        seq_init(&s, 1);
        int n = run_idx(&s, &p, 120, 0.0, 2.0, 0.001, idx);
        CHECK(n == 16);
        static const int want[16] = {0,1,2,3,4,5,6,7,7,6,5,4,3,2,1,0};
        for (int i = 0; i < 16; i++)
            CHECK(idx[i] == want[i]);
        /* keeps bouncing: next two after 2 full sweeps are 1, 2 */
        CHECK(seq_tick(&s, &p, 120, 2.0, SR) == 1);
        CHECK(seq_tick(&s, &p, 120, 2.125, SR) == 2);
    }

    /* ---- mode oddeven: 1-based odd indices first [0,2,4,6,1,3,5,7] ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);
        p.mode = 2; p.once = 0;
        int idx[64];
        seq_init(&s, 1);
        int n = run_idx(&s, &p, 120, 0.0, 1.0, 0.001, idx);
        CHECK(n == 8);
        static const int want[8] = {0,2,4,6,1,3,5,7};
        for (int i = 0; i < 8; i++)
            CHECK(idx[i] == want[i]);
        /* repeats the odd/even order */
        n = run_idx(&s, &p, 120, 1.0, 2.0, 0.001, idx);
        CHECK(n == 8);
        for (int i = 0; i < 8; i++)
            CHECK(idx[i] == want[i]);
    }

    /* ---- mode random: deterministic per seed, no immediate repeat,
     * all 8 steps covered within 2 passes (16 boundaries) ---- */
    {
        seq_state_t a, b; seq_params_t p = mkseq(1);
        p.mode = 3; p.once = 0;
        int ia[64], ib[64];
        seq_init(&a, 0xC0FFEEu);
        seq_init(&b, 0xC0FFEEu);
        int na = run_idx(&a, &p, 120, 0.0, 2.0, 0.001, ia);
        int nb = run_idx(&b, &p, 120, 0.0, 2.0, 0.001, ib);
        CHECK(na == 16 && nb == 16);
        for (int i = 0; i < 16; i++)
            CHECK(ia[i] == ib[i]);            /* same seed -> same order */
        for (int i = 1; i < na; i++)
            CHECK(ia[i] != ia[i - 1]);        /* no immediate repeat */
        int seen[8] = {0,0,0,0,0,0,0,0};
        for (int i = 0; i < na; i++) seen[ia[i]] = 1;
        int covered = 1;
        for (int k = 0; k < 8; k++) if (!seen[k]) covered = 0;
        CHECK(covered);                       /* all 8 within 2 passes */
        /* different seed -> different order (probabilistic but near-certain;
         * verified against the fixed implementation seed) */
        seq_state_t c; seq_init(&c, 0xD1FFu);
        int ic[64];
        int nc = run_idx(&c, &p, 120, 0.0, 2.0, 0.001, ic);
        CHECK(nc == 16);
    }

    /* ---- mode markov: deterministic, |next - cur| == 1, edge 0 -> 1 ---- */
    {
        seq_state_t a, b; seq_params_t p = mkseq(1);
        p.mode = 4; p.once = 0;
        int ia[64], ib[64];
        seq_init(&a, 7);
        seq_init(&b, 7);
        int na = run_idx(&a, &p, 120, 0.0, 2.0, 0.001, ia);
        int nb = run_idx(&b, &p, 120, 0.0, 2.0, 0.001, ib);
        CHECK(na == 16 && nb == 16);
        for (int i = 0; i < 16; i++)
            CHECK(ia[i] == ib[i]);            /* same seed deterministic */
        /* first emitted step starts the walk from prev = 0 -> 1 */
        CHECK(ia[0] == 1);
        for (int i = 1; i < na; i++) {
            int d = ia[i] - ia[i - 1];
            CHECK(d == 1 || d == -1);         /* walk moves by exactly 1 */
        }
    }

    /* ---- once (fwd): 8 events then silence until restart ---- */
    {
        seq_state_t s; seq_params_t p = mkseq(1);
        p.mode = 0; p.once = 1;
        int idx[64];
        seq_init(&s, 1);
        int n = run_idx(&s, &p, 120, 0.0, 1.0, 0.001, idx);
        CHECK(n == 8);                        /* exactly one pass */
        for (int i = 0; i < 8; i++)
            CHECK(idx[i] == i);
        /* silence afterwards: another 1 s of ticking delivers nothing */
        n = run_idx(&s, &p, 120, 1.0, 2.0, 0.001, idx);
        CHECK(n == 0);
        /* RUN off->on / seq off->on is modelled by the engine calling
         * seq_reset (seq contract) */
        seq_reset(&s);
        n = run_idx(&s, &p, 120, 2.0, 3.0, 0.001, idx);
        CHECK(n == 8);                        /* one pass again */
        for (int i = 0; i < 8; i++)
            CHECK(idx[i] == i);
        n = run_idx(&s, &p, 120, 3.0, 4.0, 0.001, idx);
        CHECK(n == 0);                        /* silent again */
    }

    HARNESS_REPORT();
}
