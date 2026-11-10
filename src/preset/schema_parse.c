/* src/preset/schema.c — preset format schema layer (PLAN §9 format,
 * schema version 2, owner order 2026-11-09 PLAN_MOD_MATRIX_16.md R2)
 * over the vendored sheredom/json.h (third_party/json.h, Unlicense).
 * Replaces the Phase 1-6 hand-rolled preset/json.{c,h}
 * (PLAN_CODE_REVIEW_FIXES.md Phase 7): json.h knows nothing about our
 * schema, so the contract is explicit code here — strict app/version
 * (version 2 ONLY; version 1 files REJECT), required keys, array
 * lengths 3 chains / 2 subs / 8 steps / "xmod" 0..16 ACTIVE entries in
 * ascending slot order (entry i maps to slot i; slots absent from the
 * array are OFF: dest off, amount 0, mix 0), enum strings, integer
 * percents 0..100, bpm 1..500, duty 5..95, delay/reverb/safety objects
 * REQUIRED, unknown keys leniently skipped, over-long keys (> 15 chars)
 * REJECTED (Phase 6 P5 absorbed), duplicate keys REJECTED at every
 * object level (json.h preserves member order, so the walker flags a
 * second occurrence). Parses into a local, commits into *p only on
 * success (IMPLEMENTATION.md §7). Error messages are field paths,
 * e.g. "seq.steps[2].vel".
 * Writer: the synth_params_t DOM is built into a fixed arena and
 * serialized with json.h's minified writer (compact {"key":value,...},
 * byte-identical to the Phase 1-6 hand-rolled writer, so shipped presets
 * round-trip byte-comparable). POSIX open_memstream is NOT needed:
 * json_write_minified returns its own heap buffer, which we copy into the
 * caller's fixed buffer (the 8 KiB contract is preserved).
 * BPM is GLOBAL (AMENDED, owner order, PLAN §6/§9): the REQUIRED
 * top-level "bpm" (integer 1..500) sits next to "master"; seq objects
 * carry NO "bpm" key (a stray one is an unknown key — ignored). */
#include "schema.h"

#include <json.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "schema_priv.h"

#include <json.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* ---------------- parser ---------------- */

/* Phase 6 P5 absorbed (PLAN_CODE_REVIEW_FIXES.md): object keys longer
 * than 15 characters are rejected instead of silently truncated. */
#define SCHEMA_KEY_MAX 15

typedef struct {
    synth_params_t p;   /* parse-into-local; committed on success */
    char *err;
    int err_len;
    int failed;
} ps_t;

/* field-path error message (e.g. "seq.steps[2].vel") */
static void perr(ps_t *j, const char *msg)
{
    if (j->failed) return;
    j->failed = 1;
    if (j->err && j->err_len > 0)
        snprintf(j->err, (size_t)j->err_len, "%s", msg);
}

/* "<path>.<key>", or just "<key>" at the root (empty path) */
static void join_path(char *out, int cap, const char *path, const char *key)
{
    if (path[0] == '\0') snprintf(out, (size_t)cap, "%s", key);
    else snprintf(out, (size_t)cap, "%s.%s", path, key);
}

/* required-key bookkeeping per object level */
/* A3 (PLAN_OPTIMIZATION.md O1): a key's seen-bit is its INDEX in its
 * req table — no hand-numbered bits anywhere. Each table gets a
 * parallel enum; handlers set SB(<ENUM_CONST>).
 * SB: seen-bit for index i */
#define SB(i) (1u << (i))

/* walker arg: per-object seen mask + array slot indices */
typedef struct { unsigned seen; int idx, idx2; } warg_t;

static const char *val_string(const json_value_t *v, ps_t *j,
                              const char *path)
{
    if (!v || v->type != json_type_string) { perr(j, path); return NULL; }
    return ((const json_string_t *)v->payload)->string;
}

static double val_number(const json_value_t *v, ps_t *j, const char *path)
{
    if (!v || v->type != json_type_number) { perr(j, path); return 0.0; }
    /* json.h stores numbers as NUL-terminated copies of the raw text */
    return strtod(((const json_number_t *)v->payload)->number, NULL);
}

