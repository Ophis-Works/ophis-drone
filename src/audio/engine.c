/* src/audio/engine.c — audio-thread engine block loop, per IMPLEMENTATION
 * .md §4 pseudocode: events and gate are block-level; intra-block sample
 * accuracy comes from per-sample osc/adsr/biquad inside chain_render and a
 * per-sample mixer_render. No malloc/printf in the render path. */
#include <string.h>

#include "../dsp/adsr.h"
#include "../dsp/filter.h"
#include "../dsp/osc.h"
#include "../preset/defaults.h"
#include "../seq/transport.h"
#include "engine.h"


void engine_init(engine_t *e, double sr)
{
    memset(e, 0, sizeof *e);
    e->sr = sr;
    e->now = 0.0;
    for (int c = 0; c < 3; c++) {
        chain_init(&e->ch[c], 0x9E3779B97F4A7C15ull
                              + (uint64_t)c * (uint64_t)0x1000ull);
        /* owner-ordered modes amendment: per-instance deterministic seed,
         * same pattern as chain_init (random/markov PRNG, PLAN §6) */
        seq_init(&e->sq[c], 0x9E3779B97F4A7C15ull
                            + (uint64_t)c * (uint64_t)0x1000ull);
        e->prev_seq_on[c] = 0;
    }
    e->tr.gate = 0.0f;          /* memset covers: no one-shot, deadline 0 */
    e->prev_gate = 0;
    e->prev_run = 0;
    fx_reset(&e->fx);           /* FX stage init (memset + tuning tables) */
    e->p = OPHIS_DEFAULTS;
}

/* THE live step the UI is fed (engine.h): the playing step window,
 * mode-independent (B-SEQ1). */
void engine_seq_steps(const engine_t *e, int out[3])
{
    for (int c = 0; c < 3; c++)
        out[c] = e->cur_valid[c] ? e->cur_step[c] : -1;
}

void engine_panic_reset(engine_t *e)
{
    for (int c = 0; c < 3; c++) {
        chain_init(&e->ch[c], 0x9E3779B97F4A7C15ull
                              + (uint64_t)c * (uint64_t)0x1000ull);
        seq_init(&e->sq[c], 0x9E3779B97F4A7C15ull
                            + (uint64_t)c * (uint64_t)0x1000ull);
        e->prev_seq_on[c] = e->p.seq[c].on;
        e->cur_valid[c] = 0;       /* PANIC: full state reset (PLAN §6) */
    }
    memset(&e->mx, 0, sizeof e->mx);
    fx_reset(&e->fx);           /* PANIC: clear delay lines, reverb,
                                   safety biquads (owner-ordered) */
    e->prev_gate = 0;
    e->prev_run  = 0;
}

