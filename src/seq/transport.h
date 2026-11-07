/* src/seq/transport.h — transport contract (IMPLEMENTATION.md §2, PLAN §7).
 * Pure. Called once per audio block by the engine.
 * Headers only in M2 turn 1 (TDD: tests watched failing first). */
#ifndef OPHIS_TRANSPORT_H
#define OPHIS_TRANSPORT_H

typedef struct {
    int    one_shot;           /* TRIGGER override: gate held open */
    float  gate;               /* 0..1, 10 ms ramp (edge-compatible with the
                                * M1 chain/mixer gate: 1 = held) */
    double one_shot_deadline;  /* ABSOLUTE timestamp in seconds on the
                                * engine clock (now + 30.0 on trigger);
                                * 0 = none */
} transport_state_t;

/* Process one block (block_frames samples at sr). now is the engine's
 * sample-clock timestamp (seconds) for the block being processed.
 * run:         RUN button state this block (0/1).
 * trigger_req: TRIGGER event flag this block (consumed by caller).
 * panic_req:   PANIC event flag this block (consumed by caller).
 * env_level:   the 3 chain envelope levels (caller reads the ADSRs).
 *
 * gate target = (run || one_shot); gate moves linearly toward the target,
 * full swing in 10 ms (delta = block_frames / (0.01 * sr) per call,
 * clamped). one_shot is set by trigger_req (one-shot: exactly one
 * retrigger per 0->1 edge — the engine fires the 3 chains at their VEL on
 * that edge, and one_shot_deadline is set to the ABSOLUTE timestamp
 * now + 30.0) and cleared when all env_level[] == 0 or when
 * now >= one_shot_deadline (force-close, deadline then set to 0). While
 * RUN is ON the one_shot flag is inert (gate already open).
 * panic_req: gate = 0 INSTANTLY (no ramp), one_shot = 0,
 * one_shot_deadline = 0, RUN treated as OFF (RUN cleared is the caller's
 * run flag going forward; transport holds no run state). The caller then
 * resets chain/seq state. Sequencer state is never touched here. */
void transport_process(transport_state_t *t, int run, int trigger_req,
                       int panic_req, const float env_level[3],
                       double block_frames, double now, double sr);

#endif /* OPHIS_TRANSPORT_H */