/* documented-range check for raw numeric fields (owner mandate
 * 2026-11-05, PLAN_SCHEMA_RANGE_VALIDATION.md): out of range -> reject
 * the whole preset. Also rejects ±inf ("1e999" is JSON-legal and
 * strtod turns it into inf, which poisons the DSP with NaN); the
 * comparisons fail for NaN too. */
static int val_range(double d, double lo, double hi,
                     ps_t *j, const char *path)
{
    if (d < lo || d > hi) { perr(j, path); return -1; }
    return 0;
}

/* integer per the old parser: integral value within int range.
 * -Wfloat-equal approach: no ==/!= on doubles — the NaN-safe range guard
 * (NaN fails the >=/<= pair; ±inf fail the bounds) subsumes the old
 * NaN/inf rejection, and for finite values floor(d) < d || floor(d) > d
 * is exactly d != floor(d). Same accept/reject set as the old check. */
static int val_int(const json_value_t *v, int *out, ps_t *j,
                   const char *path)
{
    double d = val_number(v, j, path);
    if (j->failed) return -1;
    if (!(d >= -2147483648.0 && d <= 2147483647.0) ||
        floor(d) < d || floor(d) > d) {
        perr(j, path);
        return -1;
    }
    *out = (int)d;
    return 0;
}

static int val_percent(const json_value_t *v, int *out, ps_t *j,
                       const char *path)
{
    int x;
    if (val_int(v, &x, j, path)) return -1;
    if (x < 0 || x > 100) { perr(j, path); return -1; }
    *out = x;
    return 0;
}

/* square duty: integer percent 5..95 (approved change eacb368+fix) */
static int val_duty(const json_value_t *v, int *out, ps_t *j,
                    const char *path)
{
    int x;
    if (val_int(v, &x, j, path)) return -1;
    if (x < 5 || x > 95) { perr(j, path); return -1; }
    *out = x;
    return 0;
}

static int val_bool(const json_value_t *v, int *out, ps_t *j,
                    const char *path)
{
    if (v && v->type == json_type_true)  { *out = 1; return 0; }
    if (v && v->type == json_type_false) { *out = 0; return 0; }
    perr(j, path);
    return -1;
}

static int val_enum(const json_value_t *v, const ent_t *tab, int n,
                    void *out, ps_t *j, const char *path)
{
    const char *s = val_string(v, j, path);
    if (j->failed) return -1;
    for (int i = 0; i < n; i++)
        if (strcmp(s, tab[i].s) == 0) {
            memcpy(out, &tab[i].v, sizeof(int));
            return 0;
        }
    perr(j, path);
    return -1;
}

/* set seen-bit i (the table index) in the walker arg */
static void set_bit(void *arg, int i) { ((warg_t *)arg)->seen |= SB(i); }

/* ---- D4 (PLAN_OPTIMIZATION.md O5): table-driven keys ----------------
 * The regular scalar keys of every object live in a key_spec_t table;
 * ONE generic walker reads them. Irregular keys (checks, arrays,
 * objects) stay as small residual handlers referenced by KT_CUSTOM.
 * The seen-bit is the table index (A3). -------------------------------
 * KT_*: member kinds (all int-sized or float members, addressed as
 * base + off). -------------------------------------------------------- */
typedef enum {
  KT_PERCENT,     /* int member, 0..100 via val_percent               */
  KT_INT,         /* int member, lo..hi                               */
  KT_ENUM,        /* int member, val_enum over tab (n = lo)           */
  KT_DUTY,        /* int member, val_duty (5..95)                     */
  KT_BOOL,        /* int member, val_bool                             */
  KT_FNUM,        /* float member, val_number + val_range lo..hi      */
  KT_CUSTOM       /* residual handler (per-object callback)           */
} key_kind_t;

typedef struct {
  const char *key;
  key_kind_t  kind;
  int         off;   /* byte offset of the member within the object base */
  double      lo, hi;/* KT_INT range / KT_ENUM count / KT_FNUM range     */
  const void *tab;   /* KT_ENUM: the ent_t table                         */
  int      (*custom)(ps_t *j, const char *path, const char *key,
                     const json_value_t *v, void *arg);
} key_spec_t;

