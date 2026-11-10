/* src/preset/schema_write.c — the preset writer: the synth_params_t DOM
 * is built into a
 * fixed arena (overflow -> -1, the 8 KiB contract) and serialized with
 * json.h's minified writer. */
#include "schema_priv.h"

#include <json.h>

#include <stdio.h>
#include <string.h>
/* ---------------- writer ---------------- */

/* Fixed arena for the DOM build (pools sized with headroom over the exact
 * field counts; any overflow makes the writer return -1 — the same
 * "does not fit" contract as the old writer's 8 KiB guarantee). Maxima
 * verified for the v2 worst case (16 active xmod entries): vals 290,
 * key+value strings 290, numbers 165, object elements 240, objects 60,
 * array elements 55, arrays 9. */
#define W_VAL_N 448
#define W_STR_N 304
#define W_NUM_N 176
#define W_EL_N  256
#define W_OBJ_N 72
#define W_AEL_N 64
#define W_ARR_N 12

typedef struct {
    json_value_t           val[W_VAL_N];
    json_string_t          str[W_STR_N];
    json_number_t          num[W_NUM_N];
    char                   numbuf[W_NUM_N][32];
    json_object_element_t  el[W_EL_N];
    json_object_t          obj[W_OBJ_N];
    json_object_element_t *otail[W_OBJ_N];
    json_array_element_t   ael[W_AEL_N];
    json_array_t           arr[W_ARR_N];
    json_array_element_t  *atail[W_ARR_N];
    int nv, nstr, nnum, nel, nobj, nael, narr;
    int overflow;
} warena;

static json_value_t *wval(warena *w, size_t type, void *payload)
{
    if (w->nv >= W_VAL_N) { w->overflow = 1; return NULL; }
    json_value_t *v = &w->val[w->nv++];
    v->payload = payload;
    v->type = type;
    return v;
}

static json_string_t *wstrbuf(warena *w, const char *s)
{
    if (w->nstr >= W_STR_N) { w->overflow = 1; return NULL; }
    json_string_t *st = &w->str[w->nstr++];
    st->string = s;
    st->string_size = strlen(s);
    return st;
}

static json_value_t *wstr(warena *w, const char *s)
{
    json_string_t *st = wstrbuf(w, s);
    return st ? wval(w, json_type_string, st) : NULL;
}

static json_value_t *wnum_raw(warena *w, const char *text)
{
    if (w->nnum >= W_NUM_N) { w->overflow = 1; return NULL; }
    char *b = w->numbuf[w->nnum];
    snprintf(b, sizeof w->numbuf[0], "%s", text);
    json_number_t *n = &w->num[w->nnum++];
    n->number = b;
    n->number_size = strlen(b);
    return wval(w, json_type_number, n);
}

/* floats: "%.9g" of the double — identical to the old writer's w_float */
static json_value_t *wnumf(warena *w, float v)
{
    char b[32];
    snprintf(b, sizeof b, "%.9g", (double)v);
    return wnum_raw(w, b);
}

/* integers: plain %d (all serialized ints are <= 20000, so exact) */
static json_value_t *wnumd(warena *w, int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    return wnum_raw(w, b);
}

static json_value_t *wbool(warena *w, int b)
{
    return wval(w, b ? json_type_true : json_type_false, NULL);
}

static json_value_t *wobj_new(warena *w)
{
    if (w->nobj >= W_OBJ_N) { w->overflow = 1; return NULL; }
    json_object_t *o = &w->obj[w->nobj];
    w->otail[w->nobj] = NULL;
    w->nobj++;
    o->start = NULL;
    o->length = 0;
    return wval(w, json_type_object, o);
}

static json_value_t *warr_new(warena *w)
{
    if (w->narr >= W_ARR_N) { w->overflow = 1; return NULL; }
    json_array_t *a = &w->arr[w->narr];
    w->atail[w->narr] = NULL;
    w->narr++;
    a->start = NULL;
    a->length = 0;
    return wval(w, json_type_array, a);
}

