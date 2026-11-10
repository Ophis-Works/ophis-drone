/* src/seq/sequencer.c — sample-clock driven sequencer.
 * Pure: sample time in, step index out. No timers,
 * no SDL/ALSA. Ticked by the engine only while RUN is ON.
 * Step ORDER per p->mode
 * (fwd/pingpong/oddeven/random/markov) and the ONCE option (loop/once).
 * random/markov use a per-instance xoshiro256** PRNG seeded via
 * splitmix64 — the shared dsp/prng.{h,c} module, the
 * same generator osc.c uses (no rand(), deterministic).
 * BPM is GLOBAL: no per-seq bpm — the
 * global tempo arrives as the `bpm` argument of seq_tick. */
#include "seq/sequencer.h"

#include "dsp/prng.h"

/* uniform step index 0..7 */
static int rng_step(seq_state_t *s)
{
    return (int)((prng_next(s->rng) >> 32) & 7);   /* bits 32..34 */
}

void seq_init(seq_state_t *s, uint64_t seed)
{
    prng_seed(s->rng, seed);
    seq_reset(s);
}

void seq_reset(seq_state_t *s)
{
    s->cursor         = 0;
    s->dir            = 1;
    s->prev           = 0;      /* markov walk starts at step 0 */
    s->pass_count     = 0;
    s->next_step_time = 0.0;   /* first boundary fires at now >= 0 */
}

/* next step index per mode; updates the order state */
static int next_index(seq_state_t *s, int mode)
{
    static const int ODDEVEN[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };
    int idx;
    switch (mode) {
    case 1:                                     /* pingpong */
        /* 0..7 then 7..0, bouncing: the turn emits the edge
         * step 7 again, and the bottom turn resumes at 1 */
        idx = s->cursor;
        s->cursor += s->dir;
        if (s->cursor >= 8) { s->cursor = 7; s->dir = -1; }
        else if (s->cursor < 0) { s->cursor = 1; s->dir = 1; }
        break;
    case 2:                                     /* oddeven */
        idx = ODDEVEN[s->cursor];
        s->cursor = (s->cursor + 1) % 8;
        break;
    case 3:                                     /* random: no repeat */
        do { idx = rng_step(s); } while (idx == s->prev);
        s->prev = idx;
        break;
    case 4: {                                   /* markov +-1 walk */
        int next = s->prev + ((prng_next(s->rng) >> 63) ? 1 : -1);
        if (next < 0) next = 1;                 /* reflect at edges */
        if (next > 7) next = 6;
        s->prev = next;
        idx = next;
        break;
    }
    default:                                    /* fwd (0) */
        idx = s->cursor;
        s->cursor = (s->cursor + 1) % 8;
        break;
    }
    return idx;
}

int seq_tick(seq_state_t *s, const seq_params_t *p, int bpm, double now,
             double sr)
{
    (void)sr;                  /* times are in seconds; sr unused here */
    if (!p->on)
        return -1;             /* sequencer disabled -> no events */

    double period = 60.0 / (double)bpm / 4.0 * (double)p->div;
    /* bpm: the GLOBAL tempo argument;
     * div = 1 -> 16th notes; a div change takes effect like a tempo
     * change (pending boundary keeps its scheduled period) */

    if (now >= s->next_step_time) {
        /* once mode: after one pass (8 emitted steps) the
         * sequencer is silent until restarted (seq_reset by the engine on
         * RUN off->on, seq off->on, or PANIC). Time is left where it is —
         * after a restart the first boundary fires immediately. */
        if (p->once && s->pass_count >= 8)
            return -1;
        int idx = next_index(s, p->mode);
        s->pass_count++;
        s->next_step_time += period;   /* pending boundary keeps the period
                                        * it was scheduled with (tempo
                                        * change takes effect afterwards) */
        /* late start (first tick well after reset): don't drain the whole
         * pattern in a burst — resync to now */
        if (s->next_step_time <= now)
            s->next_step_time = now + period;
        return idx;
    }
    return -1;
}