static int walk_keys(ps_t *j, const char *path, const char *key,
                     const json_value_t *v, void *base,
                     const key_spec_t *tab, int n, void *arg)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(key, tab[i].key) != 0) continue;
        void *m = (char *)base + tab[i].off;
        switch (tab[i].kind) {
        case KT_PERCENT:
            return val_percent(v, (int *)m, j, path)
                   ? -1 : (set_bit(arg, i), 0);
        case KT_INT: {
            int t;
            if (val_int(v, &t, j, path)) return -1;
            if ((double)t < tab[i].lo || (double)t > tab[i].hi) {
                perr(j, path); return -1;
            }
            *(int *)m = t;
            set_bit(arg, i);
            return 0;
        }
        case KT_ENUM:
            /* tab[i].lo carries the enum count as an integral double
             * (6/4/2/5/10/8 literals): exactly representable, cast is
             * value-preserving */
            return val_enum(v, (const ent_t *)tab[i].tab, (int)tab[i].lo,
                            (int *)m, j, path)
                   ? -1 : (set_bit(arg, i), 0);
        case KT_DUTY:
            return val_duty(v, (int *)m, j, path)
                   ? -1 : (set_bit(arg, i), 0);
        case KT_BOOL:
            return val_bool(v, (int *)m, j, path)
                   ? -1 : (set_bit(arg, i), 0);
        case KT_FNUM: {
            double t = val_number(v, j, path);
            if (j->failed) return -1;
            if (val_range(t, (double)tab[i].lo, (double)tab[i].hi,
                          j, path)) return -1;
            *(float *)m = (float)t;
            set_bit(arg, i);
            return 0;
        }
        case KT_CUSTOM:
            return tab[i].custom(j, path, key, v, arg);
        }
    }
    return 0;   /* unknown key: leniently skipped */
}

typedef int (*obj_fn)(ps_t *j, const char *path, const char *key,
                      const json_value_t *v, void *arg);
typedef int (*arr_fn)(ps_t *j, const char *path, int idx,
                      const json_value_t *v, void *arg);


/* Walk an object: strict type, duplicate keys REJECTED (any repeated key
 * string among the siblings — json.h preserves member order), over-long
 * keys REJECTED, unknown keys handed to fn which leniently skips them. */
static int walk_object(const json_value_t *val, ps_t *j, const char *path,
                       obj_fn fn, void *arg)
{
    if (!val || val->type != json_type_object) { perr(j, path); return -1; }
    const json_object_t *o = val->payload;
    for (const json_object_element_t *e = o->start; e; e = e->next) {
        const char *key = e->name->string;
        char cpath[128];
        join_path(cpath, (int)sizeof cpath, path, key);
        if (strlen(key) > SCHEMA_KEY_MAX) { perr(j, cpath); return -1; }
        for (const json_object_element_t *prev = o->start; prev != e;
             prev = prev->next)
            if (strcmp(prev->name->string, key) == 0) {
                perr(j, cpath);     /* duplicate key */
                return -1;
            }
        if (fn(j, cpath, key, e->value, arg)) return -1;
    }
    return 0;
}

/* Walk an array of lo..hi elements (element paths "<path>[i]"); a
 * length outside the range rejects by naming the array path itself
 * ("xmod"), never a member path */
static int walk_array(const json_value_t *val, ps_t *j, const char *path,
                      int lo, int hi, arr_fn fn, void *arg)
{
    if (!val || val->type != json_type_array) { perr(j, path); return -1; }
    const json_array_t *a = val->payload;
    if ((int)a->length < lo || (int)a->length > hi) {
        perr(j, path);
        return -1;
    }
    int i = 0;
    for (const json_array_element_t *e = a->start; e; e = e->next, i++) {
        char cpath[128];
        snprintf(cpath, sizeof cpath, "%s[%d]", path, i);
        if (fn(j, cpath, i, e->value, arg)) return -1;
    }
    return 0;
}

static int parse_delay(ps_t *j, const char *path, const json_value_t *v);
static int parse_reverb(ps_t *j, const char *path, const json_value_t *v);
static int chain_elem(ps_t *j, const char *path, int idx,
                      const json_value_t *v, void *arg);
static int seq_elem(ps_t *j, const char *path, int idx,
                    const json_value_t *v, void *arg);
static int xmod_elem(ps_t *j, const char *path, int idx,
                     const json_value_t *v, void *arg);

