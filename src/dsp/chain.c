/* src/dsp/chain.c — chain graph, per-sample render order per PLAN.md §5:
 * 1. f = f_osc + Δf (own sub PITCH detune + cross-mod detune), clamped
 *    0.1 Hz–20 kHz
 * 2. main osc waveform -> x
 * 3. amplitude mod: x *= (1 + amt×m)
 * 4. ADSR env × velocity gain
 * 5. filter: fc = base×(1 + amt×m)×(1 + envamt/100×env), clamped
 *    30 Hz–18 kHz; Q from RES: pos = res/2 (0..50), Q = 0.5×40^(pos/50)
 *    (an active FLT_RES slot sweeps res per sample and forces the
 *    per-sample coefficient path — PLAN_OSC_NAMING.md R0c);
 *    poles: 12 dB/oct (one biquad) or 24 dB/oct (two identical cascaded)
 * 6. chain level -> out
 * Contract: IMPLEMENTATION.md §2. Pure, deterministic. No SDL/ALSA. */
#include <string.h>
#include <math.h>

#include "chain.h"
#include "exact_eq.h"

#define OPHIS_2PI 6.28318530717958647692

void chain_init(chain_state_t *c, uint64_t seed)
{
    memset(c, 0, sizeof *c);
    c->last_fc = -1.0f;   /* force coefficient update on first render */
    osc_init(&c->osc, WAVE_SINE, seed);
    osc_init(&c->sub[0].o, WAVE_SINE, seed + 1);
    osc_init(&c->sub[1].o, WAVE_SINE, seed + 2);
    adsr_reset(&c->env);
    c->vel = 0.0f;
    c->f_osc = 0.0f;
    c->raw_osc_out = 0.0;
}

