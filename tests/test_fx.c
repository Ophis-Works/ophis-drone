/* tests/test_fx.c — FX stage tests (PLAN §10 row test_fx.c, PLAN §2/§5
 * amended signal chain, IMPLEMENTATION.md §2 dsp/fx.c contract).
 * TDD: written BEFORE dsp/fx.h / dsp/fx.c exist.
 *
 * Contract exercised (IMPLEMENTATION.md §2, owner-ordered):
 *   void fx_process(fx_state_t *fx, const fx_params_t *p,
 *                   float in_l, float in_r, float *out_l, float *out_r,
 *                   double sr);
 *   void fx_reset(fx_state_t *fx);
 *   fx_params_t { delay_params_t delay; reverb_params_t reverb;
 *                 int safety; int master; float gate; }
 * Order per sample: delay -> reverb -> safety (HP60+LP18k cascaded
 * biquads, or bypass when safety=0) -> master level -> gate.
 *
 * Checks (ask, owner-ordered):
 *   delay  — impulse -> first echo at time_l with mix-scaled amplitude;
 *            feedback decays geometrically; tone LP attenuates a 10 kHz
 *            echo vs 100 Hz; mix 0 = dry passthrough
 *   reverb — impulse -> diffuse tail (nonzero RMS over 200..800 ms);
 *            size raises tail length/RMS; damp lowers HF tail energy;
 *            mix scales wet; width changes L/R difference
 *   bypass — delay/reverb mix 0 -> |out-in| < 1e-6
 *   safety — 50 Hz and 19 kHz sines attenuated >= 20 dB vs 440 Hz with
 *            safety on; safety off = bypass
 *   defaults — fx silent (mix 0), safety on
 * Prints "N/M passed" as the last line (harness contract). */
#include "harness.h"
#include "dsp/fx.h"
#include "preset/defaults.h"
#include <string.h>
#include <math.h>

#define SR 44100
#define N  (2 * SR)          /* 2 s renders */

/* run fx over n samples of a stereo input, collecting L output */
static void run_fx(fx_state_t *st, const fx_params_t *p,
                   const double *in, double *out, int n)
{
    fx_reset(st);
    for (int i = 0; i < n; i++) {
        float l, r;
        fx_process(st, p, (float)in[i], (float)in[i], &l, &r, (double)SR);
        out[i] = l;
    }
}

/* FX params with everything neutral: safety off (isolate delay/reverb),
 * master 100, gate open */
static fx_params_t neutral_fx(void)
{
    fx_params_t p;
    memset(&p, 0, sizeof p);
    p.safety      = 0;
    p.master      = 100;
    p.gate        = 1.0f;
    p.delay.time_l   = 0.0f;
    p.delay.time_r   = 0.0f;
    p.delay.feedback = 0;
    p.delay.tone     = 20000;
    p.delay.mix      = 0;
    p.reverb.size  = 0;
    p.reverb.damp  = 0;
    p.reverb.mix   = 0;
    p.reverb.width = 0;
    return p;
}

static void fill_impulse(double *x, int n)
{
    memset(x, 0, (size_t)n * sizeof x[0]);
    x[0] = 1.0;
}

/* mono sine generator */
static void fill_sine(double *x, int n, double f)
{
    for (int i = 0; i < n; i++)
        x[i] = sin(2.0 * OPHIS_PI * f * (double)i / (double)SR);
}

/* first index where |x| > thresh */
static int first_above(const double *x, int n, double thresh)
{
    for (int i = 0; i < n; i++)
        if (fabs(x[i]) > thresh) return i;
    return -1;
}

/* peak |x[i]| in [a, b) */
static double peak_in(const double *x, int a, int b)
{
    double m = 0.0;
    for (int i = a; i < b && i < N; i++)
        if (fabs(x[i]) > m) m = fabs(x[i]);
    return m;
}

/* rms over [a, b) */
static double rms_in(const double *x, int a, int b)
{
    double s = 0.0;
    int n = b - a;
    if (n <= 0) return 0.0;
    for (int i = a; i < b; i++) s += x[i] * x[i];
    return sqrt(s / (double)n);
}

