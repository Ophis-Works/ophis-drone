/* tests/test_json.c — M3 preset JSON tests (PLAN §10, format per PLAN §9).
 * TDD: written BEFORE preset/json.c exists.
 * Approved plan change (commits eacb368+fix): duty on every osc and sub
 * object (always serialized, always required, 5..95 enforced); dest gains
 * "phase"; top-level delay + reverb objects are REQUIRED (engine-ignored),
 * with missing/duplicate/range violations rejecting the whole file.
 * BPM is GLOBAL (AMENDED, owner order, PLAN §6/§9): the REQUIRED
 * top-level integer "bpm" (1..500) sits next to "master"; seq objects
 * carry NO "bpm" key anymore (a stray per-seq "bpm" is an unknown key —
 * ignored). Reworked BEFORE the implementation change (strict TDD). */
#include "harness.h"
#include "dsp/mixer.h"
#include "../src/preset/schema.h"
#include <string.h>
#include <stdio.h>

/* ---- helpers (test-side) -------------------------------------------- */

/* fill p with a distinctive, full-coverage parameter set */
static void fill_params(synth_params_t *p)
{
    static const wave_t waves[6] = { WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW,
                                     WAVE_SQUARE, WAVE_WHITE, WAVE_PINK };
    static const filt_mode_t fmode[4] = { FILT_LP, FILT_HP, FILT_BP, FILT_BR };
    memset(p, 0, sizeof *p);
    p->master = 73;
    p->bpm = 137;               /* GLOBAL tempo (AMENDED): non-default so
                                    the roundtrip proves the value travels */
    p->run = 1; /* excluded from JSON — must not break the roundtrip */
    for (int c = 0; c < 3; c++) {
        chain_params_t *ch = &p->chain[c];
        ch->freq_hz = 55.0f + (float)c * 13.25f;   /* c <= 2: exact */
        ch->wave    = waves[c % 6];
        ch->duty    = 5 + ((10 + 30 * c) % 91);  /* distinct values 5..95 */
        ch->level   = 10 + c * 30;
        ch->vel     = 100 - c * 15;
        ch->adsr.a  = 0.001f * (float)(c + 1);
        ch->adsr.d  = 0.25f * (float)(c + 1);
        ch->adsr.s  = 20 + c * 25;
        ch->adsr.r  = 0.5f * (float)(c + 1);
        ch->fmode   = fmode[c % 4];
        ch->fc      = 100.0f * (float)(c + 8);
        ch->res     = (5 + 37 * c) % 101;       /* 0..100, distinct */
        ch->envamt  = (c == 0) ? -100 : (c == 1 ? 50 : 100);
        ch->poles   = (c == 1) ? 24 : 12;
        for (int s = 0; s < 2; s++) {
            subosc_params_t *so = &ch->sub[s];
            int idx = c * 2 + s;
            so->div   = 1 + (idx * 5) % 16;     /* distinct, 1..16 (O2) */
            so->wave  = waves[(idx + 2) % 4];   /* subs: sin/tri/saw/sq */
            so->duty  = 5 + ((20 + 25 * idx) % 91); /* distinct, 5..95 */
            so->level = (idx * 30) % 101;       /* distinct, 0..100 */
        }
    }
    for (int q = 0; q < 3; q++) {
        seq_params_t *sq = &p->seq[q];
        sq->on  = q % 2;
        sq->div = 1 + q * 7;    /* 1 / 8 / 15 — distinct, in 1..16 */
        /* owner-ordered modes amendment (PLAN §9): mode/once REQUIRED per
         * seq — q = 0..2 gives modes 0,1,2 and once 0,1,0 */
        sq->mode = q % 5;
        sq->once = q % 2;
        for (int st = 0; st < 8; st++) {
            sq->steps[st].pitch_hz = 27.5f * (float)(st + 1) + (float)q;
            sq->steps[st].vel      = (st * 12 + q * 7) % 101;
        }
    }
    for (int x = 0; x < 3; x++) {
        p->xmod[x].src    = x;   /* 0,1,2: off, b_osc, b_sub1 */
        p->xmod[x].dest   = x % 5;   /* covers off/pitch/phase */
        p->xmod[x].amount = -100.0f + 70.0f * (float)x;
    }
    /* delay / reverb (reserved for the FX milestone, serialized per PLAN
     * §9 — roundtrip must be bit-identical through them) */
    p->delay.time_l   = 0.37f;
    p->delay.time_r   = 0.51f;
    p->delay.feedback = 35;
    p->delay.tone     = 4500;
    p->delay.mix      = 30;
    p->reverb.size  = 85;
    p->reverb.damp  = 35;
    p->reverb.mix   = 30;
    p->reverb.width = 100;
    /* master safety filter boolean (owner-ordered FX amendment, PLAN §9:
     * REQUIRED top-level, default true). 0 here so the roundtrip proves
     * the value travels (not just the default). */
    p->safety = 0;
}

/* exact field-by-field compare, ignoring p->run (excluded from JSON) */
static int params_equal(const synth_params_t *a, const synth_params_t *b)
{
    if (memcmp(a, b, sizeof *a) == 0) return 1;
    /* memcmp differs only in run? compare everything except run */
    synth_params_t x = *a, y = *b;
    x.run = y.run = 0;
    return memcmp(&x, &y, sizeof x) == 0;
}

/* find+replace first occurrence of `from` with `to` in buf (in place,
 * buf must have room); returns 0 on success */
static int replace_once(char *buf, const char *from, const char *to)
{
    char tmp[16384];
    char *at = strstr(buf, from);
    size_t fl = strlen(from);
    if (!at) return -1;
    snprintf(tmp, sizeof tmp, "%s%s", to, at + fl);
    if (strlen(tmp) + (size_t)(at - buf) >= 16384) return -1;
    memcpy(at, tmp, strlen(tmp) + 1);
    return 0;
}