double chain_render(chain_state_t *c, const chain_params_t *p,
                    const xmod_in_t *xm, const chain_event_t *ev, double sr,
                    int gate)
{
    if (ev->retrigger) {
        adsr_trigger(&c->env);
        c->vel = ev->vel;
    }

    /* 1. frequency with cross-mod detune, PLAN §5 step 1 (subs no
     * longer modulate — they are voices; PLAN_OPTIMIZATION.md O2) */
    double f = (double)p->freq_hz + xm->detune_hz;
    double amp_mod    = xm->amp_mod;
    double filter_mod = xm->filter_mod;
    /* PHASE (PM, approved plan change eacb368+fix): the modulator adds to
     * the carrier phase — phase += (amount/100 × pi) × m per sample, m the
     * normalized (-1..1) modulator; amount ±100 % = ±pi rad. Applied to
     * the main osc phase in step 2 below; xmod A.PHASE arrives via
     * xm->phase_mod with the same mapping. */
    double phase_mod  = xm->phase_mod;

    /* subs: raw audio voices at parent_f / div, captured for the matrix
     * sources (raw pre-level — a silent sub still modulates) */
    double sub_sum = 0.0;
    for (int i = 0; i < 2; i++) {
        c->sub[i].o.wave = p->sub[i].wave;
        double m = subosc_out(&c->sub[i], &p->sub[i], (double)p->freq_hz, sr);
        c->raw_sub_out[i] = m;
        sub_sum += (double)p->sub[i].level / 100.0 * m;
    }
    f = clampd(f, 0.1, 20000.0);
    c->f_osc = (float)f;

    /* 2. waveform. PM first: the carrier phase sees the DELTA of the
     * phase offset beta*m (beta = phase_mod × pi), so the accumulated
     * phase telescopes to theta(n) + beta*m(n) — the modulator "adds to
     * the carrier phase" (PLAN §5) without integrating into a frequency
     * shift. osc_next advances/wraps as usual; the square duty compare
     * and the wave shape both see the shifted phase. */
    c->osc.wave = p->wave;
    c->osc.duty = p->duty;    /* square pulse width (approved plan change) */
    double pm_rad = phase_mod * 3.14159265358979323846;
    c->osc.phase += (pm_rad - c->last_pm_rad) / OPHIS_2PI;   /* turns */
    c->last_pm_rad = pm_rad;
    double x = osc_next(&c->osc, f, sr);
    /* OSC SUM (Spice mixer arrangement, O2): the subs' audio joins the
     * main osc BEFORE everything downstream (mod/ADSR/filter) */
    x += sub_sum;
    c->raw_osc_out = x - sub_sum;   /* the MAIN osc only — matrix sources
                                       are unaffected by sub levels */

    /* 3. amplitude mod */
    x *= (1.0 + amp_mod);

    /* 4. ADSR env × velocity gain — gate-driven: on the chain gate's
     * falling edge (edge kept in chain_state_t) the envelope is driven
     * into release by passing gate=0 to adsr_next */
    int env_gate = (c->last_gate && !gate) ? 0 : gate;
    c->last_gate = gate;
    float env = adsr_next(&c->env, &p->adsr, sr, env_gate);
    double gain = (double)env * (double)c->vel;

    /* 5. filter (coefficients updated only when fc/mode/res/poles actually
     * change). fc: xmod filter mod then ENV AMT (chain ADSR drives
     * cutoff, PLAN §5 step 5), clamped 30 Hz–18 kHz. Q: res percent ->
     * pos = res/2 in 0..50, Q = 0.5 × 40^(pos/50) (log 0.5..20). */
    double fc = (double)p->fc * (1.0 + filter_mod)
              * (1.0 + (double)p->envamt / 100.0 * (double)env);
    fc = clampd(fc, 30.0, 18000.0);
    int poles = (p->poles == 24) ? 24 : 12;
    /* FLT_RES (owner order 2026-11-07, PLAN_OSC_NAMING.md R0c): the slot
     * sweeps the RES knob — effective percent = res + res_mod×100,
     * clamped 0..100. While a FLT_RES slot is active the Q changes per
     * sample, so the biquad coefficients recompute EVERY sample (via
     * biquad_set_coefs: history preserved, same as the ENV-cutoff path);
     * res_mod == 0 keeps the existing cache paths EXACTLY (no cost, no
     * behavior change for static-res chains). */
    int res_mod_active = !fexact(xm->res_mod, 0.0);
    if (res_mod_active) {
        double res_eff = clampd((double)p->res + xm->res_mod * 100.0,
                                0.0, 100.0);
        int pos = (int)(res_eff / 2.0);           /* 0..100 -> pos 0..50 */
        float q = (float)(0.5 * pow(40.0, (double)pos / 50.0));
        if (poles == 24) {
            biquad_set_coefs(&c->filt, p->fmode, (float)fc, sr, q);
            biquad_set_coefs(&c->filt2, p->fmode, (float)fc, sr, q);
        } else {
            biquad_set_coefs(&c->filt, p->fmode, (float)fc, sr, q);
        }
        /* keep the caches coherent: when the slot stops (res_mod -> 0)
         * the static paths below resume from the current settings */
        c->last_fc    = (float)fc;
        c->last_mode  = p->fmode;
        c->last_res   = p->res;
        c->last_poles = poles;
    } else if (!fexactf((float)fc, c->last_fc) ||
        p->fmode != c->last_mode ||
        p->res != c->last_res || poles != c->last_poles) {
        /* S2 (review): Q depends only on the res knob — computed here,
         * inside the coefficient-update branch, not per sample */
        int pos = p->res / 2;                     /* res 0..100 -> pos 0..50 */
        float q = (float)(0.5 * pow(40.0, (double)pos / 50.0));
        /* S1 (review): when ONLY fc changed, update the coefficients and
         * KEEP the filter history (the ENV-driven cutoff sweeps here per
         * sample — a state-clearing set would wipe z1/z2 every sample);
         * mode/res/poles changes still take the full clearing set */
        int only_fc = (!fexactf((float)fc, c->last_fc)
                       && p->fmode == c->last_mode &&
                       p->res == c->last_res && poles == c->last_poles);
        if (poles == 24) {
            if (only_fc) {
                biquad_set_coefs(&c->filt, p->fmode, (float)fc, sr, q);
                biquad_set_coefs(&c->filt2, p->fmode, (float)fc, sr, q);
            } else {
                biquad_cascade_set(&c->filt, &c->filt2, p->fmode, (float)fc,
                                   sr, q);
            }
        } else if (only_fc) {
            biquad_set_coefs(&c->filt, p->fmode, (float)fc, sr, q);
        } else {
            biquad_set(&c->filt, p->fmode, (float)fc, sr, q);
        }
        c->last_fc    = (float)fc;
        c->last_mode  = p->fmode;
        c->last_res   = p->res;
        c->last_poles = poles;
    }
    if (poles == 24)
        x = biquad_next(&c->filt2, biquad_next(&c->filt, x)) * gain;
    else
        x = biquad_next(&c->filt, x) * gain;

    /* 6. chain level */
    return x * (double)p->level / 100.0;
}
