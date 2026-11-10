/* tests/test_chain.c — PLAN.md §10 (M1): velocity 0 → silence;
 * trigger → audible; sub mod observable end-to-end; gate falling edge
 * → release → silence (gate-driven envelope, IMPLEMENTATION.md §2).
 * Approved plan change (commits eacb368+fix): xmod dest A.PHASE — the
 * slot's modulator adds to chain A's carrier phase, amount ±100% = ±π rad;
 * exercised end-to-end through the mixer xmod matrix (B.osc → A). */
#include <string.h>
#include <math.h>
#include "harness.h"
#include "dsp/chain.h"
#include "../src/preset/defaults.h"
#include "dsp/mixer.h"

#define SR 44100.0
#define NC 88200            /* 2 s */

static double buf[NC];

static chain_params_t mkchain(void)
{
    chain_params_t p;
    memset(&p, 0, sizeof p);
    p.freq_hz = 200.0;
    p.wave    = WAVE_TRIANGLE;
    /* subs: Spice/Subharmonicon voices (O2) — level 0 = inaudible */
    p.sub[0].div = 1; p.sub[0].wave = WAVE_SINE;
    p.sub[0].duty = 50; p.sub[0].level = 0;
    p.sub[1].div = 1; p.sub[1].wave = WAVE_SINE;
    p.sub[1].duty = 50; p.sub[1].level = 0;
    p.adsr.a = 0.001f; p.adsr.d = 0.001f; p.adsr.s = 100; p.adsr.r = 0.001f;
    p.fmode  = FILT_LP;
    p.fc     = 15000.0f;
    p.res    = 0;      /* Q = 0.5 — flat baseline */
    p.envamt = 0;      /* ADSR does not drive cutoff in baseline */
    p.poles  = 12;     /* single biquad */
    p.level  = 100;
    p.vel    = 100;
    return p;
}

/* render n samples with the gate held high; retrigger with the given
 * vel at sample 0 only */
static void chain_run(chain_state_t *c, const chain_params_t *p,
                      const xmod_in_t *xm, float vel, int n)
{
    chain_init(c, 7);
    for (int i = 0; i < n; i++) {
        chain_event_t ev = { 0, 0.0f };
        if (i == 0) { ev.retrigger = 1; ev.vel = vel; }
        buf[i] = chain_render(c, p, xm, &ev, SR, 1);
    }
}

/* measured rate and RMS of the steady part (skip the 2000-sample attack) */
static double rate_steady(int n)
{
    return h_zc_rate(buf + 5000, n - 5000, SR);
}
static double rms_steady(int n)
{
    return h_rms(buf + 5000, n - 5000);
}

