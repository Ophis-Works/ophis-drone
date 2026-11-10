/* src/dsp/mixer.c — master sum + cross-mod matrix + tanh soft-clip:
 * tanh(1.5 × (A + B + C)); master level and gate live in the FX/master
 * stage (dsp/fx.c) after the safety filter (PLAN §5 amended).
 * Also computes per-chain modulation (m->xm[]) from the matrix slots and the
 * raw mod sources in m->src[] (IMPLEMENTATION.md §2, §4).
 * Pure, deterministic. No SDL/ALSA. */
#include <math.h>
#include <string.h>

#include "mixer.h"

/* frozen id -> (chain, sub) mapping (see mixer.h) */
static const modsrc_t MODSRC[MODSRC_COUNT] = {
    { 1, -1 }, { 1, 0 }, { 1, 1 },       /* 1..3: B.osc, B.sub1, B.sub2 */
    { 2, -1 }, { 2, 0 }, { 2, 1 },       /* 4..6: C.osc, C.sub1, C.sub2 */
    { 0, -1 }, { 0, 0 }, { 0, 1 },       /* 7..9: A.osc, A.sub1, A.sub2 */
};

const modsrc_t *modsrc_get(int id)
{
    return (id >= 1 && id <= MODSRC_COUNT) ? &MODSRC[id - 1] : NULL;
}

/* A2 (PLAN_OPTIMIZATION.md O3): THE single site of the dest mapping —
 * every mod dest's scaling convention lives here and nowhere else
 * (PITCH depth = amount Hz x source; PHASE amount/100 = ±pi rad; AMP /
 * FILTER / FRES amount/100; FM operator index amount/25). RING/MIX are NOT
 * here: they act on the target's OUTPUT (slot functions below). */
static void mod_apply(xmod_in_t *acc, mod_dest_t dest, float amount,
                      double s)
{
    switch (dest) {
    case MOD_PITCH:
        /* PITCH = VIBRATO (owner order 2026-11-05): depth amount Hz ×
         * the source signal, like every other dest (was: static
         * amount, source frequency irrelevant). */
        acc->detune_hz += amount * s;
        break;
    case MOD_AMP:
        acc->amp_mod += (double)amount / 100.0 * s;
        break;
    case MOD_PHASE:
        acc->phase_mod += (double)amount / 100.0 * s;
        break;
    case MOD_FILTER:
        acc->filter_mod += (double)amount / 100.0 * s;
        break;
    case MOD_FRES:
        /* FLT_RES (owner order 2026-11-07, PLAN_OSC_NAMING.md R0c): the
         * source sweeps the target's RES percent — accumulate into the
         * cutoff channel's sibling; chain.c clamps res + 100×res_mod. */
        acc->res_mod += (double)amount / 100.0 * s;
        break;
    case MOD_FENV:
        /* FLT_ENV (owner order 2026-11-10, PLAN_MOD_FENV_ADSR_DIV.md
         * MS1): the source sweeps the target's ENV AMT percent —
         * chain.c clamps envamt + 100×env_mod. */
        acc->env_mod += (double)amount / 100.0 * s;
        break;
    case MOD_ADSR_A:
    case MOD_ADSR_D:
    case MOD_ADSR_S:
    case MOD_ADSR_R:
        /* ADSR A/D/S/R (owner order 2026-11-10, same plan): normalized
         * input per channel, A/D/S/R index order; chain.c applies ratio
         * (A/D/R) or additive points (S). */
        acc->adsr_mod[dest - MOD_ADSR_A] += (double)amount / 100.0 * s;
        break;
    case MOD_FM:
        /* operator FM (owner order 2026-11-06): DX-style index
         * amount/25 cycles × the source (shares the phase channel
         * with the PHASE dest; both accumulate). */
        acc->phase_mod += (double)amount / 25.0 * s;
        break;
    case MOD_RING:
    case MOD_OFF:
    default:
        break;
    }
}

