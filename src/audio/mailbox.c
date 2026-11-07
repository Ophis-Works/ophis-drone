/* src/audio/mailbox.c — lock-free host <-> UI channels (IMPLEMENTATION.md
 * §2/§3). No mutex, no condvar — stdatomic only.
 *
 * Seqlock mechanics (Phase 2, H1): the parameter mailbox keeps a u64
 * sequence counter that is EVEN at rest and ODD while a publish is in
 * flight (double increment); the published slot is (seq >> 1) & 1. With a
 * u64 counter the ABA window of the old 1-bit version index is closed
 * (two commits can no longer flip the counter back under the reader).
 *
 * Fence placement (tests/test_mailbox.c caught both holes): the data copy
 * on BOTH sides compiles to a fast-string rep movs, which is exempt from
 * plain TSO ordering, and an acquire fence is a no-op instruction on
 * x86 — so a torn copy could still be accepted (the writer's counter
 * store only nanoseconds behind its slot write, the reader's reload
 * only nanoseconds behind its copy). seq_cst fences (mfence / dmb) on
 * the writer (slot write -> counter store) and the reader (copy ->
 * reload) bound both gaps below by the same fence latency F: a copy that
 * overlapped a slot overwrite ends at least F before the reload, while
 * the writer's counter store lands at least F after its slot write — the
 * reload therefore always sees the new counter and retries. */
#include <string.h>

#include "mailbox.h"

void mailbox_init(mailbox_t *mb)
{
    memset(mb, 0, sizeof *mb);
    atomic_init(&mb->seq, 0ull);
    atomic_init(&mb->trigger_req, 0);
    atomic_init(&mb->panic_req, 0);
    atomic_init(&mb->status_index, 0ull);
}

void mailbox_write(mailbox_t *mb, const synth_params_t *p)
{
    uint64_t s =
        atomic_load_explicit(&mb->seq, memory_order_relaxed);
    atomic_store_explicit(&mb->seq, s + 1, memory_order_relaxed);
    /* the odd store must be globally visible before the slot write, or
     * a reader could copy the slot while it is overwritten and still
     * reload the previous even count (ARM reorders stores; x86 TSO does
     * not) */
    atomic_thread_fence(memory_order_release);
    mb->buf[((s >> 1) + 1) & 1] = *p;      /* inactive slot */
    /* the whole slot write must be globally visible before the counter
     * store below, or a reader copying the slot accepts a torn snapshot
     * whose reload still sees the old even count (fast-string copies are
     * exempt from TSO store ordering — see header) */
    atomic_thread_fence(memory_order_seq_cst);
    atomic_store_explicit(&mb->seq, s + 2, memory_order_release);
}

void mailbox_read(mailbox_t *mb, synth_params_t *out)
{
    for (;;) {
        uint64_t a =
            atomic_load_explicit(&mb->seq, memory_order_acquire);
        if (a & 1)
            continue;             /* publish in flight — retry */
        *out = mb->buf[(a >> 1) & 1];
        /* the copy must be complete before the reload below — see the
         * fence note atop this file: the reload must run at least one
         * fence latency after the last copy load, so a slot overwrite
         * inside the copy always shows up in the reload */
        atomic_thread_fence(memory_order_seq_cst);
        uint64_t b =
            atomic_load_explicit(&mb->seq, memory_order_relaxed);
        if (a == b)
            return;               /* stable and even: complete snapshot */
        /* writer published while we copied — retry with the newer half */
    }
}

void mailbox_status_publish(mailbox_t *mb, const ui_status_t *s)
{
    uint64_t i =
        atomic_load_explicit(&mb->status_index, memory_order_relaxed);
    /* write the INACTIVE slot: after this publish, slot (i+1)&1 is the
     * one index i+1 points at, so a reader never copies the slot an
     * in-flight publish is writing (the old i&1 handed the reader the
     * slot the next publish would overwrite, one-two publishes stale) */
    mb->status[(i + 1) & 1] = *s;
    /* slot write globally visible before the index store — see mailbox_
     * write / the fence note atop this file */
    atomic_thread_fence(memory_order_seq_cst);
    atomic_store_explicit(&mb->status_index, i + 1, memory_order_release);
}

int mailbox_status_read(mailbox_t *mb, ui_status_t *out)
{
    uint64_t a =
        atomic_load_explicit(&mb->status_index, memory_order_acquire);
    *out = mb->status[a & 1];
    /* copy complete before the reload — see mailbox_read: a 1 return
     * must never carry a torn snapshot (tests/test_mailbox.c caught both
     * the copy sinking past the reload and the fast-string exemption) */
    atomic_thread_fence(memory_order_seq_cst);
    uint64_t b =
        atomic_load_explicit(&mb->status_index, memory_order_relaxed);
    return a == b;
}
