/* src/seq/sequencer.h — sequencer contract (IMPLEMENTATION.md §2, PLAN §6).
 * Pure: sample time in, events out. No timing deps, no SDL/ALSA.
 * Headers only in M2 turn 1 (TDD: tests watched failing first).
 * Owner-ordered modes amendment (PLAN §6): seq_params_t gains mode
 * (0=fwd default, 1=pingpong, 2=oddeven, 3=random, 4=markov) and once
 * (0=loop default, 1=once); the state owns a per-instance deterministic
 * xoshiro256** PRNG (rng), the last emitted step / walk position (prev)
 * and the steps-emitted count of the current pass (pass_count, used by
 * once mode). seq_init seeds the PRNG like chain_init/osc_init.
 * BPM is GLOBAL (AMENDED, owner order, PLAN §6): seq_params_t has NO
 * bpm member — the global tempo is passed to seq_tick as an argument. */
#ifndef OPHIS_SEQUENCER_H
#define OPHIS_SEQUENCER_H

#include <stdint.h>

#include "dsp/types.h"

typedef struct {
  double   next_step_time;
  int      cursor;     /* fwd/oddeven table position / pingpong position */
  int      dir;        /* pingpong direction: +1 up, -1 down */
  uint64_t rng[4];     /* xoshiro256** state, per-instance seeded (random,
                        * markov) — IMPLEMENTATION.md amended contract */
  int      prev;       /* last emitted step (random: no immediate repeat)
                        * / markov walk position; -1 = none yet */
  int      pass_count; /* steps emitted in the current pass (once mode) */
} seq_state_t;

/* Constructor: seeds the PRNG from `seed` (splitmix64 expansion, same
 * pattern as osc_init/chain_init — deterministic per instance) and resets
 * everything else. */
void seq_init(seq_state_t *s, uint64_t seed);

/* Reset: cursor = 0, next_step_time = 0 (next boundary fires at now >= 0,
 * i.e. immediately on the first tick at t=0); walk position 0, pass
 * count 0. Does NOT reseed the PRNG (the instance keeps its stream).
 * PANIC / RUN-on / seq on-toggle restart (PLAN §6). */
void seq_reset(seq_state_t *s);

/* Advance the sequencer clock.
 * Returns the step index 0..7 when now has reached the next step boundary,
 * else -1. Step period = 60/bpm/4 * div seconds (bpm the GLOBAL tempo
 * passed by the caller — AMENDED, owner order, PLAN §6: BPM is NOT
 * per-seq, it lives in synth_params_t and is NOT in seq_params_t;
 * div = 1 -> 16th notes, PLAN §6; div is a UI param, 1..16).
 * Boundaries are scheduled incrementally: a bpm change takes effect for
 * boundaries scheduled after the currently pending one.
 * The step ORDER follows p->mode (PLAN §6):
 *   fwd (0)      0..7, wraps;
 *   pingpong (1) 0..7 then 7..0, bouncing;
 *   oddeven (2)  1-based odd indices first: 0,2,4,6 then 1,3,5,7;
 *   random (3)   deterministic per-instance PRNG, never the same step
 *                twice in a row;
 *   markov (4)   equally weighted +-1 walk from the current step,
 *                reflecting at the edges (0 -> 1, 7 -> 6), same PRNG.
 * p->once (PLAN §6): 0 = loop (repeats forever); 1 = after one pass
 * (8 emitted steps) returns -1 forever until seq_reset — which the
 * engine performs on RUN off->on, seq off->on, or PANIC.
 * p->on == 0  -> always returns -1 (no events; sequencer is a UI param,
 *                PLAN §6). Cursor does not advance.
 * The engine (caller) converts a returned index i into a chain event
 * {pitch_hz = p->steps[i].pitch_hz, vel = p->steps[i].vel} only when
 * p->steps[i].vel > 0; vel 0 = silent step, no event (PLAN §6). The engine
 * calls seq_tick only while RUN is ON. */
int seq_tick(seq_state_t *s, const seq_params_t *p, int bpm, double now,
             double sr);

#endif /* OPHIS_SEQUENCER_H */
