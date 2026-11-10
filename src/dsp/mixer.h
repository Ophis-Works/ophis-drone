/* src/dsp/mixer.h — master sum + cross-mod matrix + tanh soft-clip; master
 * level and gate are applied in the FX/master stage (dsp/fx.c), NOT here.
 * Declarations only. */
#ifndef OPHIS_MIXER_H
#define OPHIS_MIXER_H

#include "types.h"
#include "chain.h"

typedef struct {
  const synth_params_t *synth;   /* the live params: xmod slots, per-
                                    chain mixvol (no copies) */
} mixer_params_t;

/* mod-source id table: the ONE source of
 * truth for ids 1..9 — id 0 is OFF (no entry). sub < 0 = the chain's
 * main osc, else the sub index. The historical ids are FROZEN (1..6 =
 * B/C, 7..9 = A appended — old presets keep their meaning). */
typedef struct { int chain; int sub; } modsrc_t;
#define MODSRC_COUNT 9
const modsrc_t *modsrc_get(int id);

typedef struct {
  double    src[9];        /* raw mod sources (ids 1..9), set by engine */
  xmod_in_t xm[3];         /* out: per-chain modulation computed by
                              mixer_render from p->xmod + src[]
                              (INTERFACE ADDITION: xm[] exposed in the state
                              so the engine/tests can feed chain_render). */
} mixer_state_t;

/* the mixer is TWO roles, split —
 *
 *  matrix_render: computes m->xm[0..2] from the slots and m->src (the
 *      per-chain modulation the engine feeds each chain next sample),
 *      applies the RING/MIX slot functions to their targets, and
 *      writes the three post-slot outputs to post[].
 *  master_sum: pure tanh(1.5 * sum(mixvol[c]/100 * post[c])).
 *
 * mixer_render is the builder composition of both (same contract as
 * before the split); master level + gate are applied in fx_process. */
void matrix_render(mixer_state_t *m, const mixer_params_t *p,
                   const double chain_out[3], double post[3]);
double master_sum(const mixer_params_t *p, const double post[3]);
double mixer_render(mixer_state_t *m, const mixer_params_t *p,
                    const double chain_out[3], double sr);

#endif /* OPHIS_MIXER_H */