/* root seen-bit indices (A3: bit == index = key-table order) */
enum { RB_APP, RB_VERSION, RB_MASTER, RB_BPM, RB_CHAIN, RB_SEQ,
       RB_XMOD, RB_DELAY, RB_REVERB, RB_SAFETY, RB_COUNT };
/* chain */
enum { CB_FREQ, CB_WAVE, CB_DUTY, CB_LEVEL, CB_VEL, CB_SUB, CB_ADSR,
       CB_FILTER, CB_MIXVOL, CBCOUNT };
/* subosc */
enum { SB2_DIV, SB2_WAVE, SB2_DUTY, SB2_LEVEL, SB2COUNT };
/* adsr */
enum { AB_A, AB_D, AB_S, AB_R, ABCOUNT };
/* filter (members live in chain_params_t) */
enum { FB_MODE, FB_FC, FB_RES, FB_ENV, FB_POLES, FBCOUNT };
/* seq */
enum { QB_ON, QB_DIV, QB_MODE, QB_ONCE, QB_STEPS, QBCOUNT };
/* step */
enum { TB_PITCH, TB_VEL, TBCOUNT };
/* xmod */
enum { XB_SRC, XB_DEST, XB_AMT, XB_MIX, XB_TGT, XBCOUNT };
/* delay */
enum { DB_TIMEL, DB_TIMER, DB_FEEDBACK, DB_TONE, DB_MIX, DBCOUNT };
/* reverb */
enum { VB_SIZE, VB_DAMP, VB_MIX, VB_WIDTH, VBCOUNT };

static int missing_keys(ps_t *j, const char *path, const key_spec_t *tab,
                        int n, unsigned seen)
{
    for (int i = 0; i < n; i++)
        if (!(seen & SB(i))) {
            char b[128];
            join_path(b, (int)sizeof b, path, tab[i].key);
            perr(j, b);
            return -1;
        }
    return 0;
}

/* ---- residual (irregular) key handlers ------------------------------ */

static int root_custom(ps_t *j, const char *path, const char *key,
                       const json_value_t *v, void *arg);
static int chain_custom(ps_t *j, const char *path, const char *key,
                        const json_value_t *v, void *arg);
static int seq_custom(ps_t *j, const char *path, const char *key,
                      const json_value_t *v, void *arg);
static int xmod_amount(ps_t *j, const char *path, const char *key,
                       const json_value_t *v, void *arg);

/* ---- key tables (D4): the regular scalar keys of every object -------
 * base = the object's own struct; seen-bit = table index. -------------- */

static const key_spec_t ROOT_KEYS[] = {
    [RB_APP]     = { "app",     KT_CUSTOM, 0, 0, 0, NULL, root_custom },
    [RB_VERSION] = { "version", KT_CUSTOM, 0, 0, 0, NULL, root_custom },
    [RB_MASTER]  = { "master",  KT_PERCENT, offsetof(synth_params_t, master) },
    [RB_BPM]     = { "bpm",     KT_INT, offsetof(synth_params_t, bpm),
                     1, 500 },
    [RB_CHAIN]   = { "chain",   KT_CUSTOM, 0, 0, 0, NULL, root_custom },
    [RB_SEQ]     = { "seq",     KT_CUSTOM, 0, 0, 0, NULL, root_custom },
    [RB_XMOD]    = { "xmod",    KT_CUSTOM, 0, 0, 0, NULL, root_custom },
    [RB_DELAY]   = { "delay",   KT_CUSTOM, 0, 0, 0, NULL, root_custom },
    [RB_REVERB]  = { "reverb",  KT_CUSTOM, 0, 0, 0, NULL, root_custom },
    [RB_SAFETY]  = { "safety",  KT_BOOL, offsetof(synth_params_t, safety) },
};