void engine_render_block(engine_t *e, float *out_stereo, int frames,
                         int trigger_req, int panic_req, int retrig_mask)
{
    /* transport first (env levels as of the end of the previous block) */
    float envl[3] = { e->ch[0].env.level, e->ch[1].env.level,
                      e->ch[2].env.level };
    transport_process(&e->tr, e->p.run, trigger_req, panic_req, envl,
                      (double)frames, e->now, e->sr);
    if (panic_req) {
        engine_panic_reset(e);
        e->p.run = 0;           /* ONE-BLOCK guard: RUN is the mailbox's
                                   (UI owns RUN, H3) — the next block's
                                   mailbox read restores it; after PANIC
                                   the UI commits draft.run = 0 */
    }

    int gate      = (e->tr.gate > 0.0f) ? 1 : 0;
    int gate_rise = gate && !e->prev_gate;
    e->prev_gate  = gate;

    /* once-mode restarts (owner-ordered modes amendment, PLAN §6):
     * RUN off->on or a seq toggled off->on restarts that sequencer */
    int run = e->p.run;
    int run_rise = run && !e->prev_run;
    e->prev_run = run;
    for (int c = 0; c < 3; c++) {
        if ((run_rise || (e->p.seq[c].on && !e->prev_seq_on[c]))) {
            seq_reset(&e->sq[c]);
            /* S3 (review): the restart must not keep the previous
             * window's step — the stale cur_step/cur_valid from before
             * the reset would hold the old pitch until the first new
             * boundary; the restart starts fresh at step 0 */
            e->cur_valid[c] = 0;
        }
        e->prev_seq_on[c] = e->p.seq[c].on;
    }

    /* per-chain block events */
    chain_event_t  ev[3] = { { 0, 0.0f }, { 0, 0.0f }, { 0, 0.0f } };
    chain_params_t cp[3];
    for (int c = 0; c < 3; c++) {
        cp[c] = e->p.chain[c];
        /* seq step window (owner order, PLAN §6): the chain plays the
         * CURRENT step's pitch_hz for that step's whole window — vel 0
         * steps change the pitch without a retrigger; sequencer OFF
         * returns the chain to the oscillator's freq parameter. */
        if (!e->p.seq[c].on)
            e->cur_valid[c] = 0;
        else if (e->cur_valid[c])
            cp[c].freq_hz =
                e->p.seq[c].steps[e->cur_step[c]].pitch_hz;
        if (gate_rise && !e->p.seq[c].on) {         /* direct mode */
            ev[c].retrigger = 1;
            ev[c].vel       = (float)e->p.chain[c].vel / 100.0f;
        }
        if (trigger_req) {                          /* all chains */
            ev[c].retrigger = 1;
            ev[c].vel       = (float)e->p.chain[c].vel / 100.0f;
        }
        if (retrig_mask & (1 << c)) {               /* this chain only */
            ev[c].retrigger = 1;
            ev[c].vel       = (float)e->p.chain[c].vel / 100.0f;
        }
        if (e->p.run && e->p.seq[c].on) {           /* sequencer drives */
            /* bpm: the GLOBAL tempo (AMENDED, PLAN §6) */
            int st = seq_tick(&e->sq[c], &e->p.seq[c], e->p.bpm,
                              e->now, e->sr);
            if (st >= 0) {
                e->cur_step[c] = st;        /* enter that step's window */
                e->cur_valid[c] = 1;
                cp[c].freq_hz =
                    e->p.seq[c].steps[st].pitch_hz;
                if (e->p.seq[c].steps[st].vel > 0) {
                    ev[c].retrigger = 1;
                    ev[c].vel = (float)e->p.seq[c].steps[st].vel / 100.0f;
                }
            }
        }
    }

    /* AMENDED (owner-ordered FX amendment, PLAN §5): mixer params carry
     * only the cross-mod slots; master level and the transport gate are
     * applied in the FX stage (fxp) after the safety filter. */
    mixer_params_t mxp;
    mxp.synth = &e->p;    /* S4: the mixer reads the live params — no
                             per-block copies to drift out of sync */

    fx_params_t fxp;
    fxp.delay    = e->p.delay;
    fxp.reverb   = e->p.reverb;
    fxp.safety   = e->p.safety;
    fxp.master   = e->p.master;
    fxp.gate     = e->tr.gate;  /* gate applied in fx_process, per sample */

    double out[3];
    for (int f = 0; f < frames; f++) {
        for (int c = 0; c < 3; c++) {
            const xmod_in_t *xm = &e->mx.xm[c];
            out[c] = chain_render(&e->ch[c], &cp[c], xm, &ev[c], e->sr,
                                  gate);
        }
        /* mod sources (D2): ids 1..9 from the ONE modsrc table — all
         * are the RAW pre-everything taps, consumed one sample late */
        for (int id = 1; id <= MODSRC_COUNT; id++) {
            const modsrc_t *ms = modsrc_get(id);
            e->mx.src[id - 1] = (ms->sub < 0)
                ? e->ch[ms->chain].raw_osc_out
                : e->ch[ms->chain].raw_sub_out[ms->sub];
        }

        double s = mixer_render(&e->mx, &mxp, out, e->sr);
        float l, r;
        fx_process(&e->fx, &fxp, (float)s, (float)s, &l, &r, e->sr);
        out_stereo[2 * f]     = l;
        out_stereo[2 * f + 1] = r;
    }

    e->now += (double)frames / e->sr;
}
