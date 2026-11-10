/* src/audio/mailbox.h — lock-free host <-> UI channels (IMPLEMENTATION.md
 * §2/§3, PLAN §4). BINDING: no mutex, no condvar anywhere between the
 * host (engine + audio) and UI threads. Three channels:
 *   1. parameter mailbox (UI -> host): double-buffered synth_params_t
 *      behind a u64 seqlock (mailbox.c, H1): the counter is even at rest
 *      and odd while a publish is in flight; slot = (seq >> 1) & 1; the
 *      writer publishes with release, the reader retries until the
 *      counter is stable and even. The u64 counter closes the ABA window
 *      of the old 1-bit version (two commits could flip it back under
 *      the reader and a torn snapshot was accepted). Worst case one
 *      stale block (256 frames ≈ 5.8 ms) — documented, acceptable.
 *   2. event flags (UI -> host): trigger_req / panic_req, atomic
 *      fetch-exchange by the host at block start. RUN travels inside the
 *      mailbox params (p.run).
 *   3. status snapshot (host -> UI): two preallocated ui_status_t slots +
 *      _Atomic uint64 index, store-release / load-acquire. The writer
 *      fills the slot the new index will point at (inactive slot), the
 *      reader fences its copy before reloading the index, so a 1 return
 *      is never a torn snapshot.
 * Pure C11 stdatomic. The seqlock internals (counter values, parity,
 * slot choice) are private to mailbox.c; callers only see the functions
 * below. */
#ifndef OPHIS_MAILBOX_H
#define OPHIS_MAILBOX_H

#include <stdint.h>
#include <stdatomic.h>

#include "../dsp/types.h"

/* Live status published by the host thread (IMPLEMENTATION.md §6). */
typedef struct {
  int   run;              /* RUN state as the transport saw it */
  float gate;             /* master gate 0..1 */
  float env[3];           /* chain envelope levels 0..1 */
  int   step[3];          /* LIVE step per chain — the engine's step
                             window (engine_seq_steps), 0..7, or -1 when
                             nothing plays. Mode-INDEPENDENT (B-SEQ1,
                             owner order 2026-11-05): never the
                             sequencer's order cursor. */
} ui_status_t;

typedef struct {
  synth_params_t     buf[2];         /* double buffer */
  _Atomic uint64_t   seq;            /* seqlock: even = stable, odd =
                                        publish in flight; private to
                                        mailbox.c (Phase 2, H1) */
  _Atomic int        trigger_req;    /* UI sets 1; host fetch-exchange 0 */
  _Atomic int        panic_req;      /* UI sets 1; host fetch-exchange 0 */
  _Atomic int        retrig_req;     /* UI ORs bit c = chain c retrigs at
                                      * its own chain vel (value-key edit
                                      * path, owner order 2026-11-06);
                                      * host fetch-exchange 0 */
  ui_status_t        status[2];      /* preallocated snapshot slots */
  _Atomic uint64_t   status_index;   /* publish count; published slot =
                                        index & 1 */
} mailbox_t;

void mailbox_init(mailbox_t *mb);

/* UI thread: write the inactive slot, publish it (release). */
void mailbox_write(mailbox_t *mb, const synth_params_t *p);

/* Host thread: copy the published snapshot (u64 seqlock: retries while a
 * publish is in flight or interleaved; bounded to 100 attempts — a
 * hammering writer cannot starve the audio thread). Returns 1 with a
 * COMPLETE snapshot in *out (every field from one commit, at most one
 * block stale); returns 0 on exhaustion with *out untouched — the caller
 * keeps its previous params. */
int mailbox_read(mailbox_t *mb, synth_params_t *out);

/* Host thread: publish a status snapshot (store-release). */
void mailbox_status_publish(mailbox_t *mb, const ui_status_t *s);

/* UI thread: read the published snapshot (load-acquire; the copy is
 * fenced before the index reload, so a torn snapshot is never reported
 * as stable).
 * Returns 1 if the snapshot was stable (usable), 0 if the host published
 * mid-read (caller keeps its last good status; may retry next refresh). */
int mailbox_status_read(mailbox_t *mb, ui_status_t *out);

#endif /* OPHIS_MAILBOX_H */
