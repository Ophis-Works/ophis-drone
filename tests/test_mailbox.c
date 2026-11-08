/* tests/test_mailbox.c — lock-free mailbox contracts (PLAN_CODE_REVIEW_FIXES
 * .md Phase 2, H1/H2). SDL/ALSA-free: links only src/audio/mailbox.c plus
 * pthread for the two interleave tests; everything else is the documented
 * single-threaded semantics.
 *
 * Contract pinned (mailbox.h):
 *  - mailbox_read always returns a COMPLETE synth_params_t: every field
 *    comes from one mailbox_write commit — possibly one commit stale (the
 *    documented one-block bound), or from the zeroed mailbox_init state
 *    before the first commit — never a torn mix (H1).
 *  - An odd number of commits before a read never stalls the reader (the
 *    seqlock internals — parity, seq value, slot choice — are private to
 *    mailbox.c; main.c does exactly one commit before audio starts).
 *  - mailbox_status_read returns 1 on a stable snapshot, 0 when a publish
 *    interleaved (H2: the caller keeps the last good status); a 1 return
 *    never carries a torn snapshot. */
#include "harness.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#include "../src/audio/mailbox.h"

#define TAG_MAX 250          /* small tags keep the float markers exact */
#define N_COMMITS  1000000   /* writer-side commits in the interleave tests */
#define N_READS     200000   /* reader-side reads in the interleave tests */

/* Marker payload: every field carries the commit tag so ANY torn copy
 * mixes two tags and is detected by params_valid(). */
static void fill_params(synth_params_t *p, int tag)
{
    memset(p, 0, sizeof *p);
    p->bpm    = tag;
    p->master = tag + 1;
    p->run    = tag & 1;
    p->safety = (tag >> 1) & 1;
    for (int c = 0; c < 3; c++) {
        /* tag <= TAG_MAX (small), c <= 2: int->float conversions exact */
        p->chain[c].freq_hz         = 100.0f * (float)tag + (float)c;
        p->chain[c].level           = tag;
        p->seq[c].steps[0].pitch_hz = 200.0f * (float)tag + (float)c;
        p->seq[c].steps[0].vel      = tag;
        p->seq[c].steps[7].pitch_hz = 300.0f * (float)tag + (float)c;
    }
    p->delay.feedback = tag;
    p->reverb.mix     = tag;
    p->xmod[0].src    = tag;
}

/* A read may legitimately observe the factory init state (mailbox_init
 * publishes the zeroed params; the interleave reader bursts through many
 * reads before the writer thread's first commit is scheduled) — that is
 * only valid if ALL fields are zero; anything else must be one complete
 * tagged commit. A torn copy mixes the init state or two tags -> invalid. */
static int params_valid(const synth_params_t *p)
{
    int tag = p->bpm;
    if (tag == 0) {
        synth_params_t zero;
        memset(&zero, 0, sizeof zero);
        return memcmp(p, &zero, sizeof *p) == 0;
    }
    if (tag < 1 || tag > TAG_MAX)
        return 0;
    if (p->master != tag + 1 || p->run != (tag & 1) ||
        p->safety != ((tag >> 1) & 1))
        return 0;
    for (int c = 0; c < 3; c++)
        if (!fexactf(p->chain[c].freq_hz, 100.0f * (float)tag + (float)c) ||
            p->chain[c].level != tag ||
            !fexactf(p->seq[c].steps[0].pitch_hz,
                     200.0f * (float)tag + (float)c) ||
            p->seq[c].steps[0].vel != tag ||
            !fexactf(p->seq[c].steps[7].pitch_hz,
                     300.0f * (float)tag + (float)c))
            return 0;
    return p->delay.feedback == tag && p->reverb.mix == tag &&
           p->xmod[0].src == tag;
}

static void fill_status(ui_status_t *s, int tag)
{
    s->run  = tag & 1;
    s->gate = (float)tag;
    for (int c = 0; c < 3; c++) {
        s->env[c]  = (float)tag;
        s->step[c] = tag;      /* tag carrier (B-SEQ1: no seq_cursor) */
    }
}

static int status_valid(const ui_status_t *s)
{
    int tag = s->step[0];
    if (tag == 0) {                /* init snapshot: must be all-zero */
        ui_status_t zero;
        memset(&zero, 0, sizeof zero);
        return memcmp(s, &zero, sizeof *s) == 0;
    }
    if (tag < 1 || tag > TAG_MAX)
        return 0;
    if (s->run != (tag & 1) || !fexactf(s->gate, (float)tag))
        return 0;
    for (int c = 0; c < 3; c++)
        if (!fexactf(s->env[c], (float)tag) || s->step[c] != tag)
            return 0;
    return 1;
}

/* --- single-threaded semantics ------------------------------------------ */