static int wadd(warena *w, json_value_t *ov, const char *key,
                json_value_t *vv)
{
    if (!ov || !vv || w->nel >= W_EL_N) { w->overflow = 1; return -1; }
    json_string_t *name = wstrbuf(w, key);
    if (!name) return -1;
    json_object_element_t *e = &w->el[w->nel++];
    e->name = name;
    e->value = vv;
    e->next = NULL;
    json_object_t *o = ov->payload;
    int oi = (int)(o - w->obj);
    if (!o->start) o->start = e;
    else w->otail[oi]->next = e;
    w->otail[oi] = e;
    o->length++;
    return 0;
}

static int wael(warena *w, json_value_t *av, json_value_t *vv)
{
    if (!av || !vv || w->nael >= W_AEL_N) { w->overflow = 1; return -1; }
    json_array_element_t *e = &w->ael[w->nael++];
    e->value = vv;
    e->next = NULL;
    json_array_t *a = av->payload;
    int ai = (int)(a - w->arr);
    if (!a->start) a->start = e;
    else w->atail[ai]->next = e;
    w->atail[ai] = e;
    a->length++;
    return 0;
}

#define ADD(w, obj, key, val) \
    do { if (wadd((w), (obj), (key), (val))) return -1; } while (0)
#define AEL(w, arr, val) \
    do { if (wael((w), (arr), (val))) return -1; } while (0)

/* A slot equal to the canonical OFF state (src 0, dest off, amount +0,
 * mix 0, tgt 0) is INACTIVE — omitted from the v2 array. memcmp,
 * not float ==, so -0.0 amount slots stay distinct (and round-trip). */
static int xmod_is_off(const xmod_params_t *x)
{
    static const xmod_params_t OFF;
    return memcmp(x, &OFF, sizeof *x) == 0;
}

/* Build the full DOM in the same member order the original writer used
 * (shipped presets round-trip byte-comparable after the one-time v2
 * migration). */