static const key_spec_t CHAIN_KEYS[] = {
    [CB_FREQ]  = { "freq_hz", KT_FNUM, offsetof(chain_params_t, freq_hz),
                   0.01, 20000.0 },  /* floor 0.01 Hz (PLAN_FREQ_
                                      * DECIMALS.md R4 — the LFO range) */
    [CB_WAVE]  = { "wave",    KT_ENUM, offsetof(chain_params_t, wave),
                   6, 0, wave_tab },
    [CB_DUTY]  = { "duty",    KT_DUTY, offsetof(chain_params_t, duty) },
    [CB_LEVEL] = { "level",   KT_PERCENT, offsetof(chain_params_t, level) },
    [CB_VEL]   = { "vel",     KT_PERCENT, offsetof(chain_params_t, vel) },
    [CB_SUB]   = { "sub",     KT_CUSTOM, 0, 0, 0, NULL, chain_custom },
    [CB_ADSR]  = { "adsr",    KT_CUSTOM, 0, 0, 0, NULL, chain_custom },
    [CB_FILTER]= { "filter",  KT_CUSTOM, 0, 0, 0, NULL, chain_custom },
    /* OPTIONAL with default 100 (post-walk fixup in chain_elem) */
    [CB_MIXVOL]= { "mixvol",  KT_PERCENT, offsetof(chain_params_t, mixvol) },
};

static const key_spec_t SUB_KEYS[] = {
    [SB2_DIV]  = { "div",   KT_INT,  offsetof(subosc_params_t, div),
                   1, 16 },
    [SB2_WAVE] = { "wave",  KT_ENUM, offsetof(subosc_params_t, wave),
                   6, 0, wave_tab },
    [SB2_DUTY] = { "duty",  KT_DUTY, offsetof(subosc_params_t, duty) },
    [SB2_LEVEL]= { "level", KT_PERCENT, offsetof(subosc_params_t, level) },
};

static const key_spec_t ADSR_KEYS[] = {
    [AB_A] = { "a", KT_FNUM, offsetof(adsr_params_t, a), 0.0, 60.0 },
    [AB_D] = { "d", KT_FNUM, offsetof(adsr_params_t, d), 0.0, 60.0 },
    [AB_S] = { "s", KT_PERCENT, offsetof(adsr_params_t, s) },
    [AB_R] = { "r", KT_FNUM, offsetof(adsr_params_t, r), 0.0, 60.0 },
};

static const key_spec_t FILTER_KEYS[] = {
    [FB_MODE]  = { "mode",  KT_ENUM, offsetof(chain_params_t, fmode),
                   4, 0, filt_tab },
    [FB_FC]    = { "fc",    KT_FNUM, offsetof(chain_params_t, fc),
                   20.0, 20000.0 },
    [FB_RES]   = { "res",   KT_PERCENT, offsetof(chain_params_t, res) },
    [FB_ENV]   = { "env",   KT_INT, offsetof(chain_params_t, envamt),
                   -100, 100 },
    [FB_POLES] = { "poles", KT_ENUM, offsetof(chain_params_t, poles),
                   2, 0, poles_tab },
};

static const key_spec_t STEP_KEYS[] = {
    [TB_PITCH] = { "pitch_hz", KT_FNUM, offsetof(seq_step_t, pitch_hz),
                   0.01, 20000.0 },  /* floor 0.01 Hz (PLAN_FREQ_
                                      * DECIMALS.md R4) */
    [TB_VEL]   = { "vel",      KT_PERCENT, offsetof(seq_step_t, vel) },
};

static const key_spec_t SEQ_KEYS[] = {
    [QB_ON]    = { "on",    KT_BOOL, offsetof(seq_params_t, on) },
    [QB_DIV]   = { "div",   KT_INT, offsetof(seq_params_t, div), 1, 16 },
    [QB_MODE]  = { "mode",  KT_ENUM, offsetof(seq_params_t, mode),
                   5, 0, seqmode_tab },
    [QB_ONCE]  = { "once",  KT_BOOL, offsetof(seq_params_t, once) },
    [QB_STEPS] = { "steps", KT_CUSTOM, 0, 0, 0, NULL, seq_custom },
};

static const key_spec_t XMOD_KEYS[] = {
    [XB_SRC]  = { "src",    KT_ENUM, offsetof(xmod_params_t, src),
                  10, 0, src_tab },
    [XB_DEST] = { "dest",   KT_ENUM, offsetof(xmod_params_t, dest),
                  13, 0, dest_tab },
    [XB_AMT]  = { "amount", KT_CUSTOM, 0, 0, 0, NULL, xmod_amount },
    [XB_MIX]  = { "mix",    KT_PERCENT, offsetof(xmod_params_t, mix) },
    [XB_TGT]  = { "tgt",    KT_INT, offsetof(xmod_params_t, tgt), 0, 2 },
};

