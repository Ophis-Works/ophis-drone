/* src/seq/transport.c — RUN/TRIGGER/PANIC state machine.
 * Pure; called once per audio block. Gate ramp is
 * 10 ms; gate semantics edge-compatible with the chain/mixer gate
 * (1 = held). */
#include "seq/transport.h"

void transport_process(transport_state_t *t, int run, int trigger_req,
                       int panic_req, const float env_level[3],
                       double block_frames, double now, double sr)
{
    /* PANIC: instant gate 0, no ramp, one-shot cleared, deadline 0
     * (RUN cleared is the caller's run flag going forward). */
    if (panic_req) {
        t->gate              = 0.0f;
        t->one_shot          = 0;
        t->one_shot_deadline = 0.0;
        return;
    }

    /* TRIGGER: one-shot retrigger override; inert while RUN is ON except
     * that the flag is still recorded (gate already open). */
    if (trigger_req) {
        t->one_shot          = 1;
        t->one_shot_deadline = now + 30.0;   /* ABSOLUTE safety deadline */
    }

    /* One-shot clears when all envelopes reached 0 or the absolute
     * deadline timestamp is reached. NOT evaluated on the block where
     * trigger_req arrived: the engine fires the retriggers after
     * transport_process, so the envelopes are still all-zero at entry
     * and the gate would never open from idle.
     * TRIGGER opens the gate for one envelope. */
    if (t->one_shot && !trigger_req) {
        int all_zero = (env_level[0] <= 0.0f && env_level[1] <= 0.0f &&
                        env_level[2] <= 0.0f);
        if (all_zero || now >= t->one_shot_deadline) {
            t->one_shot          = 0;
            t->one_shot_deadline = 0.0;
        }
    }

    /* Gate target: open while RUN or the one-shot holds. Linear ramp,
     * full swing in 10 ms, clamped. */
    float target = (run || t->one_shot) ? 1.0f : 0.0f;
    float delta  = (float)(block_frames / (0.01 * sr));
    if (t->gate < target) {
        t->gate += delta;
        if (t->gate > target) t->gate = target;
    } else if (t->gate > target) {
        t->gate -= delta;
        if (t->gate < target) t->gate = target;
    }
}