/* runtime-bound xmod slot accessor (P2, PLAN_MOD_MATRIX_16.md §1.3):
 * noinline keeps gcc from proving slot 5..15 out of bounds against
 * today's xmod[3] — the M2 type-widen (xmod[3] -> xmod[16]) must leave
 * these pins RUNTIME reds, not compile reds (§10 precedent) */
static __attribute__((noinline)) xmod_params_t *h_xmod(synth_params_t *p,
                                                       int s)
{
    return &p->xmod[s];
}

/* schema v2 (M2, PLAN_MOD_MATRIX_16.md R2): build_doc emits version 2 —
 * the "xmod" array holds 0..16 ACTIVE slots, version 1 is REJECTED.
 * build_doc_v2 is the plain 3/2/8-shape alias kept for the P2 pins. */
static int build_doc(char *buf, int n_chains, int n_subs, int n_steps,
                     int n_xmod);
static int build_doc_v2(char *buf, int n_xmod)
{
    return build_doc(buf, 3, 2, 8, n_xmod);
}

/* build a valid full document with configurable array lengths, for the
 * wrong-array-length rejection tests (xmod valid range 0..16) */
static int build_doc(char *buf, int n_chains, int n_subs, int n_steps,
                     int n_xmod)
{
    int n = snprintf(buf, 16384,
        "{\"app\":\"ophis-drone\",\"version\":2,\"master\":70,\"bpm\":60,"
        "\"chain\":[");
    for (int c = 0; c < n_chains; c++) {
        n += snprintf(buf + n, (size_t)(16384 - n),
            "%s{\"freq_hz\":55.0,\"wave\":\"saw\",\"duty\":50,"
            "\"level\":100,\"vel\":100,"
            "\"sub\":[", c ? "," : "");
        for (int s = 0; s < n_subs; s++)
            n += snprintf(buf + n, (size_t)(16384 - n),
                "%s{\"div\":1,\"wave\":\"sine\","
                "\"duty\":50,\"level\":0}", s ? "," : "");
        n += snprintf(buf + n, (size_t)(16384 - n),
            "],\"adsr\":{\"a\":0.05,\"d\":0.2,\"s\":100,\"r\":0.5},"
            "\"filter\":{\"mode\":\"lp\",\"fc\":800.0,\"res\":10,"
            "\"env\":0,\"poles\":\"12db\"}}");
    }
    n += snprintf(buf + n, (size_t)(16384 - n), "],\"seq\":[");
    for (int q = 0; q < 3; q++) {
        n += snprintf(buf + n, (size_t)(16384 - n),
            "%s{\"on\":false,\"div\":1,\"mode\":\"fwd\","
            "\"once\":false,\"steps\":[", q ? "," : "");
        for (int st = 0; st < n_steps; st++)
            n += snprintf(buf + n, (size_t)(16384 - n),
                "%s{\"pitch_hz\":55.0,\"vel\":100}", st ? "," : "");
        n += snprintf(buf + n, (size_t)(16384 - n), "]}");
    }
    n += snprintf(buf + n, (size_t)(16384 - n), "],\"xmod\":[");
    for (int x = 0; x < n_xmod; x++)
    n += snprintf(buf + n, (size_t)(16384 - n),
        "%s{\"src\":\"off\",\"dest\":\"off\",\"amount\":0.0}",
            x ? "," : "");
    n += snprintf(buf + n, (size_t)(16384 - n),
        "],\"delay\":{\"time_l\":0.38,\"time_r\":0.51,\"feedback\":35,"
        "\"tone\":4500.0,\"mix\":30},"
        "\"reverb\":{\"size\":85,\"damp\":35,\"mix\":30,\"width\":100},"
        "\"safety\":true}");
    return n < 16384 ? 0 : -1;
}

/* ---- tests ----------------------------------------------------------- */