int main(void)
{
    static double in[N], outL[N], outR[N];
    fx_state_t st;
    fx_params_t p;

    /* ---------------- delay ---------------- */

    /* 1. impulse -> first echo at time_l with mix-scaled amplitude.
     * time_l = 0.1 s = 4410 samples @44100; mix 100 = full wet;
     * feedback 0, tone open (20 kHz). Echo at ~4410, amplitude ~1.0. */
    p = neutral_fx();
    p.delay.time_l = 0.1f; p.delay.time_r = 0.1f;
    p.delay.mix = 100;
    fill_impulse(in, N);
    run_fx(&st, &p, in, outL, N);
    {
        int first = first_above(outL, N, 0.1);
        int expect = (int)(0.1 * SR);
        CHECK(first >= expect - 1 && first <= expect + 1);
        double pk = peak_in(outL, expect - 1, expect + 2);
        CHECK(pk > 0.9 && pk < 1.1);          /* mix-scaled: full wet */
    }

    /* 2. feedback decays geometrically: feedback 50 -> each echo is
     * half the previous. Echoes at 4410 / 8820 / 13230. */
    p = neutral_fx();
    p.delay.time_l = 0.1f; p.delay.time_r = 0.1f;
    p.delay.feedback = 50;
    p.delay.mix = 100;
    fill_impulse(in, N);
    run_fx(&st, &p, in, outL, N);
    {
        double a1 = peak_in(outL, 4409, 4412);
        double a2 = peak_in(outL, 8819, 8822);
        double a3 = peak_in(outL, 13229, 13232);
        CHECK(a1 > 0.9);                       /* first echo ~unity */
        CHECK(a2 > 0.4 * a1 && a2 < 0.6 * a1); /* ~0.5 * a1 */
        CHECK(a3 > 0.2 * a1 && a3 < 0.3 * a1); /* ~0.25 * a1 */
    }

    /* 3. tone LP attenuates a 10 kHz echo vs 100 Hz. tone = 2 kHz;
     * short delay (0.05 s) so the wet RMS is dominated by echoed
     * content; ratio of wet RMS (10 kHz input / 100 Hz input) < 0.5. */
    {
        p = neutral_fx();
        p.delay.time_l = 0.05f; p.delay.time_r = 0.05f;
        p.delay.feedback = 0;
        p.delay.tone = 2000;
        p.delay.mix = 100;
        fill_sine(in, N, 100.0);
        run_fx(&st, &p, in, outL, N);
        double rms_lo = rms_in(outL, SR / 2, N);      /* steady tail */
        fill_sine(in, N, 10000.0);
        run_fx(&st, &p, in, outL, N);
        double rms_hi = rms_in(outL, SR / 2, N);
        CHECK(rms_lo > 1e-3);                  /* 100 Hz passes the LP */
        CHECK(rms_hi < 0.5 * rms_lo);          /* 10 kHz attenuated */
    }

    /* 4. mix 0 = dry passthrough: nonzero delay AND reverb settings,
     * both mixes 0 -> |out - in| < 1e-6 for every sample (also covers
     * the bypass ask). */
    p = neutral_fx();
    p.delay.time_l = 0.37f; p.delay.time_r = 0.51f;
    p.delay.feedback = 35; p.delay.tone = 4500; p.delay.mix = 0;
    p.reverb.size = 85; p.reverb.damp = 35; p.reverb.mix = 0;
    p.reverb.width = 100;
    fill_sine(in, N, 440.0);
    run_fx(&st, &p, in, outL, N);
    {
        double maxerr = 0.0;
        for (int i = 0; i < N; i++) {
            double e = fabs(outL[i] - in[i]);
            if (e > maxerr) maxerr = e;
        }
        CHECK(maxerr < 1e-6);
    }

    /* ---------------- reverb ---------------- */

    /* 5. impulse -> diffuse tail: nonzero RMS over 200..800 ms. */
    p = neutral_fx();
    p.reverb.size = 80; p.reverb.damp = 30;
    p.reverb.mix = 100; p.reverb.width = 100;
    fill_impulse(in, N);
    run_fx(&st, &p, in, outL, N);
    double tail_big = rms_in(outL, (int)(0.2 * SR), (int)(0.8 * SR));
    CHECK(tail_big > 1e-5);

    /* 6. size raises tail length/RMS: size 90 tail > size 10 tail. */
    p = neutral_fx();
    p.reverb.size = 10; p.reverb.damp = 30;
    p.reverb.mix = 100; p.reverb.width = 100;
    fill_impulse(in, N);
    run_fx(&st, &p, in, outL, N);
    double tail_small = rms_in(outL, (int)(0.2 * SR), (int)(0.8 * SR));
    CHECK(tail_big > tail_small);
    CHECK(tail_small > 0.0);                   /* some tail even small */

    /* 7. damp lowers HF TAIL energy: goertzel 6 kHz over the late tail
     * window [0.4, 1.0] s (probe data: damp 10 -> 1.9e-5, damp 90 ->
     * 1.8e-6 there; the onset window is damp-insensitive). */
    {
        p = neutral_fx();
        p.reverb.size = 80; p.reverb.damp = 10;
        p.reverb.mix = 100; p.reverb.width = 100;
        fill_impulse(in, N);
        run_fx(&st, &p, in, outL, N);
        double hf_open = h_goertzel_mag(outL + (int)(0.4 * SR),
                                        (int)(0.6 * SR), 6000.0,
                                        (double)SR);
        p.reverb.damp = 90;
        run_fx(&st, &p, in, outL, N);
        double hf_damped = h_goertzel_mag(outL + (int)(0.4 * SR),
                                          (int)(0.6 * SR), 6000.0,
                                          (double)SR);
        CHECK(hf_open > 0.0);
        CHECK(hf_damped < hf_open);
    }

    /* 8. mix scales wet: RMS(mix 50) ~ 0.5 * RMS(mix 100) over the
     * whole render (linear wet/dry crossfade). */
    {
        p = neutral_fx();
        p.reverb.size = 80; p.reverb.damp = 30; p.reverb.mix = 100;
        p.reverb.width = 100;
        fill_impulse(in, N);
        run_fx(&st, &p, in, outL, N);
        double w100 = h_rms(outL, N);
        p.reverb.mix = 50;
        run_fx(&st, &p, in, outL, N);
        double w50 = h_rms(outL, N);
        CHECK(w50 > 0.35 * w100 && w50 < 0.65 * w100);
    }

    /* 9. width changes L/R difference: run both channels, RMS(L-R) at
     * width 0 < width 100. */
    {
        p = neutral_fx();
        p.reverb.size = 80; p.reverb.damp = 30; p.reverb.mix = 100;
        p.reverb.width = 100;
        fill_impulse(in, N);
        run_fx(&st, &p, in, outL, N);
        /* rerun collecting R */
        fx_reset(&st);
        for (int i = 0; i < N; i++) {
            float l, r;
            fx_process(&st, &p, (float)in[i], (float)in[i], &l, &r,
                       (double)SR);
            outR[i] = r;
        }
        double diff[2 * SR];
        for (int i = 0; i < N; i++) diff[i] = outL[i] - outR[i];
        double d100 = h_rms(diff, N);

        p.reverb.width = 0;
        run_fx(&st, &p, in, outL, N);
        fx_reset(&st);
        for (int i = 0; i < N; i++) {
            float l, r;
            fx_process(&st, &p, (float)in[i], (float)in[i], &l, &r,
                       (double)SR);
            outR[i] = r;
        }
        for (int i = 0; i < N; i++) diff[i] = outL[i] - outR[i];
        double d0 = h_rms(diff, N);
        CHECK(d0 < d100);
    }

    /* 9b. STABILITY GRID (owner order; the shipped presets crashed the
     * sound system): the reverb comb loop must be bounded for EVERY
     * size/damp combination AND the shipped preset values — a constant
     * input for 2 s must stay finite and < 1e3 (an unstable comb loop
     * doubles per cycle and explodes). */
    {
        int unstable = 0;
        for (int s10 = 0; s10 <= 10; s10++) {
            for (int d10 = 0; d10 <= 10; d10++) {
                p = neutral_fx();
                p.reverb.size = s10 * 10;
                p.reverb.damp = d10 * 10;
                p.reverb.mix  = 100;
                p.reverb.width = 100;
                p.master = 100;
                p.gate = 1.0f;
                fx_reset(&st);
                double peak = 0.0;
                int bad = 0;
                for (int i = 0; i < 2 * SR; i++) {
                    float l, r;
                    fx_process(&st, &p, 1.0f, 1.0f, &l, &r, (double)SR);
                    if (!isfinite(l) || !isfinite(r) ||
                        fabsf(l) > 1e3f || fabsf(r) > 1e3f) { bad = 1; break; }
                    if (fabsf(l) > peak) peak = fabsf(l);
                }
                if (bad) {
                    unstable++;
                    printf("  UNSTABLE size=%d damp=%d peak=%.1f\n",
                           s10 * 10, d10 * 10, peak);
                }
            }
        }
        CHECK(unstable == 0);
    }

    /* 9c. shipped preset FX values (drone-220 etc.): bounded. */
    {
        p = neutral_fx();
        p.delay.time_l = 0.38f; p.delay.time_r = 0.51f;
        p.delay.feedback = 35;  p.delay.tone = 4500; p.delay.mix = 30;
        p.reverb.size = 85; p.reverb.damp = 35;
        p.reverb.mix = 30;  p.reverb.width = 100;
        p.master = 40; p.gate = 1.0f;
        fx_reset(&st);
        int bad = 0;
        for (int i = 0; i < 3 * SR; i++) {
            float l, r;
            fx_process(&st, &p, 0.5f, 0.5f, &l, &r, (double)SR);
            if (!isfinite(l) || !isfinite(r) ||
                fabsf(l) > 1e3f || fabsf(r) > 1e3f) { bad = 1; break; }
        }
        CHECK(bad == 0);
    }

    /* ---------------- safety filter ---------------- */

    /* 10. safety on: 50 Hz and 19 kHz sines attenuated >= 20 dB vs
     * 440 Hz (master 100, gate open, fx mixes 0 — safety is the only
     * active stage). 4th-order rolloff: HP 60 Hz + LP 18 kHz. */
    {
        p = neutral_fx();
        p.safety = 1;
        double amp[3];
        static const double freqs[3] = { 50.0, 440.0, 19000.0 };
        for (int k = 0; k < 3; k++) {
            fill_sine(in, N, freqs[k]);
            run_fx(&st, &p, in, outL, N);
            amp[k] = rms_in(outL, SR, N);      /* steady second */
        }
        double db_lo = h_db(amp[0] / amp[1]);  /* 50 Hz  vs 440 */
        double db_hi = h_db(amp[2] / amp[1]);  /* 19 kHz vs 440 */
        /* Measured with the binding HP60 + LP18k 4th-order cascade:
         * -30.6 dB @ 50 Hz, -13.8 dB @ 19 kHz (owner ruling on the step-2
         * escalation: the -20 dB @ 19 kHz threshold was over-specified —
         * unattainable 0.079 oct above an 18 kHz corner — amended to
         * -10 dB; the 18 kHz corner itself is untouched). */
        CHECK(db_lo <= -20.0);                 /* owner: keep -20 dB */
        CHECK(db_hi <= -10.0);                 /* owner-ruled threshold */
    }

    /* 11. safety off = bypass: |out - in| < 1e-6 (mixes 0, master 100,
     * gate 1 — with safety 0 the whole master stage is transparent). */
    {
        p = neutral_fx();
        p.safety = 0;
        fill_sine(in, N, 30.0);                /* sub-HP content passes */
        run_fx(&st, &p, in, outL, N);
        double maxerr = 0.0;
        for (int i = 0; i < N; i++) {
            double e = fabs(outL[i] - in[i]);
            if (e > maxerr) maxerr = e;
        }
        CHECK(maxerr < 1e-6);
    }

    /* 12. defaults: fx silent (both mixes 0) and safety on. */
    CHECK(OPHIS_DEFAULTS.delay.mix == 0 && OPHIS_DEFAULTS.reverb.mix == 0);
    CHECK(OPHIS_DEFAULTS.safety == 1);

    /* 13. REGRESSION (Phase 1 S1/S2, PLAN_CODE_REVIEW_FIXES.md): the
     * safety biquads are fixed-fc — they must keep the FULL
     * state-clearing set: after a render their coefficients are
     * identical to an independent biquad_set, and two identical renders
     * are bitwise identical (no filter history leaks across resets). */
    {
        p = neutral_fx();
        p.safety = 1;
        fill_sine(in, N, 440.0);
        run_fx(&st, &p, in, outL, N);
        biquad_t ref_hp, ref_lp;
        biquad_set(&ref_hp, FILT_HP, 60.0f, (double)SR, 0.7071f);
        biquad_set(&ref_lp, FILT_LP, 18000.0f, (double)SR, 0.7071f);
        CHECK(fexact(st.hp1.b0, ref_hp.b0) && fexact(st.hp1.b1, ref_hp.b1) &&
              fexact(st.hp1.b2, ref_hp.b2) && fexact(st.hp1.a1, ref_hp.a1) &&
              fexact(st.hp1.a2, ref_hp.a2));
        CHECK(fexact(st.lp1.b0, ref_lp.b0) && fexact(st.lp1.b1, ref_lp.b1) &&
              fexact(st.lp1.b2, ref_lp.b2) && fexact(st.lp1.a1, ref_lp.a1) &&
              fexact(st.lp1.a2, ref_lp.a2));

        static double outL2[N];
        run_fx(&st, &p, in, outL2, N);
        int differs = 0;
        for (int i = 0; i < N; i++)
            if (!fexact(outL[i], outL2[i])) { differs = 1; break; }
        CHECK(differs == 0);
    }

    HARNESS_REPORT();
}