static void test_single_threaded(void)
{
    mailbox_t mb;
    mailbox_init(&mb);

    /* init: the zeroed params are the published initial snapshot (main.c
     * commits once before the audio thread starts and must read it). */
    synth_params_t p;
    memset(&p, 0xAA, sizeof p);
    mailbox_read(&mb, &p);
    synth_params_t zero;
    memset(&zero, 0, sizeof zero);
    CHECK(memcmp(&p, &zero, sizeof p) == 0);

    /* write/read round-trip, deep across the struct. */
    fill_params(&p, 7);
    mailbox_write(&mb, &p);
    synth_params_t r;
    fill_params(&r, TAG_MAX);          /* poison */
    mailbox_read(&mb, &r);
    CHECK(params_valid(&r) && r.bpm == 7);

    /* read is non-destructive. */
    mailbox_read(&mb, &r);
    CHECK(r.bpm == 7);

    /* two writes before one read: the newest complete snapshot wins. */
    fill_params(&p, 1);
    mailbox_write(&mb, &p);
    fill_params(&p, 2);
    mailbox_write(&mb, &p);
    mailbox_read(&mb, &r);
    CHECK(params_valid(&r) && r.bpm == 2);

    /* an odd commit count (1 and 3) before a read must not stall the
     * reader and must yield the newest snapshot. */
    for (int tag = 1; tag <= 3; tag++) {
        fill_params(&p, tag);
        mailbox_write(&mb, &p);
        mailbox_read(&mb, &r);
        CHECK(params_valid(&r) && r.bpm == tag);
    }

    /* many alternating commits: the reader always sees one whole commit
     * (the last one here), never a mix. */
    for (int i = 1; i <= 100; i++) {
        fill_params(&p, i % TAG_MAX + 1);
        mailbox_write(&mb, &p);
    }
    mailbox_read(&mb, &r);
    CHECK(params_valid(&r) && r.bpm == 100 % TAG_MAX + 1);
}

static void test_status_single_threaded(void)
{
    mailbox_t mb;
    mailbox_init(&mb);

    ui_status_t s, out;
    fill_status(&s, 5);
    mailbox_status_publish(&mb, &s);
    fill_status(&out, TAG_MAX);        /* poison */
    CHECK(mailbox_status_read(&mb, &out) == 1);
    CHECK(status_valid(&out) && out.step[0] == 5);

    /* newest publish wins; the stability flag reports stable. */
    fill_status(&s, 6);
    mailbox_status_publish(&mb, &s);
    CHECK(mailbox_status_read(&mb, &out) == 1);
    CHECK(status_valid(&out) && out.step[0] == 6);
}

/* --- interleave: H1 torn-snapshot validity under a committing writer ---- */
/* A writer commits N_COMMITS times while the reader reads; every snapshot
 * the reader accepts must be one whole commit (tag-consistent). This is
 * the H1 observable: with the old 1-bit version, two commits between the
 * reader's version loads flip it back (ABA) while the second commit
 * overwrites the slot the reader is copying — a torn mix gets accepted. */

static mailbox_t g_mb;
static atomic_int g_params_done;

static void *params_writer(void *arg)
{
    (void)arg;
    synth_params_t p;
    for (int i = 0; i < N_COMMITS; i++) {
        fill_params(&p, i % TAG_MAX + 1);
        mailbox_write(&g_mb, &p);
    }
    atomic_store(&g_params_done, 1);
    return NULL;
}

static void test_params_interleave(void)
{
    mailbox_init(&g_mb);
    atomic_init(&g_params_done, 0);

    pthread_t th;
    CHECK(pthread_create(&th, NULL, params_writer, NULL) == 0);

    synth_params_t r;
    long reads = 0, tears = 0;
    for (int i = 0; i < N_READS; i++) {
        mailbox_read(&g_mb, &r);
        reads++;
        if (!params_valid(&r))
            tears++;
        if (atomic_load(&g_params_done))
            break;
    }
    pthread_join(th, NULL);

    /* final read after the writer is finished: must be its last commit */
    mailbox_read(&g_mb, &r);
    CHECK(params_valid(&r) && r.bpm == (N_COMMITS - 1) % TAG_MAX + 1);
    printf("params interleave: %ld reads, %ld torn snapshots accepted\n",
           reads, tears);
    CHECK(tears == 0);
}

/* --- interleave: H2 stability flag toggles under a publishing writer ---- */

static atomic_int g_status_done;

static void *status_writer(void *arg)
{
    (void)arg;
    ui_status_t s;
    for (int i = 0; i < N_COMMITS; i++) {
        fill_status(&s, i % TAG_MAX + 1);
        mailbox_status_publish(&g_mb, &s);
    }
    atomic_store(&g_status_done, 1);
    return NULL;
}

static void test_status_interleave(void)
{
    mailbox_init(&g_mb);
    atomic_init(&g_status_done, 0);

    pthread_t th;
    CHECK(pthread_create(&th, NULL, status_writer, NULL) == 0);

    ui_status_t out;
    long stable = 0, unstable = 0, tears = 0;
    for (int i = 0; i < N_READS; i++) {
        int rc = mailbox_status_read(&g_mb, &out);
        if (rc) {
            stable++;
            if (!status_valid(&out))
                tears++;               /* 1 must never carry a torn copy */
        } else {
            unstable++;                /* publish interleaved: caller keeps
                                          the last good status (H2) */
        }
        if (atomic_load(&g_status_done))
            break;
    }
    pthread_join(th, NULL);
    printf("status interleave: %ld stable, %ld unstable, %ld torn\n",
           stable, unstable, tears);

    CHECK(stable > 0);                 /* flag usable on the stable path */
    if (unstable == 0)
        printf("note: no interleaved publish observed (serialized "
               "scheduler, e.g. under valgrind); the H2 retry path is "
               "unexercised this run, not violated\n");
    CHECK(tears == 0);                 /* stable never means torn */
}

int main(void)
{
    test_single_threaded();
    test_status_single_threaded();
    test_params_interleave();
    test_status_interleave();
    HARNESS_REPORT();
}
