/* src/dsp/chain.h — chain graph: subs -> main osc -> AM -> ADSR*vel -> biquad.
 * Declarations only. */
#ifndef OPHIS_CHAIN_H
#define OPHIS_CHAIN_H

#include <stdint.h>
#include "types.h"
#include "osc.h"
#include "subosc.h"
#include "adsr.h"
#include "filter.h"

typedef struct {
  /* double accumulators: mod_apply adds double products with a single
     rounding per term (no per-term float rounding) */
  double detune_hz, amp_mod, filter_mod;
  double phase_mod;  /* normalized PM input
                     amount/100 × m (-1..1); chain adds phase_mod × pi rad
                     to the carrier phase (A.PHASE xmod dest). */
  double res_mod;    /* FLT_RES dest: normalized resonance input
                     amount/100 × m (-1..1); the chain adds res_mod × 100
                     percent to the RES knob (clamped 0..100). 0 = the
                     static-res coefficient caches apply unchanged. */
  double env_mod;    /* FLT_ENV dest: normalized ENV AMT
                     input amount/100 × m (-1..1); the chain adds
                     env_mod × 100 percent points to the ENV AMT knob
                     (clamped ±100). */
  double adsr_mod[4]; /* ADSR A/D/S/R dests (same order): A/D/R entries
                     are ratio inputs — the stage seconds scale by
                     (1 + adsr_mod[i]), clamped 0..60 s; the S entry is
                     additive points (s + 100×adsr_mod[2], clamped
                     0..100). All zero = the plain params apply. */
} xmod_in_t;

typedef struct { int retrigger; float vel; } chain_event_t;

typedef struct {
  osc_t          osc;
  subosc_state_t sub[2];
  adsr_state_t   env;
  biquad_t       filt;
  biquad_t       filt2;       /* second cascade stage (poles=24);
                               identical cutoff/Q, shared state clear */
  float          vel;          /* current retrigger level 0..1 */
  float          f_osc;        /* current main frequency (Hz) */
  double         raw_osc_out;  /* mod source for xmod (post-wave, pre-AM) */
  double         raw_sub_out[2]; /* INTERFACE ADDITION: raw
                                    sub-osc outputs, engine feeds mixer
                                    mod-source ids 2/3/5/6. */
  float          last_fc;      /* cached biquad settings (avoid resetting
                                  the filter state every sample) */
  filt_mode_t    last_mode;
  int            last_res;     /* Q changes need a coefficient update too */
  int            last_poles;   /* 12 vs 24 dB/oct topology */
  int            last_gate;    /* previous chain gate — falling-edge release */
  double         last_pm_rad;  /* previous PM phase offset (rad): the delta
                                  offset−previous is added to the carrier
                                  phase each sample so the accumulated
                                  phase telescopes to theta + beta*m
                                  (true PM, not integrated FM) */
} chain_state_t;

/* INTERFACE ADDITION: seeds the chain's
 * PRNGs (main osc + both subs) and zeroes env/filt — the contract names no
 * constructor but deterministic noise subs need a seed. */
void chain_init(chain_state_t *c, uint64_t seed);

/* FLT_RES Q table: Q = 0.5 × 40^(pos/50) for pos 0..50, computed
 * lazily on first use (audio thread, no malloc, no lock) — bit-identical
 * to the per-sample pow expression it replaces. */
double chain_q_lut(int pos);

/* render one sample (order: detune -> wave -> AM -> env*vel*level
   -> biquad -> level/100). gate is the chain's gate: on its falling edge
   (edge kept in chain_state_t) the envelope is driven into release by
   passing gate=0 to adsr_next. */
double chain_render(chain_state_t *c, const chain_params_t *p,
                    const xmod_in_t *xm, const chain_event_t *ev, double sr,
                    int gate);

#endif /* OPHIS_CHAIN_H */
