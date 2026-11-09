/* tests/test_mixer.c — PLAN.md §10 (M1) AS AMENDED (PLAN §5, owner order):
 * mixer_render sums chains and soft-clips: out == tanh(1.5*(A+B+C)) exactly.
 * mixer_params_t carries ONLY the cross-mod slots (xmod[3]) — master level
 * and master gate are applied in the FX/master stage (dsp/fx.c), never in
 * the mixer. Cross-mod B→A detune measurable. */
#include <string.h>
#include <math.h>
#include "harness.h"
#include "dsp/mixer.h"
#include "dsp/chain.h"
#include "../src/preset/defaults.h"

#define SR 44100.0
#define NM 44100

static synth_params_t mksp(void)   /* defaults with mixvol 100 (M3) */
{
    synth_params_t sp = OPHIS_DEFAULTS;
    return sp;
}

static mixer_params_t mkp(const synth_params_t *sp)
{
    mixer_params_t p;
    p.synth = sp;              /* S4: the mixer reads the live params */
    return p;
}

int main(void)
{
    /* ---- sum correctness: out == tanh(1.5 * (A+B+C)) exactly ---- */
    mixer_state_t m;
    memset(&m, 0, sizeof m);
    synth_params_t sp0 = mksp();
    mixer_params_t mp = mkp(&sp0);
    const double ch[3] = { 0.1, 0.2, -0.15 };
    double out = mixer_render(&m, &mp, ch, SR);
    CHECK(fabs(out - tanh(1.5 * (ch[0] + ch[1] + ch[2]))) < 1e-6);

    /* ---- tanh keeps |out| ≤ 1 even for maximal chains ---- */
    memset(&m, 0, sizeof m);
    const double hot[3] = { 1.0, 1.0, 1.0 };
    sp0 = mksp();
    mp = mkp(&sp0);
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out) <= 1.0);
    CHECK(fabs(out - tanh(4.5)) < 1e-6);       /* saturated but < 1 */

    /* ---- cross-mod matrix computation ----
     * PITCH dest (owner order 2026-11-05): VIBRATO depth — the field
     * carries amount × source, like every other dest (was: static
     * amount, which made the source frequency affect nothing). */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;  /* B.osc: id 1 -> src[0] */
    sp0 = mksp();
    mp = mkp(&sp0);
    sp0.xmod[0].src = 1; sp0.xmod[0].dest = MOD_PITCH; sp0.xmod[0].amount = 4.0f;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[0].detune_hz - 2.0) < 1e-6);   /* amount × source */
    CHECK(fabs(m.xm[0].amp_mod) < 1e-9);
    CHECK(fabs(m.xm[1].detune_hz) < 1e-9);
    CHECK(fabs(m.xm[2].detune_hz) < 1e-9);

    /* src OFF → no modulation; dest OFF → no modulation */
    memset(&m, 0, sizeof m);
    sp0.xmod[0].src = 0; sp0.xmod[0].dest = MOD_PITCH; sp0.xmod[0].amount = 4.0f;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[0].detune_hz) < 1e-9);
    sp0.xmod[0].src = 1; sp0.xmod[0].dest = MOD_OFF;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[0].detune_hz) < 1e-9);

    /* RING dest (owner order 2026-11-06): A × ((1-w) + w×src) per slot;
     * amount 100 = full ring mod, amount 0 = dry */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;
    sp0.xmod[0].src = 1; sp0.xmod[0].dest = MOD_RING; sp0.xmod[0].amount = 100.0f;
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(1.5 * (0.5 + 2.0))) < 1e-6); /* A×src=0.5, B+C=2 */
    memset(&m, 0, sizeof m);
    sp0.xmod[0].amount = 0.0f;
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(4.5)) < 1e-6);           /* dry at amount 0 */

    /* MIX knob (owner order 2026-11-06, the "usual" mixing): A'
     * = (1-w)×A + w×src — INDEPENDENT of the dest, so a slot can add
     * AND modulate, or either alone. mix 100 full wet, 0 dry. */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;
    sp0.xmod[0].src = 1; sp0.xmod[0].dest = MOD_OFF; sp0.xmod[0].mix = 100;
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(1.5 * (0.5 + 2.0))) < 1e-6); /* A' = 0.5 */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;
    sp0.xmod[0].mix = 0;
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(4.5)) < 1e-6);           /* dry at mix 0 */
    sp0.xmod[0].mix = 50;
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(1.5 * (0.75 + 2.0))) < 1e-6); /* 50/50 blend */
    /* BOTH together (the point of the split): RING dest at amount 100
     * AND mix 50 — ring first, then the blend */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;
    sp0.xmod[0].dest = MOD_RING; sp0.xmod[0].amount = 100.0f;
    sp0.xmod[0].mix = 50;
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(1.5 * (0.75 * 0.5 + 2.0))) < 1e-6); /* (A×s)×0.5
                                                         + 1.5 dry half */

    /* FM dest: the phase channel carries amount/25 × src (operator
     * index; shares the channel with PHASE, both accumulate) */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;
    sp0.xmod[0].dest = MOD_FM;
    sp0.xmod[0].amount = 100.0f;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[0].phase_mod - 2.0) < 1e-6);   /* 100/25 × 0.5 */

    /* ---- TARGET routing (PLAN_MOD_ROUTING.md M1, owner order): each
     * slot carries a target chain 0..2; the modulation lands in
     * m->xm[tgt], and the RING/MIX applications hit the target's own
     * output instead of A's ---- */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;
    sp0 = mksp();
    mp = mkp(&sp0);
    sp0.xmod[0].src = 1; sp0.xmod[0].dest = MOD_PITCH;
    sp0.xmod[0].amount = 4.0f; sp0.xmod[0].tgt = 1;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[1].detune_hz - 2.0) < 1e-6);   /* lands on B */
    CHECK(fabs(m.xm[0].detune_hz) < 1e-9);         /* A untouched */
    CHECK(fabs(m.xm[2].detune_hz) < 1e-9);
    /* RING to chain C: out = tanh(1.5*(A + B + C×src)) */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;
    sp0.xmod[0].dest = MOD_RING; sp0.xmod[0].amount = 100.0f;
    sp0.xmod[0].tgt = 2;
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(1.5 * (1.0 + 1.0 + 0.5))) < 1e-6);
    /* the slot's ADD knob follows the TARGET too: mix 100 into B */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;
    sp0.xmod[0].dest = MOD_OFF; sp0.xmod[0].amount = 0.0f;
    sp0.xmod[0].mix = 100; sp0.xmod[0].tgt = 1;
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(1.5 * (1.0 + 0.5 + 1.0))) < 1e-6);

    /* ---- MIX VOLUME (PLAN_MOD_ROUTING.md M3, owner order): the
     * per-chain master send — scales the chain's post-RING/MIX output
     * into the master sum; never touches the mod sources ---- */
    memset(&m, 0, sizeof m);
    sp0 = mksp();
    mp = mkp(&sp0);
    sp0.chain[1].mixvol = 0;                      /* B silent at master */
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(1.5 * 2.0)) < 1e-6);    /* A + C only */
    sp0.chain[0].mixvol = 50;                     /* A at half send */
    out = mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(out - tanh(1.5 * 1.5)) < 1e-6);    /* 0.5×1 + 0×1 + 1×1 */

    /* ---- S2/A2 (PLAN_OPTIMIZATION.md O3): the split — matrix_render
     * produces the post-slot outputs, master_sum is the pure tanh of
     * the mixvol-scaled sum, mixer_render is their exact composition;
     * mod_apply is the single dest-mapping site (the per-dest exact
     * pins above ARE its spec) ---- */
    {
        synth_params_t sp3 = mksp();
        sp3.xmod[0].src = 1; sp3.xmod[0].dest = MOD_RING;
        sp3.xmod[0].amount = 100.0f; sp3.xmod[0].tgt = 2;
        mixer_params_t mp3 = mkp(&sp3);
        mixer_state_t m3;
        memset(&m3, 0, sizeof m3);
        m3.src[0] = 0.5;
        double post[3];
        matrix_render(&m3, &mp3, hot, post);
        CHECK(fexact(post[2], 0.5));           /* C ringed by 0.5 */
        CHECK(fexact(post[0], 1.0) && fexact(post[1], 1.0));
        memset(&sp3.chain[0].mixvol, 0, sizeof sp3.chain[0].mixvol);
        sp3.chain[1].mixvol = 0;               /* only C's send open */
        double ms = master_sum(&mp3, post);
        CHECK(fabs(ms - tanh(1.5 * 0.5)) < 1e-12);
        mixer_state_t m4;
        memset(&m4, 0, sizeof m4);
        m4.src[0] = 0.5;                       /* same source as m3 */
        double full = mixer_render(&m4, &mp3, hot, SR);
        CHECK(fabs(full - ms) < 1e-12);        /* composition equality */
    }

    /* A sources (PLAN_MOD_ROUTING.md M2): ids 7..9 read src[6..8] —
     * id 7 = A.osc; self-routing A -> A is legal (D3 yes) */
    memset(&m, 0, sizeof m);
    m.src[6] = 0.25;                       /* A.osc: id 7 -> src[6] */
    sp0.xmod[0].src = 7; sp0.xmod[0].dest = MOD_PITCH;
    sp0.xmod[0].amount = 4.0f; sp0.xmod[0].tgt = 0;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[0].detune_hz - 1.0) < 1e-6);   /* 4 × 0.25 */
    CHECK(fabs(m.xm[1].detune_hz) < 1e-9);

    /* PR1 (PLAN_OSC_NAMING.md §1.0g/R0b): the FLT_CUT pin — the block
     * below IS the matrix-level FLT_CUT spec: the mixer slot with dest
     * MOD_FILTER (renamed "flt_cut" per §1.0e) accumulates
     * filter_mod = amount/100 × src into xm->filter_mod exactly as
     * today. NO new pin written — this existing block covers it; the
     * FLT_RES sibling lives in PR2 below. */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;  /* B.osc: id 1 -> src[0] */
    sp0.xmod[0].tgt = 0;                    /* back to A (pin blocks above
                                              left the slot on C/B) */
    sp0.xmod[0].src = 1; sp0.xmod[0].dest = MOD_AMP; sp0.xmod[0].amount = 100.0f;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[0].amp_mod - 0.5) < 1e-6);      /* amount/100 * m */
    sp0.xmod[0].dest = MOD_FILTER;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[0].filter_mod - 0.5) < 1e-6);
    CHECK(fabs(m.xm[0].amp_mod) < 1e-9);

    /* PR2 (PLAN_OSC_NAMING.md §1.0 R0c, §1.4): the FLT_RES pin — dest
     * MOD_FRES accumulates res_mod = amount/100 × src into xm->res_mod
     * (the NEW 5th xmod_in_t field, chain.h); the cutoff channel
     * (filter_mod, PR1) stays untouched. Compile-red until types.h
     * gains MOD_FRES and chain.h the res_mod field. */
    memset(&m, 0, sizeof m);
    m.src[0] = 0.5;                                /* B.osc: id 1 -> src[0] */
    sp0.xmod[0].tgt = 0;
    sp0.xmod[0].src = 1; sp0.xmod[0].dest = MOD_FRES; sp0.xmod[0].amount = 100.0f;
    (void)mixer_render(&m, &mp, hot, SR);
    CHECK(fabs(m.xm[0].res_mod - 0.5) < 1e-6);     /* 100/100 × 0.5 */
    CHECK(fabs(m.xm[0].filter_mod) < 1e-9);        /* FLT_CUT untouched */

    /* ---- P1 (PLAN_MOD_MATRIX_16.md §1.3): the detune accumulator is
     * EXACT at the reachable maximum — all 3 slots target chain A with
     * dest MOD_PITCH, amount 0.1f, source 1.0; the accumulated
     * detune_hz must be BIT-EQUAL to the double reference
     * 3.0 × (double)0.1f. RED with float xmod_in_t fields: the float
     * accumulator rounds 3×0.1f to 0.30000001192092896, which is not
     * the double reference 0.30000000447034836. ---- */
    memset(&m, 0, sizeof m);
    m.src[0] = 1.0;                            /* B.osc: id 1 -> src[0] */
    sp0 = mksp();
    mp = mkp(&sp0);
    for (int i = 0; i < 3; i++) {
        sp0.xmod[i].src = 1;
        sp0.xmod[i].dest = MOD_PITCH;
        sp0.xmod[i].amount = 0.1f;
        sp0.xmod[i].tgt = 0;                   /* all three land on A */
    }
    {
        double post[3];
        matrix_render(&m, &mp, hot, post);
        CHECK(fexact((double)m.xm[0].detune_hz, 3.0 * (double)0.1f));
    }

    /* ---- cross-mod B→A detune measurable end-to-end: 200 → 202 Hz ---- */
    chain_params_t cp;
    memset(&cp, 0, sizeof cp);
    cp.freq_hz = 200.0;
    cp.wave    = WAVE_TRIANGLE;
    cp.adsr.a = 0.001f; cp.adsr.d = 0.001f; cp.adsr.s = 100; cp.adsr.r = 0.001f;
    cp.fmode = FILT_LP; cp.fc = 15000.0f;
    cp.level = 100; cp.vel = 100;

    chain_state_t c;
    chain_init(&c, 5);
    memset(&m, 0, sizeof m);
    m.src[0] = 1.0;                                /* B.osc: id 1 -> src[0] */
    sp0.xmod[0].tgt = 0;                    /* A (the pins above left 2) */
    sp0.xmod[0].src = 1; sp0.xmod[0].dest = MOD_PITCH; sp0.xmod[0].amount = 2.0f;
    (void)mixer_render(&m, &mp, hot, SR);          /* compute m.xm[0] */
    double obuf[NM];
    for (int i = 0; i < NM; i++) {
        chain_event_t ev = { 0, 0.0f };
        if (i == 0) { ev.retrigger = 1; ev.vel = 1.0f; }
        obuf[i] = chain_render(&c, &cp, &m.xm[0], &ev, SR, 1);
    }
    double rate = h_zc_rate(obuf + 5000, NM - 5000, SR);
    CHECK(fabs(rate - 202.0) < 1.5);

    HARNESS_REPORT();
}