int main(void)
{
    chain_state_t c;
    chain_params_t p;
    xmod_in_t xm0 = { 0 };

    /* ---- velocity 0 → silence ---- */
    p = mkchain();
    chain_run(&c, &p, &xm0, 0.0f, NC);
    CHECK(rms_steady(NC) < 1e-6);

    /* ---- trigger → audible ---- */
    p = mkchain();
    chain_run(&c, &p, &xm0, 1.0f, NC);
    double base_rms = rms_steady(NC);
    double base_rate = rate_steady(NC);
    CHECK(base_rms > 0.05);
    CHECK(fabs(base_rate - 200.0) < 1.5);      /* sanity: rate == freq */

    /* ---- SP1 (PLAN_OPTIMIZATION.md O2): the sub is an AUDIO voice —
     * level 100, div 1, same wave/phase sums to ~2x the osc alone;
     * level 0 with a WILD sub (div 3 saw) is byte-identical to the
     * bare chain. Carrier here: 200 Hz SINE for an exact in-phase sum. */
    p = mkchain();
    p.wave = WAVE_SINE;
    chain_run(&c, &p, &xm0, 1.0f, NC);
    double sine_rms = rms_steady(NC);
    p.sub[0].div = 1; p.sub[0].wave = WAVE_SINE; p.sub[0].level = 100;
    chain_run(&c, &p, &xm0, 1.0f, NC);
    double sum_rms = rms_steady(NC);
    CHECK(sum_rms > 1.9 * sine_rms && sum_rms < 2.1 * sine_rms);
    p.sub[0].div = 3; p.sub[0].wave = WAVE_SAW; p.sub[0].level = 0;
    chain_run(&c, &p, &xm0, 1.0f, NC);
    CHECK(fabs(rms_steady(NC) / sine_rms - 1.0) < 1e-9);  /* silent sub */

    /* ---- SP2 (PLAN_OPTIMIZATION.md O2): divider tracking — the sub
     * plays parent_f / div. Carrier 200 Hz SAW (its harmonics are
     * 200k — 100 Hz and 50 Hz are NOT harmonics, so the sub owns the
     * probed bins). div 2 -> 100 Hz; div 4 -> 50 Hz. */
    p = mkchain();
    p.wave = WAVE_SAW;
    p.sub[0].div = 2; p.sub[0].wave = WAVE_SINE; p.sub[0].level = 100;
    chain_run(&c, &p, &xm0, 1.0f, NC);
    double g100_d2 = h_goertzel_mag(buf + 5000, NC - 5000, 100.0, SR);
    double g50_d2  = h_goertzel_mag(buf + 5000, NC - 5000, 50.0, SR);
    p.sub[0].div = 4;
    chain_run(&c, &p, &xm0, 1.0f, NC);
    double g100_d4 = h_goertzel_mag(buf + 5000, NC - 5000, 100.0, SR);
    double g50_d4  = h_goertzel_mag(buf + 5000, NC - 5000, 50.0, SR);
    CHECK(g100_d2 > 5.0 * g100_d4);      /* div 2 puts energy at 100 */
    CHECK(g50_d4  > 5.0 * g50_d2);       /* div 4 puts energy at 50 */

    /* ---- gate falling edge → release → silence end-to-end ---- */
    p = mkchain();
    chain_init(&c, 7);
    for (int i = 0; i < NC; i++) {
        chain_event_t ev = { 0, 0.0f };
        int gate = (i < 20000);                /* falling edge at 20000 */
        if (i == 0) { ev.retrigger = 1; ev.vel = 1.0f; }
        buf[i] = chain_render(&c, &p, &xm0, &ev, SR, gate);
    }
    /* r = 0.001 s → 44 release samples after the edge */
    CHECK(h_rms(buf + 40000, 10000) < 1e-6);

    /* ---- filter expansion (PLAN §5): ENV AMT drives cutoff end-to-end.
     * Saw 200 Hz, LP fc=500; measure the 10 kHz harmonic (50th). ---- */
    {
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc   = 500.0f;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double g10k_base = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);

        /* envamt +100: env=1 in sustain -> fc = 500×(1+1) = 1000 Hz,
         * 10 kHz content rises (stop slope 12 dB/oct ≈ +6 dB here) */
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 500.0f; p.envamt = 100;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double g10k_up = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        CHECK(g10k_up > g10k_base * 1.4);

        /* envamt -50: fc = 250 Hz, 10 kHz content falls */
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 500.0f; p.envamt = -50;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double g10k_down = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        CHECK(g10k_down < g10k_base / 1.4);
    }

    /* ---- res passes through: LP with fc at the tone frequency — higher
     * RES (Q) raises the measured gain at fc (RBJ BP is constant 0 dB
     * peak, so LP at fc is the observable) ---- */
    {
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fmode = FILT_LP; p.fc = 200.0f; p.res = 0;    /* Q = 0.5 */
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double rms_q0 = rms_steady(NC);
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fmode = FILT_LP; p.fc = 200.0f; p.res = 100;  /* Q = 20 */
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double rms_q20 = rms_steady(NC);
        CHECK(rms_q20 > rms_q0 * 1.5);
    }

    /* ---- poles = 24: rejection at 10 kHz deepens end-to-end ---- */
    {
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 1000.0f; p.poles = 12;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double g12 = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 1000.0f; p.poles = 24;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double g24 = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        CHECK(g24 < g12);
    }

    /* ---- fc clamps to 30 Hz–18 kHz with env + mod (PLAN §5) ---- */
    {
        /* high: base 15000, filter_mod ×1.2 and envamt +100 -> 36000,
         * clamped to 18000. Compare against the exact 18000 Hz filter
         * (base 9000, envamt +100, no mod): spectra must match. */
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 9000.0f; p.envamt = 100;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double rms_18k = rms_steady(NC);

        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 15000.0f; p.envamt = 100;
        xmod_in_t xm_hi = { .filter_mod = 0.2f };   /* fc -> 36000 -> clamp */
        chain_run(&c, &p, &xm_hi, 1.0f, NC);
        double rms_clamped = rms_steady(NC);
        CHECK(fabs(rms_clamped / rms_18k - 1.0) < 0.10);

        /* sanity: the unclamped-but-equal-frequency pair really match */
        /* low: base 60, envamt -100 -> 0 -> clamp 30 Hz. vs 30 Hz direct:
         * same 10 kHz stop depth (both deep), same passband RMS scale */
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 60.0f; p.envamt = -100;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double g_lo_clamp = h_goertzel_mag(buf + 5000, NC - 5000, 200.0, SR);
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 30.0f; p.envamt = 0;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        double g_lo_direct = h_goertzel_mag(buf + 5000, NC - 5000, 200.0, SR);
        CHECK(fabs(g_lo_clamp / g_lo_direct - 1.0) < 0.15);
        /* output stays finite under the extremes */
        for (int i = 0; i < NC; i++)
            if (!(buf[i] > -2.0 && buf[i] < 2.0)) { CHECK(0); break; }
    }

    /* ---- xmod slot dest A.PHASE (PLAN §5, approved change) ----
     * Slot 1: src = B.osc (raw source fed as a 30 Hz sine), dest = PHASE,
     * amount ±100/±50/0. A renders a 200 Hz sine carrier; PM by a 30 Hz
     * sine moves carrier energy into 200±30 Hz sidebands:
     * carrier ×|J0(β)| (1.0 / 0.472 / 0.304), sideband ×|J1(β)|. */
    {
        mixer_state_t m;
        synth_params_t spx = OPHIS_DEFAULTS;
        mixer_params_t mp;
        memset(&m, 0, sizeof m);
        mp.synth = &spx;

        /* the mixer must map dest PHASE into the xm phase channel,
         * normalized like amp/filter: phase_mod = amount/100 × m */
        m.src[0] = 0.5;
        spx.xmod[0].src = 1; spx.xmod[0].dest = MOD_PHASE;
        spx.xmod[0].amount = 100.0f;
        const double hot[3] = { 1.0, 1.0, 1.0 };
        (void)mixer_render(&m, &mp, hot, SR);
        CHECK(fabs(m.xm[0].phase_mod - 0.5) < 1e-6);
        CHECK(fabs(m.xm[0].detune_hz) < 1e-9 &&
              fabs(m.xm[0].amp_mod) < 1e-9 &&
              fabs(m.xm[0].filter_mod) < 1e-9);    /* phase only */

        p = mkchain();
        p.wave = WAVE_SINE;                        /* pure carrier */

        static const double amts[3] = { 0.0, 50.0, 100.0 };
        double carr[3], side_up[3];
        for (int a = 0; a < 3; a++) {
            spx.xmod[0].amount = (float)amts[a];
            chain_init(&c, 7);
            for (int i = 0; i < NC; i++) {
                chain_event_t ev = { 0, 0.0f };
                if (i == 0) { ev.retrigger = 1; ev.vel = 1.0f; }
                m.src[0] = sin(2.0 * OPHIS_PI * 30.0 * (double)i / SR);
                (void)mixer_render(&m, &mp, hot, SR);   /* compute m.xm[0] */
                buf[i] = chain_render(&c, &p, &m.xm[0], &ev, SR, 1);
            }
            carr[a]    = h_goertzel_mag(buf + 5000, NC - 5000, 200.0, SR);
            side_up[a] = h_goertzel_mag(buf + 5000, NC - 5000, 230.0, SR);
        }

        /* amount 0: unmodulated carrier */
        CHECK(carr[0] > 0.05);
        CHECK(side_up[0] < 0.02 * carr[0]);

        /* amount 100 measurably alters the carrier spectrum */
        CHECK(carr[2] < 0.45 * carr[0]);           /* |J0(π)| = 0.304 */
        CHECK(side_up[2] > 0.15 * carr[0]);        /* |J1(π)| = 0.285 */

        /* ±50 differs from ±100 (|J0(π/2)| = 0.472, |J1(π/2)| = 0.567) */
        CHECK(carr[1] > 0.35 * carr[0] && carr[1] < 0.62 * carr[0]);
        CHECK(carr[1] > carr[2]);
        CHECK(side_up[1] > side_up[2]);
    }

    /* ---- PLAN_MOD_FENV_ADSR_DIV.md MS1: envelope-stage timing helpers.
     * first_above: first index where |buf| rises above frac × the late
     * (sustain) peak — attack timing. last_above: last index above
     * frac × the global peak — decay/release timing. ---- */
    {
        /* FLT_ENV consumption: envamt 0 + env_mod 0.5 (i.e. +50 points)
         * must shift fc EXACTLY like envamt 50 without mod — same
         * formula, same coefficient path. */
        double g_ref, g_mod;
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 500.0f; p.envamt = 50;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        g_ref = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 500.0f; p.envamt = 0;
        xmod_in_t xm_env;
        memset(&xm_env, 0, sizeof xm_env);
        xm_env.env_mod = 0.5;                  /* +50 envamt points */
        chain_run(&c, &p, &xm_env, 1.0f, NC);
        g_mod = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        CHECK(fabs(g_mod / g_ref - 1.0) < 1e-9);     /* exact identity */

        /* clamp at +100: envamt 100 + env_mod 1.0 == envamt 100 alone */
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 500.0f; p.envamt = 100;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        g_ref = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 500.0f; p.envamt = 100;
        memset(&xm_env, 0, sizeof xm_env);
        xm_env.env_mod = 1.0;
        chain_run(&c, &p, &xm_env, 1.0f, NC);
        g_mod = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        CHECK(fabs(g_mod / g_ref - 1.0) < 1e-9);

        /* clamp at -100: envamt -100 + env_mod -1.0 == envamt -100 alone */
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 500.0f; p.envamt = -100;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        g_ref = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        p = mkchain();
        p.wave = WAVE_SAW;
        p.fc = 500.0f; p.envamt = -100;
        memset(&xm_env, 0, sizeof xm_env);
        xm_env.env_mod = -1.0;
        chain_run(&c, &p, &xm_env, 1.0f, NC);
        g_mod = h_goertzel_mag(buf + 5000, NC - 5000, 10000.0, SR);
        CHECK(fabs(g_mod / g_ref - 1.0) < 1e-9);
    }

    /* ---- ADSR consumption (owner decision: A/D/R ratio, S additive
     * points). adsr_mod[i]: 0=A 1=D 2=S 3=R. ---- */
    {
        /* first index where |buf| rises above frac × the sustain peak */
        double peak;
        int t_base, t_mod;
        xmod_in_t xm_a;

        /* a_mod +1.0: attack 0.1 s -> 0.2 s (~2x the samples) */
        p = mkchain();
        p.wave = WAVE_SINE;
        p.adsr.a = 0.1f;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        peak = 0.0;
        for (int i = NC - 2000; i < NC; i++)
            if (fabs(buf[i]) > peak) peak = fabs(buf[i]);
        t_base = -1;
        for (int i = 0; i < NC && t_base < 0; i++)
            if (fabs(buf[i]) >= 0.9 * peak) t_base = i;
        memset(&xm_a, 0, sizeof xm_a);
        xm_a.adsr_mod[0] = 1.0;                /* ratio ×2 */
        chain_run(&c, &p, &xm_a, 1.0f, NC);
        t_mod = -1;
        for (int i = 0; i < NC && t_mod < 0; i++)
            if (fabs(buf[i]) >= 0.9 * peak) t_mod = i;
        CHECK(t_base > 3000);                        /* ~0.9 × 4410 */
        CHECK(t_mod > 17 * t_base / 10 && t_mod < 23 * t_base / 10);

        /* a_mod -1.0: ratio 0 -> clamped 0 s -> 1-sample attack */
        memset(&xm_a, 0, sizeof xm_a);
        xm_a.adsr_mod[0] = -1.0;
        chain_run(&c, &p, &xm_a, 1.0f, NC);
        t_mod = -1;
        for (int i = 0; i < NC && t_mod < 0; i++)
            if (fabs(buf[i]) >= 0.9 * peak) t_mod = i;
        CHECK(t_mod >= 0 && t_mod < 100);

        /* d_mod +1.0: decay 0.1 s -> 0.2 s (linear to sustain 0) */
        p = mkchain();
        p.wave = WAVE_SINE;
        p.adsr.a = 0.001f; p.adsr.d = 0.1f; p.adsr.s = 0;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        peak = 0.0;
        for (int i = 0; i < NC; i++)
            if (fabs(buf[i]) > peak) peak = fabs(buf[i]);
        t_base = -1;
        for (int i = NC - 1; i >= 0 && t_base < 0; i--)
            if (fabs(buf[i]) >= 0.1 * peak) t_base = i;
        xmod_in_t xm_d;
        memset(&xm_d, 0, sizeof xm_d);
        xm_d.adsr_mod[1] = 1.0;
        chain_run(&c, &p, &xm_d, 1.0f, NC);
        t_mod = -1;
        for (int i = NC - 1; i >= 0 && t_mod < 0; i--)
            if (fabs(buf[i]) >= 0.1 * peak) t_mod = i;
        CHECK(t_base > 3500);                        /* ~0.9 × 4410 */
        CHECK(t_mod > 15 * t_base / 10 && t_mod < 25 * t_base / 10);

        /* s_mod +1.0 on s=50: +100 points -> clamp 100 -> sustain rms
         * equals the s=100 baseline exactly */
        double rms_s100;
        p = mkchain();
        p.wave = WAVE_SINE;
        p.adsr.s = 100;
        chain_run(&c, &p, &xm0, 1.0f, NC);
        rms_s100 = rms_steady(NC);
        p = mkchain();
        p.wave = WAVE_SINE;
        p.adsr.s = 50;
        xmod_in_t xm_s;
        memset(&xm_s, 0, sizeof xm_s);
        xm_s.adsr_mod[2] = 1.0;
        chain_run(&c, &p, &xm_s, 1.0f, NC);
        CHECK(fabs(rms_steady(NC) / rms_s100 - 1.0) < 1e-9);
        /* s_mod -1.0 on s=50: -100 points -> clamp 0 -> silence */
        memset(&xm_s, 0, sizeof xm_s);
        xm_s.adsr_mod[2] = -1.0;
        chain_run(&c, &p, &xm_s, 1.0f, NC);
        CHECK(rms_steady(NC) < 1e-6);

        /* r_mod +1.0: release 0.1 s -> 0.2 s after the gate's falling
         * edge at 20000 */
        p = mkchain();
        p.wave = WAVE_SINE;
        p.adsr.a = 0.001f; p.adsr.d = 0.001f;
        p.adsr.s = 100; p.adsr.r = 0.1f;
        chain_init(&c, 7);
        for (int i = 0; i < NC; i++) {
            chain_event_t ev = { 0, 0.0f };
            int gate = (i < 20000);
            if (i == 0) { ev.retrigger = 1; ev.vel = 1.0f; }
            buf[i] = chain_render(&c, &p, &xm0, &ev, SR, gate);
        }
        peak = 0.0;
        for (int i = 0; i < 20000; i++)
            if (fabs(buf[i]) > peak) peak = fabs(buf[i]);
        t_base = -1;
        for (int i = NC - 1; i >= 0 && t_base < 0; i--)
            if (fabs(buf[i]) >= 0.1 * peak) t_base = i;
        xmod_in_t xm_r;
        memset(&xm_r, 0, sizeof xm_r);
        xm_r.adsr_mod[3] = 1.0;
        chain_init(&c, 7);
        for (int i = 0; i < NC; i++) {
            chain_event_t ev = { 0, 0.0f };
            int gate = (i < 20000);
            if (i == 0) { ev.retrigger = 1; ev.vel = 1.0f; }
            buf[i] = chain_render(&c, &p, &xm_r, &ev, SR, gate);
        }
        t_mod = -1;
        for (int i = NC - 1; i >= 0 && t_mod < 0; i--)
            if (fabs(buf[i]) >= 0.1 * peak) t_mod = i;
        CHECK(t_base > 20000 + 3500);                /* ~20000+0.9×4410 */
        /* compare release LENGTHS (the 20000-sample gate offset would
         * skew a raw index ratio) */
        {
            int rl_base = t_base - 20000, rl_mod = t_mod - 20000;
            CHECK(rl_mod > 15 * rl_base / 10 && rl_mod < 25 * rl_base / 10);
        }
    }

    /* sub RING/FM dest pins (owner order 2026-11-06) are GONE with the
     * sub dests (O2/DP2): the matrix covers them — test_mixer pins the
     * RING/MIX application, test_chain's PM pin above exercises the
     * phase channel, test_engine_seq pins FM end-to-end (P2). */

    HARNESS_REPORT();
}
