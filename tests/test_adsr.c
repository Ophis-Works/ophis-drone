/* tests/test_adsr.c — PLAN.md §10 (M1), gate-driven model:
 * gate rising edge -> ATTACK from current level; gate held -> A/D/S;
 * gate falling edge (from any stage) -> RELEASE to zero at exact sample
 * counts; retrigger via adsr_trigger() while held re-enters ATTACK;
 * gate rise during RELEASE re-enters ATTACK from the current level;
 * reset -> 0; envelope in [0,1] everywhere. */
#include <string.h>
#include "harness.h"
#include "dsp/adsr.h"

#define SR 44100.0

/* a = 1000 samples, d = 2000 samples, s = 50%, r = 1000 samples at 44.1 kHz */
static adsr_params_t mkp(void)
{
    adsr_params_t p;
    /* the params are float by design: the double quotient rounds once,
     * intentionally (documented per -Wconversion rule) */
    p.a = (float)(1000.0 / SR);
    p.d = (float)(2000.0 / SR);
    p.s = 50;
    p.r = (float)(1000.0 / SR);
    return p;
}

int main(void)
{
    adsr_params_t p = mkp();
    adsr_state_t e;
    float lv[7000];
    float mn = 1e9f, mx = -1e9f;

    /* ---- full envelope shape at exact sample counts, gate held high ---- */
    adsr_reset(&e);
    for (int n = 0; n < 5000; n++) {
        lv[n] = adsr_next(&e, &p, SR, 1);
        if (lv[n] < mn) mn = lv[n];
        if (lv[n] > mx) mx = lv[n];
    }
    /* attack: linear from 0, peak at sample 999 (1000 attack samples) */
    CHECK(fabs(lv[0]   - 0.001) < 0.01);
    CHECK(fabs(lv[499] - 0.5)   < 0.02);
    CHECK(lv[998] < lv[999]);                 /* still rising at the peak */
    CHECK(fabs(lv[999] - 1.0)   < 0.02);
    /* decay: 2000 samples down to sustain 0.5 (linear) */
    CHECK(fabs(lv[1999] - 0.75) < 0.03);
    CHECK(fabs(lv[2999] - 0.5)  < 0.02);
    /* sustain held while gate stays high */
    CHECK(fabs(lv[4000] - 0.5)  < 0.02);
    CHECK(fabs(lv[4999] - 0.5)  < 0.02);
    /* bounds */
    CHECK(mx <= 1.0f);
    CHECK(mn >= 0.0f);

    /* ---- release from SUSTAIN via gate falling edge (1 -> 0) ---- */
    /* at sample 5000 level = 0.5; release is linear over 1000 samples */
    for (int n = 5000; n < 6600; n++) {
        lv[n] = adsr_next(&e, &p, SR, 0);
        if (lv[n] < mn) mn = lv[n];
        if (lv[n] > mx) mx = lv[n];
    }
    CHECK(fabs(lv[5499] - 0.25) < 0.02);       /* halfway down */
    CHECK(lv[5999] < 0.02f);                   /* 1000 release samples done */
    CHECK(fexactf(lv[6599], 0.0f));            /* finished holds zero */
    CHECK(mx <= 1.0f && mn >= 0.0f);

    /* ---- release from ATTACK via gate falling edge mid-attack ---- */
    adsr_reset(&e);
    for (int n = 0; n < 500; n++) lv[n] = adsr_next(&e, &p, SR, 1);
    CHECK(fabs(lv[499] - 0.5) < 0.02);         /* gate=1 for 500 samples */
    for (int n = 500; n < 1700; n++) lv[n] = adsr_next(&e, &p, SR, 0);
    CHECK(fabs(lv[999]  - 0.25) < 0.02);       /* linear from level .5 */
    CHECK(lv[1499] < 0.02f);                   /* zero 1000 samples after edge */
    CHECK(fexactf(lv[1699], 0.0f));
    for (int n = 500; n < 1700; n++) CHECK(lv[n] >= 0.0f && lv[n] <= 1.0f);

    /* ---- retrigger while gate held: re-enters ATTACK from current level ---- */
    /* e is idle/zero after the previous section; run to sustain, retrigger
     * at sustain, expect level to climb again */
    adsr_reset(&e);
    for (int n = 0; n < 4000; n++) lv[n] = adsr_next(&e, &p, SR, 1);
    CHECK(fabs(lv[3999] - 0.5) < 0.02);        /* in sustain */
    adsr_trigger(&e);                          /* re-entry, gate still high */
    float a0 = adsr_next(&e, &p, SR, 1);
    float a1 = adsr_next(&e, &p, SR, 1);
    CHECK(a0 >= 0.5f);                         /* attack from current level */
    CHECK(a1 > a0);                            /* climbing again */

    /* ---- gate rise during RELEASE re-enters ATTACK from current level ---- */
    adsr_reset(&e);
    for (int n = 0; n < 4000; n++) lv[n] = adsr_next(&e, &p, SR, 1);
    for (int n = 0; n < 500; n++) lv[n] = adsr_next(&e, &p, SR, 0);
    /* released from 0.5 for 500 of 1000 samples -> around 0.25 */
    float pre = lv[499];
    CHECK(fabs(pre - 0.25) < 0.03);
    float r0 = adsr_next(&e, &p, SR, 1);       /* rising edge mid-release */
    float r1 = adsr_next(&e, &p, SR, 1);
    CHECK(r0 >= pre);                          /* attack from current level */
    CHECK(r1 > r0);                            /* climbing, not falling */
    /* and it keeps climbing through attack rather than dropping to zero */
    float r2 = adsr_next(&e, &p, SR, 1);
    CHECK(r2 >= r1);

    /* ---- reset -> 0 ---- */
    adsr_reset(&e);
    CHECK(adsr_next(&e, &p, SR, 1) > 0.0f);    /* gate rises -> attack */
    adsr_reset(&e);
    CHECK(fexactf(adsr_next(&e, &p, SR, 0), 0.0f));   /* reset -> idle, level 0 */

    /* ---- Phase 5 item 6 (PLAN_CODE_REVIEW_FIXES.md): adsr_trigger()
     * mid-RELEASE restarts the attack EXACTLY like a gate rising edge —
     * sample-exact equality over the whole attack (both entry paths must
     * share ONE mechanism) ---- */
    adsr_reset(&e);
    for (int n = 0; n < 4000; n++) adsr_next(&e, &p, SR, 1);
    for (int n = 0; n < 500; n++) adsr_next(&e, &p, SR, 0);
    adsr_state_t e_trig, e_edge;               /* same mid-release start */
    memcpy(&e_trig, &e, sizeof e);
    memcpy(&e_edge, &e, sizeof e);
    adsr_trigger(&e_trig);                     /* re-entry, gate stays low */
    for (int n = 0; n < 1200; n++) {           /* attack = 1000 samples,
                                                  crosses into decay */
        float a = adsr_next(&e_trig, &p, SR, 0);   /* gate held low */
        float b = adsr_next(&e_edge, &p, SR, 1);   /* rising edge */
        CHECK(fexactf(a, b));   /* sample-exact equality is the pin */
    }

    HARNESS_REPORT();
}
