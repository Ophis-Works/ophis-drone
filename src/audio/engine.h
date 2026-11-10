/* src/audio/engine.h — audio-thread body.
 * Pure except that it consumes the module contracts; the caller (audio
 * thread loop) does the mailbox read into e->p and the flag fetch-exchange
 * per §2 signature — engine_render_block takes the already-consumed
 * trigger/panic flags. */
#ifndef OPHIS_ENGINE_H
#define OPHIS_ENGINE_H

#include "../dsp/types.h"
#include "../dsp/chain.h"
#include "../dsp/mixer.h"
#include "../dsp/fx.h"
#include "../seq/sequencer.h"
#include "../seq/transport.h"

typedef struct {
  double            sr, now;   /* sample rate; engine sample clock (s) */
  chain_state_t     ch[3];
  seq_state_t       sq[3];
  transport_state_t tr;
  mixer_state_t     mx;
  fx_state_t        fx;        /* master-path FX stage (after mixer, before
                                * master level / gate) */
  synth_params_t    p;
  int               prev_gate; /* INTERFACE ADDITION: block
                                * gate edge detection for direct-mode
                                * retrigger ("gate rising"). */
  int               prev_run;       /* RUN edge: off->on restarts once-mode
                                   * seqs */
  int               prev_seq_on[3]; /* seq on edge: off->on restarts that
                                    * once-mode seq */
  int               cur_step[3];   /* seq step window: the step whose time
                                   * window the
                                   * engine is in — its pitch_hz plays
                                   * for the WHOLE window (vel 0 steps
                                   * change the pitch, they just do not
                                   * retrigger); seq OFF clears
                                   * cur_valid (direct mode = the osc
                                   * freq param) */
  int               cur_valid[3];
} engine_t;

void engine_init(engine_t *e, double sr);

/* Render one block (frames frames, stereo interleaved). trigger_req /
 * panic_req are the already-fetch-exchanged event flags; retrig_mask
 * (bit c = chain c) retrigs ONLY those chains at their own chain vel
 * — the value-key edit path (changing one
 * parameter must not retrig the whole synth). */
void engine_render_block(engine_t *e, float *out_stereo, int frames,
                         int trigger_req, int panic_req, int retrig_mask);

/* PANIC: envs -> IDLE, phases -> 0, biquads reset, seq clocks reset. */
void engine_panic_reset(engine_t *e);

/* THE live step the UI is fed, per chain: the step whose time window is
 * currently playing (cur_step/cur_valid), -1 when nothing plays (seq
 * off, before the first boundary, after PANIC). Mode-INDEPENDENT by
 * construction — never the sequencer's order cursor, which only moves
 * in fwd/pingpong/oddeven (never the order cursor). */
void engine_seq_steps(const engine_t *e, int out[3]);

#endif /* OPHIS_ENGINE_H */