void matrix_render(mixer_state_t *m, const mixer_params_t *p,
                   const double chain_out[3], double post[3])
{
    const synth_params_t *sp = p->synth;

    /* cross-mod matrix (PLAN_MOD_ROUTING.md M1, owner order): each
     * slot routes to its own TARGET chain t = x->tgt (0..2) — the
     * modulation lands in m->xm[t] (detune Hz / phase / amp / filter
     * ratio per PLAN §5; src id 0 = OFF) */
    memset(m->xm, 0, sizeof m->xm);
    double ring_w[3][3], ring_s[3][3];     /* RING/MIX slots per target */
    int ring_mode[3][3];                   /* 0 = ring (multiply), 1 = mix
                                              (crossfade) */
    int ring_n[3] = { 0, 0, 0 };
    for (int slot = 0; slot < MOD_SLOTS; slot++) {
        const xmod_params_t *x = &sp->xmod[slot];
        int t = clampi(x->tgt, 0, 2);
        /* the slot's own ADD function (owner order 2026-11-06): the
         * source's audio blended into the TARGET — independent of the
         * dest, so a slot can add AND modulate, or either alone;
         * applied before the slot's modulation check (works with dest
         * OFF too) */
        if (x->mix > 0 && x->src >= 1 && x->src <= 9 && ring_n[t] < 3) {
            ring_w[t][ring_n[t]] = (double)x->mix / 100.0;
            ring_s[t][ring_n[t]] = m->src[x->src - 1];
            ring_mode[t][ring_n[t]] = 1;
            ring_n[t]++;
        }
        if (x->src < 1 || x->src > 9 || x->dest == MOD_OFF)
            continue;
        double s = m->src[x->src - 1];
        if (x->dest == MOD_RING) {
            /* ring mod on the target (owner order 2026-11-06):
             * collected here, applied to its output after the loop */
            if (ring_n[t] < 3) {
                ring_w[t][ring_n[t]] = fabs((double)x->amount) / 100.0;
                ring_s[t][ring_n[t]] = s * ((x->amount < 0.0f) ? -1.0 : 1.0);
                ring_mode[t][ring_n[t]] = 0;
                ring_n[t]++;
            }
            continue;
        }
        mod_apply(&m->xm[t], (mod_dest_t)x->dest, x->amount, s);
    }

    /* RING/MIX slots apply to their TARGET's output in slot order
     * (owner order 2026-11-06): RING multiplies
     * (T × ((1-w) + w × src)), MIX crossfades (T' = (1-w)×T + w×src) */
    double out_t[3];
    for (int t = 0; t < 3; t++) {
        out_t[t] = chain_out[t];
        for (int r = 0; r < ring_n[t]; r++) {
            if (ring_mode[t][r])
                out_t[t] = (1.0 - ring_w[t][r]) * out_t[t]
                           + ring_w[t][r] * ring_s[t][r];
            else
                out_t[t] *= (1.0 - ring_w[t][r])
                            + ring_w[t][r] * ring_s[t][r];
        }
    }
    for (int t = 0; t < 3; t++)
        post[t] = out_t[t];
}

double master_sum(const mixer_params_t *p, const double post[3])
{
    const synth_params_t *sp = p->synth;
    /* MIX VOLUME (PLAN_MOD_ROUTING.md M3, owner order): the chain's
     * master send scales its post-RING/MIX output into the sum — the
     * raw mod sources are untouched, a muted chain still modulates */
    return tanh(1.5 * ((double)sp->chain[0].mixvol / 100.0 * post[0]
                       + (double)sp->chain[1].mixvol / 100.0 * post[1]
                       + (double)sp->chain[2].mixvol / 100.0 * post[2]));
}

double mixer_render(mixer_state_t *m, const mixer_params_t *p,
                    const double chain_out[3], double sr)
{
    (void)sr;   /* pure sum; nothing sr-dependent */
    double post[3];
    matrix_render(m, p, chain_out, post);   /* builder composition (S2) */
    return master_sum(p, post);
}