static int build_dom(warena *w, const synth_params_t *p)
{
    json_value_t *root = wobj_new(w);
    if (!root) return -1;
    ADD(w, root, "app", wstr(w, "ophis-drone"));
    ADD(w, root, "version", wnumd(w, 2));
    ADD(w, root, "master", wnumd(w, p->master));
    /* top-level "bpm" next to "master": the GLOBAL tempo */
    ADD(w, root, "bpm", wnumd(w, p->bpm));

    json_value_t *chains = warr_new(w);
    ADD(w, root, "chain", chains);
    for (int c = 0; c < 3; c++) {
        const chain_params_t *ch = &p->chain[c];
        json_value_t *cv = wobj_new(w);
        AEL(w, chains, cv);
        ADD(w, cv, "freq_hz", wnumf(w, ch->freq_hz));
        ADD(w, cv, "wave", wstr(w, enum_name(wave_tab, 6, ch->wave)));
        ADD(w, cv, "duty", wnumd(w, ch->duty));
        ADD(w, cv, "level", wnumd(w, ch->level));
        ADD(w, cv, "vel", wnumd(w, ch->vel));
        /* master send */
        ADD(w, cv, "mixvol", wnumd(w, ch->mixvol));
        json_value_t *subs = warr_new(w);
        ADD(w, cv, "sub", subs);
        for (int s = 0; s < 2; s++) {
            const subosc_params_t *so = &ch->sub[s];
            json_value_t *ov = wobj_new(w);
            /* Spice/Subharmonicon sub: divider, wave, duty, level */
            ADD(w, ov, "div", wnumd(w, so->div));
            ADD(w, ov, "wave", wstr(w, enum_name(wave_tab, 6, so->wave)));
            ADD(w, ov, "duty", wnumd(w, so->duty));
            ADD(w, ov, "level", wnumd(w, so->level));
            AEL(w, subs, ov);
        }
        json_value_t *adsr = wobj_new(w);
        ADD(w, cv, "adsr", adsr);
        ADD(w, adsr, "a", wnumf(w, ch->adsr.a));
        ADD(w, adsr, "d", wnumf(w, ch->adsr.d));
        ADD(w, adsr, "s", wnumd(w, ch->adsr.s));
        ADD(w, adsr, "r", wnumf(w, ch->adsr.r));
        json_value_t *filt = wobj_new(w);
        ADD(w, cv, "filter", filt);
        ADD(w, filt, "mode", wstr(w, enum_name(filt_tab, 4, ch->fmode)));
        ADD(w, filt, "fc", wnumf(w, ch->fc));
        ADD(w, filt, "res", wnumd(w, ch->res));
        ADD(w, filt, "env", wnumd(w, ch->envamt));
        ADD(w, filt, "poles", wstr(w, enum_name(poles_tab, 2, ch->poles)));
    }

    json_value_t *seqs = warr_new(w);
    ADD(w, root, "seq", seqs);
    for (int q = 0; q < 3; q++) {
        const seq_params_t *sq = &p->seq[q];
        json_value_t *qv = wobj_new(w);
        AEL(w, seqs, qv);
        /* no per-seq "bpm" */
        ADD(w, qv, "on", wbool(w, sq->on));
        ADD(w, qv, "div", wnumd(w, sq->div));
        ADD(w, qv, "mode", wstr(w, enum_name(seqmode_tab, 5, sq->mode)));
        ADD(w, qv, "once", wbool(w, sq->once));
        json_value_t *steps = warr_new(w);
        ADD(w, qv, "steps", steps);
        for (int st = 0; st < 8; st++) {
            json_value_t *sv = wobj_new(w);
            ADD(w, sv, "pitch_hz", wnumf(w, sq->steps[st].pitch_hz));
            ADD(w, sv, "vel", wnumd(w, sq->steps[st].vel));
            AEL(w, steps, sv);
        }
    }

    /* schema v2: ACTIVE slots only, in
     * ascending slot order, same 5 keys per entry — the parser fills
     * absent slots with exactly the OFF state xmod_is_off tests */
    json_value_t *xmods = warr_new(w);
    ADD(w, root, "xmod", xmods);
    for (int x = 0; x < MOD_SLOTS; x++) {
        const xmod_params_t *xp = &p->xmod[x];
        if (xmod_is_off(xp)) continue;
        json_value_t *xv = wobj_new(w);
        AEL(w, xmods, xv);
        ADD(w, xv, "src", wstr(w, enum_name(src_tab, 10, xp->src)));
        ADD(w, xv, "dest", wstr(w, enum_name(dest_tab, 13, xp->dest)));
        ADD(w, xv, "amount", wnumf(w, xp->amount));
        /* the slot's own ADD knob */
        ADD(w, xv, "mix", wnumd(w, xp->mix));
        /* routing target 0..2 */
        ADD(w, xv, "tgt", wnumd(w, xp->tgt));
    }

    /* delay / reverb: REQUIRED top-level objects */
    json_value_t *delay = wobj_new(w);
    ADD(w, root, "delay", delay);
    ADD(w, delay, "time_l", wnumf(w, p->delay.time_l));
    ADD(w, delay, "time_r", wnumf(w, p->delay.time_r));
    ADD(w, delay, "feedback", wnumd(w, p->delay.feedback));
    ADD(w, delay, "tone", wnumd(w, p->delay.tone));
    ADD(w, delay, "mix", wnumd(w, p->delay.mix));
    json_value_t *reverb = wobj_new(w);
    ADD(w, root, "reverb", reverb);
    ADD(w, reverb, "size", wnumd(w, p->reverb.size));
    ADD(w, reverb, "damp", wnumd(w, p->reverb.damp));
    ADD(w, reverb, "mix", wnumd(w, p->reverb.mix));
    ADD(w, reverb, "width", wnumd(w, p->reverb.width));

    /* top-level "safety": REQUIRED boolean */
    ADD(w, root, "safety", wbool(w, p->safety));

    return w->overflow ? -1 : 0;
}

int schema_write_params(char *buf, int buf_len, const synth_params_t *p)
{
    if (!buf || buf_len <= 0 || !p) return -1;
    warena w;
    memset(&w, 0, sizeof w);
    if (build_dom(&w, p) != 0) return -1;
    size_t out_size = 0;
    char *out = json_write_minified(&w.val[0], &out_size);
    if (!out) return -1;
    /* out_size includes the terminating NUL (json.h contract) */
    if (out_size > (size_t)buf_len) {
        free(out);
        return -1;
    }
    memcpy(buf, out, out_size);
    free(out);
    return (int)out_size - 1;
}

