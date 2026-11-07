/* tests/test_filter.c — PLAN.md §10 (M1): LP/HP pass/stop dB at 100 Hz /
 * 10 kHz for fc = 1 kHz (Q = 0.707); BP band, BR notch at fc. */
#include "harness.h"
#include "dsp/filter.h"

#define SR 44100.0
#define NF 44100
#define WARMUP 4410

static double in[NF], outp[NF];

/* steady-state gain (linear) of mode m at fc=1 kHz, Q=q, tone ftest */
static double gain_at_q(filt_mode_t m, double ftest, float q)
{
    biquad_t b;
    biquad_set(&b, m, 1000.0f, SR, q);      /* also clears state (D7) */
    for (int i = 0; i < NF; i++) {
        in[i]  = sin(2.0 * OPHIS_PI * ftest * (double)i / SR);
        outp[i] = biquad_next(&b, in[i]);
    }
    double rms_in  = h_rms(in + WARMUP, NF - WARMUP);
    double rms_out = h_rms(outp + WARMUP, NF - WARMUP);
    return rms_out / rms_in;
}

int main(void)
{
    double g100  = gain_at_q(FILT_LP, 100.0, 0.707f);
    double g1000 = gain_at_q(FILT_LP, 1000.0, 0.707f);
    double g10k  = gain_at_q(FILT_LP, 10000.0, 0.707f);
    /* LP: passes 100 Hz (~unity), stops 10 kHz (2nd order ≈ −40 dB) */
    CHECK(h_db(g100) >= -1.0);
    CHECK(h_db(g1000) >= -3.5 && h_db(g1000) <= 1.0);  /* half-power @fc */
    CHECK(h_db(g10k) <= -25.0);

    g100 = gain_at_q(FILT_HP, 100.0, 0.707f);
    g10k = gain_at_q(FILT_HP, 10000.0, 0.707f);
    /* HP: stops 100 Hz, passes 10 kHz */
    CHECK(h_db(g100) <= -25.0);
    CHECK(h_db(g10k) >= -1.0);

    /* BP: band at fc, both decades attenuated */
    double bp_fc  = gain_at_q(FILT_BP, 1000.0, 0.707f);
    double bp_lo  = gain_at_q(FILT_BP, 100.0, 0.707f);
    double bp_hi  = gain_at_q(FILT_BP, 10000.0, 0.707f);
    CHECK(h_db(bp_fc) >= -3.0);
    CHECK(h_db(bp_lo) <= -10.0);
    CHECK(h_db(bp_hi) <= -10.0);

    /* BR: notch at fc, near-unity away from it */
    double br_fc  = gain_at_q(FILT_BR, 1000.0, 0.707f);
    double br_lo  = gain_at_q(FILT_BR, 100.0, 0.707f);
    double br_hi  = gain_at_q(FILT_BR, 10000.0, 0.707f);
    CHECK(h_db(br_fc) <= -20.0);
    CHECK(h_db(br_lo) >= -1.0);
    CHECK(h_db(br_hi) >= -1.0);

    /* full set clears history: no output from stale state (D7: the
     * clear lives on biquad_set — biquad_reset is gone) */
    biquad_t b;
    biquad_set(&b, FILT_LP, 1000.0f, SR, 0.707f);
    for (int i = 0; i < 100; i++) (void)biquad_next(&b, 0.3);
    biquad_set(&b, FILT_LP, 1000.0f, SR, 0.707f);
    CHECK(biquad_next(&b, 0.0) == 0.0);

    /* ---- resonance (filter expansion, PLAN §5): higher Q raises the
     * measured gain at fc, same topology ---- */
    {
        double lp_lo_q = gain_at_q(FILT_LP, 1000.0, 0.707f);
        double lp_hi_q = gain_at_q(FILT_LP, 1000.0, 4.0f);
        CHECK(h_db(lp_hi_q) > h_db(lp_lo_q) + 3.0);
    }

    /* ---- coefficient/state split (Phase 1 S1, PLAN_CODE_REVIEW_FIXES
     * .md): biquad_set_coefs computes + normalizes the coefficients and
     * MUST NOT touch z1/z2 (the per-sample ENV-cutoff path updates
     * coefficients while the filter keeps ringing); biquad_set = set_coefs
     * + state clear; the cascade helper keeps its old contract ---- */
    {
        biquad_t b;
        biquad_set(&b, FILT_LP, 1000.0f, SR, 0.707f);
        for (int i = 0; i < 64; i++) (void)biquad_next(&b, 0.3);
        CHECK(b.z1 != 0.0 || b.z2 != 0.0);        /* state built up */
        double z1 = b.z1, z2 = b.z2;

        /* set_coefs: coefficients change, state is preserved */
        biquad_set_coefs(&b, FILT_LP, 3000.0f, SR, 2.0f);
        CHECK(b.z1 == z1 && b.z2 == z2);
        biquad_t r3000;
        biquad_set(&r3000, FILT_LP, 3000.0f, SR, 2.0f);
        CHECK(b.b0 == r3000.b0 && b.b1 == r3000.b1 && b.b2 == r3000.b2 &&
              b.a1 == r3000.a1 && b.a2 == r3000.a2);

        /* set_coefs matches biquad_set's coefficients for ANY mode */
        biquad_set_coefs(&b, FILT_BP, 800.0f, SR, 3.0f);
        biquad_set(&r3000, FILT_BP, 800.0f, SR, 3.0f);
        CHECK(b.b0 == r3000.b0 && b.b1 == r3000.b1 && b.b2 == r3000.b2 &&
              b.a1 == r3000.a1 && b.a2 == r3000.a2);

        /* biquad_set still clears the state (after streaming) */
        for (int i = 0; i < 64; i++) (void)biquad_next(&r3000, 0.5);
        CHECK(r3000.z1 != 0.0 || r3000.z2 != 0.0);
        biquad_set(&r3000, FILT_BP, 900.0f, SR, 3.0f);
        CHECK(r3000.z1 == 0.0 && r3000.z2 == 0.0);

        /* cascade helper unchanged: both stages get the shared
         * coefficients AND zeroed state (full set clears history) */
        biquad_t s1, s2;
        biquad_cascade_set(&s1, &s2, FILT_LP, 1000.0f, SR, 0.707f);
        biquad_set(&r3000, FILT_LP, 1000.0f, SR, 0.707f);
        CHECK(s1.b0 == r3000.b0 && s1.b1 == r3000.b1 && s1.b2 == r3000.b2 &&
              s1.a1 == r3000.a1 && s1.a2 == r3000.a2 &&
              s1.z1 == 0.0 && s1.z2 == 0.0);
        CHECK(s2.b0 == r3000.b0 && s2.b1 == r3000.b1 && s2.b2 == r3000.b2 &&
              s2.a1 == r3000.a1 && s2.a2 == r3000.a2 &&
              s2.z1 == 0.0 && s2.z2 == 0.0);
    }

    /* ---- poles 24 dB/oct (PLAN §5): cascade of two identical biquads
     * sharing one fc/Q; 10 kHz stop at fc=1 kHz is deeper than 12 dB/oct
     * (PLAN §10: ≈ 6 dB deeper; asserted as at-least-6 dB) and the cascade
     * dB is twice the single-biquad dB (the 24 dB/oct property) ---- */
    {
        float q = 0.707f;
        biquad_t b1, b2;
        biquad_set(&b1, FILT_LP, 1000.0f, SR, q);   /* full set zeroes state */
        biquad_set(&b2, FILT_LP, 1000.0f, SR, q);
        for (int i = 0; i < NF; i++) {
            in[i] = sin(2.0 * OPHIS_PI * 10000.0 * (double)i / SR);
            outp[i] = biquad_next(&b2, biquad_next(&b1, in[i]));
        }
        double g10k_cascade = h_rms(outp + WARMUP, NF - WARMUP) /
                              h_rms(in + WARMUP, NF - WARMUP);
        double g10k_single = gain_at_q(FILT_LP, 10000.0, q);
        double single_db  = h_db(g10k_single);
        double cascade_db = h_db(g10k_cascade);
        CHECK(single_db - cascade_db >= 6.0);
        CHECK(fabs(cascade_db - 2.0 * single_db) <= 3.0);
    }

    HARNESS_REPORT();
}