static const key_spec_t DELAY_KEYS[] = {
    [DB_TIMEL] = { "time_l", KT_FNUM, offsetof(delay_params_t, time_l),
                   0.0, 2.0 },
    [DB_TIMER] = { "time_r", KT_FNUM, offsetof(delay_params_t, time_r),
                   0.0, 2.0 },
    [DB_FEEDBACK] = { "feedback", KT_PERCENT,
                      offsetof(delay_params_t, feedback) },
    [DB_TONE]  = { "tone", KT_INT, offsetof(delay_params_t, tone),
                   20, 20000 },
    [DB_MIX]   = { "mix",  KT_PERCENT, offsetof(delay_params_t, mix) },
};

static const key_spec_t REVERB_KEYS[] = {
    [VB_SIZE]  = { "size",  KT_PERCENT, offsetof(reverb_params_t, size) },
    [VB_DAMP]  = { "damp",  KT_PERCENT, offsetof(reverb_params_t, damp) },
    [VB_MIX]   = { "mix",   KT_PERCENT, offsetof(reverb_params_t, mix) },
    [VB_WIDTH] = { "width", KT_PERCENT, offsetof(reverb_params_t, width) },
};

/* ---- the per-object key fns: ONE walk_keys call (+ custom residues) -- */

static int root_key(ps_t *j, const char *path, const char *key,
                    const json_value_t *v, void *arg)
{
    return walk_keys(j, path, key, v, &j->p, ROOT_KEYS, RB_COUNT, arg);
}

static int root_custom(ps_t *j, const char *path, const char *key,
                       const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    if (!strcmp(key, "app")) {
        const char *s = val_string(v, j, path);
        if (j->failed) return -1;
        if (strcmp(s, "ophis-drone") != 0) { perr(j, path); return -1; }
        a->seen |= SB(RB_APP);
        return 0;
    }
    if (!strcmp(key, "version")) {
        int ver;
        if (val_int(v, &ver, j, path)) return -1;
        /* schema 2 ONLY (owner order 2026-11-09, R2): version 1 — the
         * old fixed-length-3 contract — is REJECTED, not migrated */
        if (ver != 2) { perr(j, path); return -1; }
        a->seen |= SB(RB_VERSION);
        return 0;
    }
    if (!strcmp(key, "chain")) {
        if (walk_array(v, j, path, 3, 3, chain_elem, NULL)) return -1;
        a->seen |= SB(RB_CHAIN);
        return 0;
    }
    if (!strcmp(key, "seq")) {
        if (walk_array(v, j, path, 3, 3, seq_elem, NULL)) return -1;
        a->seen |= SB(RB_SEQ);
        return 0;
    }
    if (!strcmp(key, "xmod")) {
        /* v2 (R2): 0..MOD_SLOTS ACTIVE entries; entry i lands in slot i
         * (positional — xmod_elem's warg idx); absent slots stay at the
         * memset-zero OFF state of the parse-into local */
        if (walk_array(v, j, path, 0, MOD_SLOTS, xmod_elem, NULL))
            return -1;
        a->seen |= SB(RB_XMOD);
        return 0;
    }
    if (!strcmp(key, "delay")) {
        if (parse_delay(j, path, v)) return -1;
        a->seen |= SB(RB_DELAY);
        return 0;
    }
    if (!strcmp(key, "reverb")) {
        if (parse_reverb(j, path, v)) return -1;
        a->seen |= SB(RB_REVERB);
        return 0;
    }
    return 0;   /* unknown key: leniently skipped */
}

static int sub_key(ps_t *j, const char *path, const char *key,
                   const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    subosc_params_t *s = &j->p.chain[a->idx].sub[a->idx2];
    return walk_keys(j, path, key, v, s, SUB_KEYS, SB2COUNT, arg);
}

static int sub_elem(ps_t *j, const char *path, int idx,
                    const json_value_t *v, void *arg)
{
    warg_t *ca = arg;               /* chain's warg: idx = chain slot */
    warg_t a = { 0, ca->idx, idx };
    if (walk_object(v, j, path, sub_key, &a)) return -1;
    return missing_keys(j, path, SUB_KEYS, SB2COUNT, a.seen);
}