int main(void)
{
    char buf[16384];
    synth_params_t p, q;
    char err[256];

    /* 1. write -> parse roundtrip, bit-identical to input params */
    fill_params(&p);
    int wlen = schema_write_params(buf, sizeof buf, &p);
    CHECK(wlen > 0 && wlen < 8192);        /* 8 KiB provably enough */
    memset(&q, 0xEE, sizeof q);
    CHECK(schema_parse_params(buf, wlen, &q, err, sizeof err) == 0);
    CHECK(params_equal(&p, &q));           /* every field, bit-identical */

    /* error path must not leave *p half-written (IMPLEMENTATION §7) */
    synth_params_t keep = q;
    CHECK(schema_parse_params("{", 1, &q, err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* 2. enum-string mapping — writer emits PLAN §9 lowercase strings.
     * Written doc for a distinctive param set must contain each string. */
    /* subs cover only sin/tri/saw/square now (O2); white/pink remain
     * MAIN-osc waves and are covered parse-side by the mapping loop */
    CHECK(strstr(buf, "\"saw\"") && strstr(buf, "\"square\"") &&
          strstr(buf, "\"tri\"")  && strstr(buf, "\"sine\""));
    CHECK(strstr(buf, "\"off\"") && strstr(buf, "\"pitch\"") &&
          strstr(buf, "\"phase\""));   /* fill's 3 xmod slots; amp/filter/
                                          ring/fm covered parse-side */
    /* 2c. duty is emitted on osc and sub objects (approved change: always
     * serialized, not just for square) */
    CHECK(strstr(buf, "\"duty\":"));
    /* 3 chains cover lp/hp/bp (br covered parse-side by the roundtrip's
     * enum comparison is impossible here — 3 chains, 4 modes — so the
     * parse mapping loop below is the authoritative br check) */
    CHECK(strstr(buf, "\"lp\"") && strstr(buf, "\"hp\"") &&
          strstr(buf, "\"bp\""));
    /* 3 xmod slots cover srcs 0,1,2; all 7 strings are covered by the
     * parse mapping loop below */
    CHECK(strstr(buf, "\"off\"") && strstr(buf, "\"b_osc\"") &&
          strstr(buf, "\"b_sub1\""));
    /* 2b. writer emits the per-chain filter expansion fields (PLAN §9:
     * "res"/"env"/"poles" on every filter object; poles "12db"|"24db") */
    CHECK(strstr(buf, "\"res\":") && strstr(buf, "\"env\":") &&
          strstr(buf, "\"poles\":"));
    /* 2c'. writer emits per-seq "div" (approved tabbed-UI amendment) */
    CHECK(strstr(buf, "\"div\":"));
    /* 2c''. writer emits per-seq "mode"/"once" (owner-ordered modes
     * amendment, PLAN §9); fill_params sets modes 0,1,2 so the doc
     * contains fwd, pingpong, oddeven — random/markov are covered by the
     * string-mapping loop below */
    CHECK(strstr(buf, "\"mode\":\"fwd\"") &&
          strstr(buf, "\"mode\":\"pingpong\"") &&
          strstr(buf, "\"mode\":\"oddeven\""));
    CHECK(strstr(buf, "\"once\":true") && strstr(buf, "\"once\":false"));
    CHECK(strstr(buf, "\"poles\":\"12db\"") &&
          strstr(buf, "\"poles\":\"24db\""));
    /* 2d. writer emits the top-level "safety" boolean (owner-ordered FX
     * amendment, PLAN §9); the roundtrip above already proves the value
     * survives (fill_params sets safety 0) */
    CHECK(strstr(buf, "\"safety\":"));
    /* 2e. BPM is GLOBAL (AMENDED, owner order, PLAN §6/§9): writer emits
     * the top-level "bpm" next to "master"; seq objects contain NO
     * "bpm" key; the roundtrip preserves the non-default global bpm */
    CHECK(strstr(buf, "\"master\":73,\"bpm\":137") != NULL);
    {
        const char *seq_at = strstr(buf, "\"seq\":[");
        const char *xmod_at = strstr(buf, "\"xmod\":[");
        CHECK(seq_at != NULL && xmod_at != NULL && seq_at < xmod_at);
        int bpm_in_seq = 0;
        for (const char *hit = strstr(seq_at, "\"bpm\"");
             hit && hit < xmod_at;
             hit = strstr(hit + 1, "\"bpm\""))
            bpm_in_seq = 1;
        CHECK(bpm_in_seq == 0);               /* no "bpm" key in seq objects */
    }
    CHECK(q.bpm == 137);                      /* global bpm survived roundtrip */

    /* parser maps each string to the right enum: parse a doc whose chain A
     * wave cycles through all six */
    {
        static const struct { const char *s; wave_t w; } wtab[] = {
            {"sine", WAVE_SINE}, {"tri", WAVE_TRIANGLE}, {"saw", WAVE_SAW},
            {"square", WAVE_SQUARE}, {"white", WAVE_WHITE},
            {"pink", WAVE_PINK}};
        for (int i = 0; i < 6; i++) {
            char doc[16384];
            int n = snprintf(doc, sizeof doc,
                "{\"app\":\"ophis-drone\",\"version\":2,\"master\":0,"
                "\"chain\":[{\"freq_hz\":55.0,\"wave\":\"%s\",\"level\":0,"
                "\"vel\":0,\"sub\":[{\"div\":1,\"wave\":\"sine\","
                "\"duty\":50,\"level\":0},"
                "{\"div\":1,\"wave\":\"sine\",\"duty\":50,\"level\":0}],"
                "\"adsr\":{\"a\":0.0,\"d\":0.0,\"s\":0,\"r\":0.0},"
                "\"filter\":{\"mode\":\"lp\",\"fc\":1000.0,\"res\":10,\"env\":0,\"poles\":\"12db\"}}],"
                "\"seq\":[{\"on\":false,\"steps\":[]}],"
                "\"xmod\":[]}", wtab[i].s);
            /* wrong array lengths on purpose here would fail; use full
             * lengths via build_doc instead */
            (void)n;
            /* rebuild with the target wave for chain A */
            {
                int k = snprintf(doc, sizeof doc,
                "{\"app\":\"ophis-drone\",\"version\":2,\"master\":0,"
                "\"bpm\":60,"
                    "\"chain\":[");
                for (int c = 0; c < 3; c++) {
                    const char *w = (c == 0) ? wtab[i].s
                                  : (c == 1 ? "tri" : "pink");
                    k += snprintf(doc + k, sizeof doc - (size_t)k,
                        "%s{\"freq_hz\":55.0,\"wave\":\"%s\",\"duty\":50,"
                        "\"level\":0,"
                        "\"vel\":0,\"sub\":[{\"div\":1,\"wave\":\"sine\","
                        "\"duty\":50,\"level\":0},"
                        "{\"div\":1,\"wave\":\"sine\",\"duty\":50,\"level\":0}],"
                        "\"adsr\":{\"a\":0.0,\"d\":0.0,\"s\":0,\"r\":0.0},"
                        "\"filter\":{\"mode\":\"lp\",\"fc\":1000.0,\"res\":10,\"env\":0,\"poles\":\"12db\"}}",
                        c ? "," : "", w);
                }
                k += snprintf(doc + k, sizeof doc - (size_t)k,
                    "],\"seq\":[");
                for (int sq = 0; sq < 3; sq++) {
                    k += snprintf(doc + k, sizeof doc - (size_t)k,
                        "%s{\"on\":false,\"div\":1,"
                        "\"mode\":\"fwd\",\"once\":false,\"steps\":[",
                        sq ? "," : "");
                    for (int st = 0; st < 8; st++)
                        k += snprintf(doc + k, sizeof doc - (size_t)k,
                            "%s{\"pitch_hz\":55.0,\"vel\":0}", st ? "," : "");
                    k += snprintf(doc + k, sizeof doc - (size_t)k, "]}");
                }
                snprintf(doc + k, sizeof doc - (size_t)k,
                    "],\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
                    "\"amount\":0.0},{\"src\":\"off\",\"dest\":\"off\","
                    "\"amount\":0.0},{\"src\":\"off\",\"dest\":\"off\","
                    "\"amount\":0.0}],"
                    "\"delay\":{\"time_l\":0.0,\"time_r\":0.0,"
                    "\"feedback\":0,\"tone\":20000.0,\"mix\":0},"
                    "\"reverb\":{\"size\":0,\"damp\":0,\"mix\":0,"
                    "\"width\":0},\"safety\":true}");
            }
            memset(&q, 0, sizeof q);
            CHECK(schema_parse_params(doc, (int)strlen(doc), &q,
                                    err, sizeof err) == 0);
            CHECK(q.chain[0].wave == wtab[i].w);
        }
    }

    /* xmod source string mapping (off|b_osc|b_sub1|b_sub2|c_osc|c_sub1|c_sub2) */
    {
        static const struct { const char *s; int v; } xtab[] = {
            {"off", 0}, {"b_osc", 1}, {"b_sub1", 2}, {"b_sub2", 3},
            {"c_osc", 4}, {"c_sub1", 5}, {"c_sub2", 6}};
        for (int i = 0; i < 7; i++) {
            CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
            {
                char from[128], to[128];
                snprintf(from, sizeof from,
                    "[{\"src\":\"off\",\"dest\":\"off\",\"amount\":0.0}");
                snprintf(to, sizeof to,
                    "[{\"src\":\"%s\",\"dest\":\"off\",\"amount\":0.0}",
                    xtab[i].s);
                CHECK(replace_once(buf, from, to) == 0);
            }
            memset(&q, 0, sizeof q);
            CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                    err, sizeof err) == 0);
            CHECK(q.xmod[0].src == xtab[i].v);
        }
    }

    /* 3. rejections — each leaves *p untouched */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    keep = q; /* q currently valid from previous parse */
    {
        int len = (int)strlen(buf);

        /* truncated file */
        CHECK(schema_parse_params(buf, len / 2, &q, err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));
        CHECK(schema_parse_params(buf, len - 2, &q, err, sizeof err) == -1);

        /* garbage */
        CHECK(schema_parse_params("this is not json", 16, &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* missing field: drop "app" */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"app\":\"ophis-drone\",", "") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* missing field: drop nested required field (first step vel) */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"vel\":100}", "}") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* missing field: drop top-level "safety" (owner-ordered FX
         * amendment, PLAN §9: REQUIRED boolean, default true — but the
         * LOADER requires the field per "every listed field is
         * required") */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, ",\"safety\":true", "") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* missing field: drop top-level "bpm" (AMENDED, owner order,
         * PLAN §6/§9: the GLOBAL tempo is a REQUIRED top-level integer
         * 1..500 next to "master" — a file without it is invalid) */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"bpm\":60,", "") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* range enforcement: top-level bpm integer 1..500 */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"bpm\":60", "\"bpm\":0") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"bpm\":60", "\"bpm\":501") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* boundaries 1 and 500 are valid, value lands in the struct */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"bpm\":60", "\"bpm\":1") == 0);
        memset(&q, 0, sizeof q);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == 0);
        CHECK(q.bpm == 1);
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"bpm\":60", "\"bpm\":500") == 0);
        memset(&q, 0, sizeof q);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == 0);
        CHECK(q.bpm == 500);
        q = keep;   /* restore the no-half-write baseline */

        /* wrong version: schema 1 is REJECTED (M2, R2 — the old
         * fixed-length-3 contract is gone) */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"version\":2", "\"version\":1") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* wrong app */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"app\":\"ophis-drone\"",
                                "\"app\":\"other\"") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* wrong array lengths */
        CHECK(build_doc(buf, 2, 2, 8, 3) == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(build_doc(buf, 4, 2, 8, 3) == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(build_doc(buf, 3, 1, 8, 3) == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(build_doc(buf, 3, 3, 8, 3) == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(build_doc(buf, 3, 2, 7, 3) == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(build_doc(buf, 3, 2, 9, 3) == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        /* xmod length: v2 accepts 0..16 entries — 2 is legal now, 17
         * rejects (M2, R2) */
        CHECK(build_doc(buf, 3, 2, 8, 2) == 0);
        memset(&q, 0xEE, sizeof q);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == 0);
        CHECK(h_xmod(&q, 0)->dest == MOD_OFF &&
              h_xmod(&q, 1)->dest == MOD_OFF &&
              h_xmod(&q, 2)->dest == MOD_OFF);
        q = keep;   /* restore the no-half-write baseline */
        CHECK(build_doc(buf, 3, 2, 8, 17) == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* filter expansion (PLAN §9): res/env/poles are REQUIRED parts of
         * version 2 — a file without them is missing required fields */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"res\":10,", "") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"env\":0,", "") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, ",\"poles\":\"12db\"", "") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* whitespace leniency (PLAN §9 "lenient about whitespace"):
         * a space before a literal value (safety bool) must parse —
         * lit() used to memcmp at the raw position without skipping */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"safety\":true", "\"safety\": true") == 0);
        memset(&q, 0, sizeof q);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == 0);
        CHECK(q.safety == 1);
        q = keep;   /* restore the no-half-write baseline */

        /* range enforcement: res 0..100, env -100..+100,
         * poles "12db"|"24db" only */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"res\":10", "\"res\":101") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"res\":10", "\"res\":-1") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"env\":0", "\"env\":101") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"env\":0", "\"env\":-101") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"poles\":\"12db\"", "\"poles\":\"18db\"")
              == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == -1);
        CHECK(params_equal(&q, &keep));

        /* boundary values are valid */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"res\":10,\"env\":0,\"poles\":\"12db\"",
              "\"res\":0,\"env\":-100,\"poles\":\"24db\"") == 0);
        memset(&q, 0, sizeof q);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == 0);
        CHECK(q.chain[0].res == 0 && q.chain[0].envamt == -100 &&
              q.chain[0].poles == 24);

        /* unknown keys ignored */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "{\"app\":", "{\"zzz_unknown\":12345,"
                                            "\"another\":[1,2],\"app\":") == 0);
        memset(&q, 0, sizeof q);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == 0);
        CHECK(q.master == 70 && q.bpm == 60 &&
              fexactf(q.chain[0].freq_hz, 55.0f));  /* 55.0 is exact */

        /* stray per-seq "bpm" is an UNKNOWN key now (AMENDED, PLAN §6:
         * BPM is global) — ignored, global bpm untouched */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "{\"on\":false,\"div\":1",
                            "{\"on\":false,\"bpm\":999,\"div\":1") == 0);
        memset(&q, 0, sizeof q);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                err, sizeof err) == 0);
        CHECK(q.bpm == 60);                   /* the global tempo, unchanged */
    }

    /* 4. approved change (eacb368+fix): dest "phase" — sub and xmod slots
     * parse to MOD_PHASE */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"dest\":\"off\",\"amount\":0.0}",
                        "\"dest\":\"phase\",\"amount\":100.0}") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == 0);
    /* subs have NO dest/amount (O2): the keys are unknown -> skipped */
    CHECK(q.chain[0].sub[0].div == 1);

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"src\":\"off\",\"dest\":\"off\"",
                        "{\"src\":\"off\",\"dest\":\"phase\"") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == 0);
    CHECK(q.xmod[0].dest == MOD_PHASE);

    /* 5. duty (approved change): REQUIRED on every osc and sub object,
     * range 5..95 enforced regardless of wave; roundtrip covered by the
     * fill_params roundtrip above */
    keep = q;
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"wave\":\"saw\",\"duty\":50,",
                        "\"wave\":\"saw\",") == 0);       /* osc duty gone */
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"wave\":\"sine\",\"duty\":50,",
                        "\"wave\":\"sine\",") == 0);      /* sub duty gone */
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"duty\":50", "\"duty\":4") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"duty\":50", "\"duty\":96") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* duty boundaries 5 and 95 are valid — even on a sine (always
     * enforced, not square-only) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"wave\":\"saw\",\"duty\":50",
                       "\"wave\":\"sine\",\"duty\":5") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == 0);
    CHECK(q.chain[0].duty == 5);

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"duty\":50", "\"duty\":95") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == 0);
    CHECK(q.chain[0].duty == 95);
    keep = q;

    /* 5b. seq "div" (approved tabbed-UI amendment): REQUIRED per seq,
     * integer 1..16 enforced; roundtrip covered by fill_params above */
    keep = q;
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"div\":1,", "") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);      /* missing div */
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"div\":1", "\"div\":0") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);      /* below range */
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"div\":1", "\"div\":17") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);      /* above range */
    CHECK(params_equal(&q, &keep));

    /* boundaries 1 and 16 are valid and land in the struct (the
     * pattern must hit the SEQ div — subs have a "div" of their own
     * since O2) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"on\":false,\"div\":1",
                       "\"on\":false,\"div\":16") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == 0);
    CHECK(q.seq[0].div == 16 && q.seq[1].div == 1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == 0);
    CHECK(q.seq[0].div == 1);
    keep = q;

    /* 5c. seq "mode"/"once" (owner-ordered modes amendment, PLAN §9):
     * REQUIRED per seq; mode enum strings fwd|pingpong|oddeven|random|
     * markov; once boolean. Writer covered above; here: parse mapping,
     * bad string and missing once. */
    {
        static const struct { const char *s; int v; } mtab[] = {
            {"fwd", 0}, {"pingpong", 1}, {"oddeven", 2},
            {"random", 3}, {"markov", 4}};
        for (int i = 0; i < 5; i++) {
            CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
            CHECK(replace_once(buf, "\"div\":1,\"mode\":\"fwd\",\"once\":false",
                  "\"div\":1,\"once\":true,\"mode\":\"x\"") == 0);
            /* fix the placeholder to the real target string */
            snprintf(err, sizeof err, "\"mode\":\"%s\"", mtab[i].s);
            {
                /* err-sized targets: no truncation of the err content */
                char from[256], to[256];
                snprintf(from, sizeof from, "\"mode\":\"x\"");
                snprintf(to, sizeof to, "%s", err);
                CHECK(replace_once(buf, from, to) == 0);
            }
            memset(&q, 0, sizeof q);
            CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                    err, sizeof err) == 0);
            CHECK(q.seq[0].mode == mtab[i].v && q.seq[0].once == 1);
        }
    }
    keep = q;

    /* bad mode string rejects */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"mode\":\"fwd\"", "\"mode\":\"zigzag\"") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q, err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* missing mode rejects */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"mode\":\"fwd\",", "") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q, err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* missing once rejects */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"once\":false,", "") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q, err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* 6. delay / reverb (approved change): REQUIRED top-level objects,
     * engine-ignored but parsed, ranges enforced */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf,
        "\"delay\":{\"time_l\":0.38,\"time_r\":0.51,\"feedback\":35,"
        "\"tone\":4500.0,\"mix\":30},", "") == 0);          /* missing */
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf,
        ",\"reverb\":{\"size\":85,\"damp\":35,\"mix\":30,\"width\":100}",
        "") == 0);                                          /* missing */
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* duplicate delay / reverb keys reject the whole file */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"delay\":{",
        "\"delay\":{\"time_l\":0.0,\"time_r\":0.0,\"feedback\":0,"
        "\"tone\":20.0,\"mix\":0},\"delay\":{") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"reverb\":{",
        "\"reverb\":{\"size\":0,\"damp\":0,\"mix\":0,\"width\":0},"
        "\"reverb\":{") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* range violations: time_l/time_r seconds 0..2 */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"time_l\":0.38", "\"time_l\":2.5") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"time_r\":0.51", "\"time_r\":-0.1") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* feedback / mix percent 0..100 */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"feedback\":35", "\"feedback\":101") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"feedback\":35", "\"feedback\":-1") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"mix\":30,", "\"mix\":101,") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* tone Hz 20..20000 */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"tone\":4500.0", "\"tone\":10.0") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"tone\":4500.0", "\"tone\":25000.0") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* reverb size/damp/mix/width percent 0..100 */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"size\":85", "\"size\":101") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"damp\":35", "\"damp\":-1") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"width\":100", "\"width\":101") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* delay/reverb boundaries are valid, values land in the struct */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf,
        "\"delay\":{\"time_l\":0.38,\"time_r\":0.51,\"feedback\":35,"
        "\"tone\":4500.0,\"mix\":30}",
        "\"delay\":{\"time_l\":0.0,\"time_r\":2.0,\"feedback\":100,"
        "\"tone\":20000.0,\"mix\":0}") == 0);
    CHECK(replace_once(buf,
        "\"reverb\":{\"size\":85,\"damp\":35,\"mix\":30,\"width\":100}",
        "\"reverb\":{\"size\":0,\"damp\":100,\"mix\":100,\"width\":0}")
        == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == 0);
    CHECK(fexactf(q.delay.time_l, 0.0f) && fexactf(q.delay.time_r, 2.0f) &&
          q.delay.feedback == 100 && q.delay.tone == 20000 &&
          q.delay.mix == 0);
    CHECK(q.reverb.size == 0 && q.reverb.damp == 100 &&
          q.reverb.mix == 100 && q.reverb.width == 0);

    /* run excluded from serialization: writing defaults with run=1 and
     * parsing must not require/consume a run key */
    fill_params(&p);
    p.run = 1;
    CHECK(schema_write_params(buf, sizeof buf, &p) > 0);
    CHECK(strstr(buf, "\"run\"") == NULL);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q, err, sizeof err) == 0);

    /* 7. duplicate keys reject at every object level (PLAN_CODE_REVIEW_
     * FIXES.md Phase 7: the schema layer walks the json.h DOM, which
     * preserves member order, so a second occurrence of a known key is
     * flagged). delay/reverb duplicates are covered by section 6 above;
     * every other level gets one here. Whole file rejects, *p untouched. */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q, err, sizeof err) == 0);
    keep = q;

    /* top-level: "master" twice */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"master\":70",
                       "\"master\":70,\"master\":70") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* top-level: "app" twice */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"app\":\"ophis-drone\",",
                       "\"app\":\"ophis-drone\",\"app\":\"ophis-drone\",")
          == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* chain level: "duty" twice in chain A */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"wave\":\"saw\",\"duty\":50,\"level\":100",
                       "\"wave\":\"saw\",\"duty\":50,\"duty\":50,\"level\":100")
          == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* sub level: "div" twice in chain A sub 0 */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"div\":1,\"wave\":\"sine\",",
                       "{\"div\":1,\"div\":2,\"wave\":\"sine\",")
          == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* filter level: "res" twice in chain A */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"res\":10,\"env\":0",
                       "\"res\":10,\"res\":10,\"env\":0") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* seq level: "div" twice in seq A */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"on\":false,\"div\":1,",
                       "{\"on\":false,\"div\":1,\"div\":1,") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* steps level: "vel" twice in seq A step 0 */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"pitch_hz\":55.0,\"vel\":100}",
                       "{\"pitch_hz\":55.0,\"vel\":100,\"vel\":100}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* 8. over-long keys reject (Phase 6 P5, absorbed into the Phase 7
     * schema layer): any object key longer than 15 characters is
     * rejected instead of silently truncated (no "time_lxxxx" aliasing). */
    /* a 20-char unknown key at top level rejects (all required keys still
     * present — the reject is about the key length, not a missing field) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"app\":",
                       "{\"zzzzzzzzzzzzzzzzzzzz\":0,\"app\":") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* "time_l" padded to 20 chars inside the delay object rejects
     * (prepended, so "time_l" itself is still present) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"delay\":{",
                       "\"delay\":{\"time_lxxxxxxxxxxxxxx\":0.5,\"time_l\":0.38")
          == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* boundary: a 15-char unknown key is still leniently skipped */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"app\":",
                       "{\"abcdefghijklmno\":0,\"app\":") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                            err, sizeof err) == 0);
    CHECK(q.master == 70 && q.bpm == 60);
    q = keep;

    /* 8. numeric range validation (owner mandate 2026-11-05, PLAN_
     * SCHEMA_RANGE_VALIDATION.md): the seven fields the parser read raw
     * now REJECT values outside their documented ranges — including
     * JSON-legal "1e999" (strtod -> inf -> NaN poison). Reject style:
     * -1, *p untouched. Boundaries are inclusive. */
    {
        static const struct { const char *from, *to; } bad[] = {
            /* PLAN_FREQ_DECIMALS.md P3 (R4): the floor widens to
             * 0.01 Hz — 0.05 is VALID now; 0.009 (sub-centi) rejects */
            { "\"freq_hz\":55.0",       "\"freq_hz\":0.009"  },
            { "\"freq_hz\":55.0",       "\"freq_hz\":1e999"  },
            { "\"freq_hz\":55.0",       "\"freq_hz\":20001"  },
            /* sub freq/fmode/amount keys are GONE (O2); the amount
             * bounds live on the xmod slots now */
            { "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
              "\"amount\":0.0}",
              "\"xmod\":[{\"src\":\"off\",\"dest\":\"pitch\","
              "\"amount\":51.0}"            },
            { "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
              "\"amount\":0.0}",
              "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
              "\"amount\":101.0}"            },
            { "\"a\":0.05",             "\"a\":-0.001"       },
            { "\"a\":0.05",             "\"a\":60.1"         },
            { "\"d\":0.2",              "\"d\":61.0"         },
            { "\"r\":0.5",              "\"r\":-1.0"         },
            { "\"fc\":800.0",           "\"fc\":19.0"        },
            { "\"fc\":800.0",           "\"fc\":20001.0"     },
            { "\"pitch_hz\":55.0",      "\"pitch_hz\":0.009" },
            { "\"pitch_hz\":55.0",      "\"pitch_hz\":1e999" },
            { "\"src\":\"off\",\"dest\":\"off\",\"amount\":0.0",
              "\"src\":\"off\",\"dest\":\"pitch\",\"amount\":51.0" },
        };
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
            CHECK(replace_once(buf, bad[i].from, bad[i].to) == 0);
            memset(&q, 0, sizeof q);
            CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                      err, sizeof err) == -1);
        }
        /* boundaries are inclusive: parse ok — freq_hz / step pitch_hz
         * floors are 0.01 now (PLAN_FREQ_DECIMALS.md P3, R4) */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"freq_hz\":55.0", "\"freq_hz\":0.01") == 0);
        CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
              "\"amount\":0.0}",
              "\"xmod\":[{\"src\":\"off\",\"dest\":\"pitch\","
              "\"amount\":50.0}") == 0);
        CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"pitch\","
              "\"amount\":50.0}",
              "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
              "\"amount\":100.0}") == 0);
        CHECK(replace_once(buf, "\"a\":0.05", "\"a\":60.0") == 0);
        CHECK(replace_once(buf, "\"fc\":800.0", "\"fc\":20000.0") == 0);
        CHECK(replace_once(buf, "\"pitch_hz\":55.0", "\"pitch_hz\":0.01") == 0);
        CHECK(replace_once(buf, "\"src\":\"off\",\"dest\":\"off\",\"amount\":0.0",
              "\"src\":\"off\",\"dest\":\"pitch\",\"amount\":50.0") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                  err, sizeof err) == 0);
    }

    /* 9. dest enum ring/fm bounds — XMOD slots only (subs have no
     * dest since O2). RING 0..100 (negative rejects), FM ±100 (101
     * rejects), dest "mix" GONE (M4b/D4 new format only) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"ring\","
          "\"amount\":-1.0}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"fm\","
          "\"amount\":101.0}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"mix\","
          "\"amount\":100.0}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    /* old sub keys (fmode/freq/dest/amount) are UNKNOWN in the new
     * format -> leniently skipped, parse succeeds, values ignored */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"div\":1,\"wave\":\"sine\","
          "\"duty\":50,\"level\":0}",
          "{\"fmode\":\"hz\",\"freq\":0.5,\"dest\":\"ring\","
          "\"amount\":50.0,\"div\":1,\"wave\":\"sine\","
          "\"duty\":50,\"level\":0}") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(q.chain[0].sub[0].div == 1 && q.chain[0].sub[0].level == 0);
    /* SP4 bounds: div 0 / 17 reject; level 101 reject; missing
     * div/level reject */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"div\":1,", "{\"div\":0,") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"div\":1,", "{\"div\":17,") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"level\":0}", "\"level\":101}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "{\"div\":1,\"wave\":\"sine\",",
                       "{\"wave\":\"sine\",") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, ",\"level\":0}", "}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);

    /* xmod matrix ring/fm parse too */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"ring\","
          "\"amount\":100.0}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(q.xmod[0].dest == MOD_RING);

    /* D2 (PLAN_OPTIMIZATION.md O1): the mod-source id table is ONE
     * source of truth — the frozen historical ids (1..6 = B/C, 7..9 =
     * A; M2 compat) and schema's src names must agree with modsrc_get */
    {
        static const struct { int id, chain, sub; } FROZEN[9] = {
            { 1, 1, -1 }, { 2, 1, 0 }, { 3, 1, 1 },
            { 4, 2, -1 }, { 5, 2, 0 }, { 6, 2, 1 },
            { 7, 0, -1 }, { 8, 0, 0 }, { 9, 0, 1 },
        };
        static const char *const NAMES[9] = {
            "b_osc", "b_sub1", "b_sub2", "c_osc", "c_sub1", "c_sub2",
            "a_osc", "a_sub1", "a_sub2"
        };
        for (int i = 0; i < 9; i++) {
            const modsrc_t *ms = modsrc_get(FROZEN[i].id);
            CHECK(ms != NULL && ms->chain == FROZEN[i].chain &&
                  ms->sub == FROZEN[i].sub);
            CHECK(modsrc_get(0) == NULL);       /* OFF has no source */
        }
        /* every id's schema name round-trips: parse a doc per name */
        for (int i = 0; i < 9; i++) {
            CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
            char pat[32], rep[40];
            snprintf(pat, sizeof pat, "\"src\":\"off\"");
            snprintf(rep, sizeof rep, "\"src\":\"%s\"", NAMES[i]);
            CHECK(replace_once(buf, pat, rep) == 0);
            memset(&q, 0, sizeof q);
            CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                                      err, sizeof err) == 0);
            CHECK(q.xmod[0].src == FROZEN[i].id);
            const modsrc_t *ms = modsrc_get(q.xmod[0].src);
            CHECK(ms && ms->chain == FROZEN[i].chain &&
                  ms->sub == FROZEN[i].sub);
        }
    }

    /* P6 (PLAN_MOD_ROUTING.md M4): CHFM is REMOVED — a preset carrying
     * "chfm" parses (generic unknown-key skip) and nothing else changes */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"level\":100,\"vel\":100",
          "\"level\":100,\"vel\":100,\"chfm\":50") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(fexactf(q.chain[0].freq_hz, 55.0f));  /* 55.0 exact; parsed normally */

    /* tgt (PLAN_MOD_ROUTING.md M1): optional 0..2, default 0 = A;
     * out-of-range rejects (P5 tgt part / P7) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);   /* no tgt key: default 0 */
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(q.xmod[0].tgt == 0 && q.xmod[2].tgt == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0,\"tgt\":2}") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(q.xmod[0].tgt == 2);
    CHECK(replace_once(buf, "\"tgt\":2", "\"tgt\":3") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0,\"tgt\":-1}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);

    /* A sources (PLAN_MOD_ROUTING.md M2): ids 7..9 = a_osc/a_sub1/
     * a_sub2 — APPENDED after the B/C ids so old presets keep their
     * meaning (src 1 is still B.osc); self-routing legal (D3 yes) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"a_osc\",\"dest\":\"off\","
          "\"amount\":0.0}") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(q.xmod[0].src == 7);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"src\":\"off\"",
          "\"src\":\"a_sub2\"") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(q.xmod[0].src == 9);
    /* mixvol (PLAN_MOD_ROUTING.md M3): optional 0..100 per chain,
     * default 100 (absent must NOT mean silent) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);   /* no mixvol key */
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(q.chain[0].mixvol == 100 && q.chain[2].mixvol == 100);
    CHECK(replace_once(buf, "\"level\":100,\"vel\":100",
          "\"level\":100,\"vel\":100,\"mixvol\":0") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);
    CHECK(q.chain[0].mixvol == 0 && q.chain[1].mixvol == 100);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"level\":100,\"vel\":100",
          "\"level\":100,\"vel\":100,\"mixvol\":101") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"level\":100,\"vel\":100",
          "\"level\":100,\"vel\":100,\"mixvol\":-1") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);

    /* out of range: id 10 has no name -> reject */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"src\":\"off\"",
          "\"src\":\"nosuch\"") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);

    /* 10. PR3 (PLAN_OSC_NAMING.md §1.0e/R0g, §1.4): the dest split —
     * "flt_res" parses (amount 0) and rejects amount 101 (±100, same
     * percent rule), "flt_cut" parses, and the old "filter" dest
     * REJECTS (new format only, §1.0e). Red until schema_tab.c's
     * dest_tab becomes the 8-entry off/pitch/phase/amp/flt_cut/
     * flt_res/ring/fm table. Kept string-level (no MOD_FRES reference)
     * so the reds here stay RUNTIME reds, not compile reds. */
    /* "flt_res" parses with amount 0 */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"flt_res\","
          "\"amount\":0.0}") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);

    /* "flt_res" amount 101 rejects (±100) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"flt_res\","
          "\"amount\":101.0}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);

    /* "flt_cut" parses */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"flt_cut\","
          "\"amount\":0.0}") == 0);
    memset(&q, 0, sizeof q);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == 0);

    /* the old "filter" dest REJECTS (new format only) */
    CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
    CHECK(replace_once(buf, "\"xmod\":[{\"src\":\"off\",\"dest\":\"off\","
          "\"amount\":0.0}",
          "\"xmod\":[{\"src\":\"off\",\"dest\":\"filter\","
          "\"amount\":0.0}") == 0);
    CHECK(schema_parse_params(buf, (int)strlen(buf), &q,
                              err, sizeof err) == -1);

    /* 11. P2 (PLAN_MOD_MATRIX_16.md §1.3, M2): schema v2 — the "xmod"
     * array holds ONLY the active slots, 0..16 entries; slots missing
     * from the array are OFF (dest off); version key must be 2 (version
     * 1 REJECTED — the fixed-length-3 contract is gone); "xmod" stays
     * REQUIRED (empty array = matrix fully off). RED pre-M2 (parser
     * demanded version 1 and exactly 3 entries; see the M2 PIN log
     * section), GREEN post-M2. Runtime-bound h_xmod access kept from the
     * pin — now plain in-bounds slot reads on xmod[16]. */
    {
        synth_params_t v2;

        /* 5 active entries parse; array index == slot index (entry 0 is
         * made ACTIVE to prove the positional mapping); slots 5..15 are
         * OFF (dest off — vacuous until the M2 widen makes them exist) */
        CHECK(build_doc_v2(buf, 5) == 0);
        CHECK(replace_once(buf,
              "{\"src\":\"off\",\"dest\":\"off\",\"amount\":0.0}",
              "{\"src\":\"b_osc\",\"dest\":\"pitch\",\"amount\":50.0}")
              == 0);
        memset(&v2, 0xEE, sizeof v2);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &v2,
                                  err, sizeof err) == 0);
        CHECK(h_xmod(&v2, 0)->src == 1 &&
              h_xmod(&v2, 0)->dest == MOD_PITCH);
        CHECK(fexactf(h_xmod(&v2, 0)->amount, 50.0f));
        CHECK(h_xmod(&v2, 4)->src == 0);       /* the 5th entry, still off */
        for (int s = 5; s < 16; s++)
            CHECK(h_xmod(&v2, s)->dest == MOD_OFF);
        CHECK((int)(sizeof v2.xmod / sizeof v2.xmod[0]) == 16);

        /* 17 entries REJECT — the reject names the ARRAY (its length),
         * not the version */
        CHECK(build_doc_v2(buf, 17) == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &v2,
                                  err, sizeof err) == -1);
        CHECK(strstr(err, "xmod") != NULL);

        /* version 1 REJECTS — the old contract is gone */
        CHECK(build_doc(buf, 3, 2, 8, 3) == 0);
        CHECK(replace_once(buf, "\"version\":2", "\"version\":1") == 0);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &v2,
                                  err, sizeof err) == -1);
        CHECK(strstr(err, "version") != NULL);

        /* empty xmod array parses — matrix fully off (all 16 slots
         * dest off) */
        CHECK(build_doc_v2(buf, 0) == 0);
        memset(&v2, 0xEE, sizeof v2);
        CHECK(schema_parse_params(buf, (int)strlen(buf), &v2,
                                  err, sizeof err) == 0);
        for (int s = 0; s < 16; s++)
            CHECK(h_xmod(&v2, s)->dest == MOD_OFF);
    }

    HARNESS_REPORT();
}
