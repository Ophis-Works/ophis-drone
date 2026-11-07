/* src/dsp/adsr.h — sample-accurate GATE-DRIVEN A/D/S/R state machine.
 * Contract: IMPLEMENTATION.md §2 (adsr_reset/adsr_trigger/adsr_next only).
 * Pure C11. Declarations only. */
#ifndef OPHIS_ADSR_H
#define OPHIS_ADSR_H

#include "types.h"

typedef enum { ADSR_IDLE, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN,
               ADSR_RELEASE } adsr_stage_t;

typedef struct {
  adsr_stage_t stage;
  double t;        /* samples elapsed in current stage */
  float level;     /* 0..1 */
  int prev_gate;   /* last gate seen by adsr_next — edge detection */
  float atk_base;  /* level at attack entry (attack from current level) */
  float rel_base;  /* level at release entry (linear release) */
} adsr_state_t;

void  adsr_reset(adsr_state_t *e);      /* -> IDLE, level 0 */
void  adsr_trigger(adsr_state_t *e);    /* re-enter ATTACK from the current
                                           level (gate stays as-is) */
/* Gate-driven per-sample advance. Rising edge (0->1) enters ATTACK from
 * the current level; gate held runs ATTACK -> DECAY -> SUSTAIN; falling
 * edge (1->0) enters RELEASE from whatever stage the envelope is in and
 * RELEASE runs to zero. There is no separate release entry point. */
float adsr_next(adsr_state_t *e, const adsr_params_t *p, double sr, int gate);

#endif /* OPHIS_ADSR_H */