static int adsr_key(ps_t *j, const char *path, const char *key,
                    const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    adsr_params_t *ad = &j->p.chain[a->idx].adsr;
    return walk_keys(j, path, key, v, ad, ADSR_KEYS, ABCOUNT, arg);
}

static int parse_adsr(ps_t *j, const char *path, const json_value_t *v,
                      int idx)
{
    warg_t a = { 0, idx, 0 };
    if (walk_object(v, j, path, adsr_key, &a)) return -1;
    return missing_keys(j, path, ADSR_KEYS, ABCOUNT, a.seen);
}

static int filter_key(ps_t *j, const char *path, const char *key,
                      const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    chain_params_t *c = &j->p.chain[a->idx];
    return walk_keys(j, path, key, v, c, FILTER_KEYS, FBCOUNT, arg);
}

static int parse_filter(ps_t *j, const char *path, const json_value_t *v,
                        int idx)
{
    warg_t a = { 0, idx, 0 };
    if (walk_object(v, j, path, filter_key, &a)) return -1;
    return missing_keys(j, path, FILTER_KEYS, FBCOUNT, a.seen);
}

static int chain_key(ps_t *j, const char *path, const char *key,
                     const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    chain_params_t *c = &j->p.chain[a->idx];
    return walk_keys(j, path, key, v, c, CHAIN_KEYS, CBCOUNT, arg);
}

static int chain_elem(ps_t *j, const char *path, int idx,
                      const json_value_t *v, void *arg)
{
    (void)arg;
    warg_t a = { 0, idx, 0 };
    if (walk_object(v, j, path, chain_key, &a)) return -1;
    /* mixvol is OPTIONAL with default 100 — j.p is memset before the
     * walk, so absent must be fixed up here (0 would be SILENT) */
    if (missing_keys(j, path, CHAIN_KEYS, CBCOUNT - 1, a.seen))
        return -1;
    if (!(a.seen & SB(CB_MIXVOL))) j->p.chain[idx].mixvol = 100;
    return 0;
}

static int chain_custom(ps_t *j, const char *path, const char *key,
                        const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    if (!strcmp(key, "sub")) {
        if (walk_array(v, j, path, 2, 2, sub_elem, a)) return -1;
        a->seen |= SB(CB_SUB);
        return 0;
    }
    if (!strcmp(key, "adsr")) {
        if (parse_adsr(j, path, v, a->idx)) return -1;
        a->seen |= SB(CB_ADSR);
        return 0;
    }
    if (!strcmp(key, "filter")) {
        if (parse_filter(j, path, v, a->idx)) return -1;
        a->seen |= SB(CB_FILTER);
        return 0;
    }
    return 0;   /* unknown key: leniently skipped */
}

static int step_key(ps_t *j, const char *path, const char *key,
                    const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    seq_step_t *st = &j->p.seq[a->idx].steps[a->idx2];
    return walk_keys(j, path, key, v, st, STEP_KEYS, TBCOUNT, arg);
}

static int step_elem(ps_t *j, const char *path, int idx,
                     const json_value_t *v, void *arg)
{
    (void)idx;
    warg_t *ca = arg;               /* seq's warg: idx = seq slot */
    warg_t a = { 0, ca->idx, idx };
    if (walk_object(v, j, path, step_key, &a)) return -1;
    return missing_keys(j, path, STEP_KEYS, TBCOUNT, a.seen);
}

static int seq_key(ps_t *j, const char *path, const char *key,
                   const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    seq_params_t *sq = &j->p.seq[a->idx];
    return walk_keys(j, path, key, v, sq, SEQ_KEYS, QBCOUNT, arg);
}

static int seq_custom(ps_t *j, const char *path, const char *key,
                      const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    if (!strcmp(key, "steps")) {
        if (walk_array(v, j, path, 8, 8, step_elem, a)) return -1;
        a->seen |= SB(QB_STEPS);
        return 0;
    }
    return 0;   /* unknown key: leniently skipped */
}

static int seq_elem(ps_t *j, const char *path, int idx,
                    const json_value_t *v, void *arg)
{
    (void)arg;
    warg_t a = { 0, idx, 0 };
    if (walk_object(v, j, path, seq_key, &a)) return -1;
    return missing_keys(j, path, SEQ_KEYS, QBCOUNT, a.seen);
}

