/* src/dsp/adsr.c — sample-accurate GATE-DRIVEN A/D/S/R state machine.
 * Linear segments, exact sample counts.
 * Pure. No SDL/ALSA. */
#include <string.h>

#include "adsr.h"

/* samples for a seconds-duration stage; 0 s -> 1 sample */
static double stage_n(double seconds, double sr)
{
    double n = seconds * sr;
    return (n < 1.0) ? 1.0 : n;
}

void adsr_reset(adsr_state_t *e)
{
    memset(e, 0, sizeof *e);   /* stage IDLE (=0), level 0, prev_gate 0 */
}

/* THE one ATTACK entry (used by adsr_trigger re-entry AND adsr_next's
 * rising edge): (re-)start ATTACK from the current
 * level; gate edge state untouched. */
static void enter_attack(adsr_state_t *e)
{
    e->stage    = ADSR_ATTACK;
    e->t        = 0.0;
    e->atk_base = e->level;
}

void adsr_trigger(adsr_state_t *e)
{
    /* re-entry: ATTACK from the current level, gate edge state untouched */
    enter_attack(e);
}

float adsr_next(adsr_state_t *e, const adsr_params_t *p, double sr, int gate)
{
    /* ---- gate edge detection (single mechanism for both directions) ---- */
    if (gate && !e->prev_gate) {
        /* rising edge -> ATTACK from the current level (also from RELEASE) */
        enter_attack(e);
    } else if (!gate && e->prev_gate) {
        /* falling edge -> RELEASE from whatever stage the envelope is in */
        if (e->stage != ADSR_IDLE) {
            e->stage    = ADSR_RELEASE;
            e->t        = 0.0;
            e->rel_base = e->level;
        }
    }
    e->prev_gate = gate;

    /* ---- stage advance ---- */
    switch (e->stage) {
    case ADSR_IDLE:
        e->level = 0.0f;
        break;

    case ADSR_ATTACK: {
        double n = stage_n(p->a, sr);
        e->t += 1.0;
        e->level = (float)((double)e->atk_base
                           + (1.0 - (double)e->atk_base) * (e->t / n));
        if (e->t >= n) {
            e->level = 1.0f;
            e->stage = ADSR_DECAY;
            e->t = 0.0;
        }
        break;
    }

    case ADSR_DECAY: {
        double n = stage_n(p->d, sr);
        float target = (float)p->s / 100.0f;
        e->t += 1.0;
        e->level = (float)(1.0 - (1.0 - (double)target) * (e->t / n));
        if (e->t >= n) {
            e->level = target;
            e->stage = ADSR_SUSTAIN;
            e->t = 0.0;
        }
        break;
    }

    case ADSR_SUSTAIN:
        e->level = (float)p->s / 100.0f;
        break;

    case ADSR_RELEASE: {
        double n = stage_n(p->r, sr);
        e->t += 1.0;
        e->level = (float)((double)e->rel_base * (1.0 - e->t / n));
        if (e->t >= n) {
            e->level = 0.0f;
            e->stage = ADSR_IDLE;
            e->t = 0.0;
        }
        break;
    }
    }

    if (e->level > 1.0f) e->level = 1.0f;
    if (e->level < 0.0f) e->level = 0.0f;
    return e->level;
}
