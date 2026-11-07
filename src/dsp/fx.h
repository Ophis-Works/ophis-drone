/* src/dsp/fx.h — master-path FX + safety filter + master level + gate.
 * Owner-ordered FX amendment (PLAN §2/§5): per sample the order is
 *   delay -> reverb -> SAFETY (HP 60 Hz + LP 18 kHz cascaded biquads,
 *   or bypass) -> master level -> master gate.
 * The mixer ends at tanh(...); the gate is applied HERE, per sample,
 * NOT in mixer_render (amended contract, PLAN §5).
 * Fixed preallocated buffers only — no malloc in the RT path.
 * Contract: IMPLEMENTATION.md §2. Declarations only. */
#ifndef OPHIS_FX_H
#define OPHIS_FX_H

#include "types.h"
#include "filter.h"

/* delay lines are fixed 2 s at 44100 (static-size arrays inside
 * fx_state_t; at higher sample rates the effective maximum time
 * shrinks — time_s * sr is clamped to FX_MAX_DELAY_SAMPLES) */
#define FX_MAX_DELAY_SAMPLES (2 * 44100)

typedef struct {
  delay_params_t   delay;    /* seconds / percent / Hz / percent */
  reverb_params_t  reverb;   /* percent x4 */
  int              safety;   /* 1 = ON (default), 0 = bypass */
  int              master;   /* percent 0..100 (applied after safety) */
  float            gate;     /* 0..1 from the transport, applied here */
} fx_params_t;

/* reverb bank cells (buffer length = max tuning 1617 + spread 23) */
#define FX_BUF_LEN 1640
typedef struct { float buf[FX_BUF_LEN]; int idx; int len; float store; }
        fx_comb_t;
typedef struct { float buf[FX_BUF_LEN]; int idx; int len; } fx_ap_t;

typedef struct {
  /* --- stereo delay: 2 s lines @44100, ring buffers --- */
  float dl[FX_MAX_DELAY_SAMPLES];
  float dr[FX_MAX_DELAY_SAMPLES];
  int   wpos;
  float tone_z1_l, tone_z1_r;   /* one-pole LP state in the loop */
  float tone_a;                 /* one-pole coefficient */
  int   last_tone;

  /* --- reverb: Freeverb-class (8 combs + 4 allpasses per channel),
   *     damping one-pole LP inside the comb loop, size -> comb
   *     feedback (tail), width -> mid/side spread on the wet pair. --- */
  fx_comb_t combL[8], combR[8];
  fx_ap_t   apL[4], apR[4];
  float rv_fb;                    /* comb feedback (from size) */
  float rv_damp1;                 /* damping LP coefficient (from damp) */
  int   last_size, last_damp;

  /* --- safety filter: cascaded RBJ biquads, HP 60 Hz + LP 18 kHz --- */
  biquad_t hp1, hp2, lp1, lp2;
  int      safety_dirty;          /* (re)set coefficients lazily */
} fx_state_t;

/* Initialize / PANIC-reset: clears every buffer and filter state.
 * Doubles as the one-time initializer (call before first use). */
void fx_reset(fx_state_t *fx);

/* One stereo sample through the master stage. out_l / out_r receive the
 * processed pair ("stereo out" of the owner-ordered signature). */
void fx_process(fx_state_t *fx, const fx_params_t *p,
                float in_l, float in_r, float *out_l, float *out_r,
                double sr);

#endif /* OPHIS_FX_H */