static int xmod_key(ps_t *j, const char *path, const char *key,
                    const json_value_t *v, void *arg)
{
    warg_t *a = arg;
    xmod_params_t *x = &j->p.xmod[a->idx];
    return walk_keys(j, path, key, v, x, XMOD_KEYS, XBCOUNT, arg);
}

static int xmod_amount(ps_t *j, const char *path, const char *key,
                       const json_value_t *v, void *arg)
{
    (void)key;
    warg_t *a = arg;
    xmod_params_t *x = &j->p.xmod[a->idx];
    x->amount = (float)val_number(v, j, path);
    if (j->failed) return -1;
    a->seen |= SB(XB_AMT);
    return 0;
}

static int xmod_elem(ps_t *j, const char *path, int idx,
                     const json_value_t *v, void *arg)
{
    (void)arg;
    warg_t a = { 0, idx, 0 };
    if (walk_object(v, j, path, xmod_key, &a)) return -1;
    if (missing_keys(j, path, XMOD_KEYS, XBCOUNT - 2, a.seen)) return -1;
    /* amount range depends on dest (key order arbitrary — post-walk;
     * PLAN_SCHEMA_RANGE_VALIDATION.md) */
    xmod_params_t *x = &j->p.xmod[idx];
    if (x->dest == MOD_RING)
        return val_range((double)x->amount, 0.0, 100.0, j, path);
    double amt_max = (x->dest == MOD_PITCH) ? 50.0 : 100.0;
    return val_range((double)x->amount, -amt_max, amt_max, j, path);
}

static int delay_key(ps_t *j, const char *path, const char *key,
                     const json_value_t *v, void *arg)
{
    delay_params_t *d = &j->p.delay;
    return walk_keys(j, path, key, v, d, DELAY_KEYS, DBCOUNT, arg);
}

static int parse_delay(ps_t *j, const char *path, const json_value_t *v)
{
    warg_t a = { 0, 0, 0 };
    if (walk_object(v, j, path, delay_key, &a)) return -1;
    return missing_keys(j, path, DELAY_KEYS, DBCOUNT, a.seen);
}

static int reverb_key(ps_t *j, const char *path, const char *key,
                      const json_value_t *v, void *arg)
{
    reverb_params_t *r = &j->p.reverb;
    return walk_keys(j, path, key, v, r, REVERB_KEYS, VBCOUNT, arg);
}

static int parse_reverb(ps_t *j, const char *path, const json_value_t *v)
{
    warg_t a = { 0, 0, 0 };
    if (walk_object(v, j, path, reverb_key, &a)) return -1;
    return missing_keys(j, path, REVERB_KEYS, VBCOUNT, a.seen);
}

int schema_parse_params(const char *text, int text_len, synth_params_t *p,
                        char *err, int err_len)
{
    ps_t j;
    memset(&j, 0, sizeof j);
    memset(&j.p, 0, sizeof j.p);   /* run stays 0: excluded from JSON */
    j.err = err;
    j.err_len = err_len;

    if (!text || text_len <= 0 || !p) {
        perr(&j, "bad arguments");
        return -1;
    }

    json_parse_result_t res;
    /* default allocator (NULL -> malloc): one allocation per parse;
     * preset I/O runs on the UI thread, the RT path never parses */
    json_value_t *root = json_parse_ex(text, (size_t)text_len, 0, NULL,
                                       NULL, &res);
    if (!root) {
        if (err && err_len > 0)
            snprintf(err, (size_t)err_len, "%s",
                     res.error == (size_t)json_parse_error_allocator_failed
                         ? "out of memory" : "malformed JSON");
        return -1;   /* nothing allocated: parse failed before the DOM */
    }

    /* json.h allocates the whole DOM in one block (default allocator =
     * malloc/free); release it on every exit path below. */
    int rc = -1;
    if (root->type != json_type_object) {
        perr(&j, "malformed JSON");
    } else {
        warg_t a = { 0, 0, 0 };
        if (walk_object(root, &j, "", root_key, &a)) goto done;
        /* all 10 root keys required — a missing one reports its path */
        if (missing_keys(&j, "", ROOT_KEYS, RB_COUNT, a.seen)) {
            /* missing_keys already reported */
        } else {
            *p = j.p;
            rc = 0;
        }
    }
done:
    free(root);
    return rc;
}
