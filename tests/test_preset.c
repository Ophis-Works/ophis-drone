/* tests/test_preset.c — M3 preset file I/O + factory defaults tests
 * (PLAN §10, contracts per IMPLEMENTATION.md §2/§7). TDD: written BEFORE
 * preset/preset.c / preset/defaults.c exist.
 * Approved plan change (commits eacb368+fix): OPHIS_DEFAULTS gains
 * duty 50 on every main osc and sub; delay {time_l 0.0, time_r 0.0,
 * feedback 0, tone 20000, mix 0}; reverb {size 0, damp 0, mix 0,
  * width 0} — silent defaults, engine-ignored until the FX milestone.
  * BPM is GLOBAL (AMENDED, owner order, PLAN §6): one top-level bpm
  * (roundtrip must preserve it); seq entries carry no bpm. */
#include "harness.h"
#include "../src/preset/preset.h"
#include "../src/preset/defaults.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* exact field-by-field compare, ignoring p->run (excluded from JSON) */
static int params_equal(const synth_params_t *a, const synth_params_t *b)
{
    synth_params_t x = *a, y = *b;
    x.run = y.run = 0;
    return memcmp(&x, &y, sizeof x) == 0;
}

static void fill_params(synth_params_t *p)
{
    memset(p, 0, sizeof *p);
    p->master = 88;
    p->bpm = 137;              /* GLOBAL tempo (AMENDED): non-default so the
                                  roundtrip proves the value travels */
    for (int c = 0; c < 3; c++) {
        p->chain[c].freq_hz = 41.5f * (float)(c + 1);
        p->chain[c].wave    = (wave_t)(c + 1);
        p->chain[c].duty    = 5 + 20 * c;            /* 5/25/45 */
        p->chain[c].sub[0].duty = 95;
        p->chain[c].sub[1].div   = 5 + 2 * c;        /* 5/7/9, in 1..16 */
        p->chain[c].sub[1].wave  = WAVE_SAW;
        p->chain[c].sub[1].duty  = 5 + 45 * c;        /* 5/50/95 */
        p->chain[c].sub[1].level = 70 - 20 * c;       /* 70/50/30 */
        p->chain[c].level   = 11 * (c + 1);
        p->chain[c].vel     = 33 * (c + 1);
        p->chain[c].adsr.a  = 0.01f; p->chain[c].adsr.d = 0.3f;
        p->chain[c].adsr.s  = 44;    p->chain[c].adsr.r = 1.5f;
        p->chain[c].fmode   = FILT_HP;  p->chain[c].fc = 500.0f * (float)(c + 1);
        p->chain[c].res     = 25 * (c + 1);          /* 25/50/75 */
        p->chain[c].envamt  = (c == 1) ? -75 : 50;
        p->chain[c].poles   = (c == 1) ? 12 : 24;
        p->chain[c].sub[0].div   = 2 + c;      /* distinct dividers 2/3/4 */
        p->chain[c].sub[0].wave  = WAVE_TRIANGLE;
        p->chain[c].sub[0].level = 20 * (c + 1);   /* 20/40/60 */
    }
    for (int q = 0; q < 3; q++) {
        p->seq[q].on = 1;      /* no per-seq bpm (GLOBAL, AMENDED, PLAN §6) */
        p->seq[q].div = 1 + 6 * q;   /* 1/7/13 — distinct, in 1..16 */
        /* owner-ordered modes amendment: seq gains mode (step order) and
         * once (loop=0/once=1) — PLAN §6/§9 REQUIRED fields */
        p->seq[q].mode = (q == 0) ? 1 : (q == 1 ? 2 : 4); /* 1/2/4; mode 3
                                     (markov) covered in main below */
        p->seq[q].once = q % 2;      /* 0/1/0 */
        for (int s = 0; s < 8; s++) {
            p->seq[q].steps[s].pitch_hz = 110.0f * (float)(s + 1);
            p->seq[q].steps[s].vel = (s * 5 + q) % 101;
        }
    }
    p->xmod[1].src = 2; p->xmod[1].dest = 3; p->xmod[1].amount = 66.0f;
    p->delay.time_l = 0.42f; p->delay.time_r = 0.77f;
    p->delay.feedback = 55;  p->delay.tone = 6000;  p->delay.mix = 25;
    p->reverb.size = 70; p->reverb.damp = 40; p->reverb.mix = 35;
    p->reverb.width = 90;
    p->safety = 0; /* owner-ordered FX amendment: serialized boolean */
    p->run = 1; /* excluded from presets */
}

int main(void)
{
    char dirbuf[512], err[256];
    synth_params_t p, q;

    /* temp dir via mkdtemp (PLAN §10: roundtrip in a temp dir) */
    snprintf(dirbuf, sizeof dirbuf, "/tmp/ophis_test_preset.XXXXXX");
    char *dir = mkdtemp(dirbuf);
    CHECK(dir != NULL);

    /* 1. save -> list -> load roundtrip */
    fill_params(&p);
    p.seq[1].mode = 3;  /* owner-ordered modes amendment: fill_params modes
                           are 1/2/4; markov (3) rides along in slot 1 so
                           the roundtrip covers modes 1..4 and once 0/1 */
    CHECK(preset_save(dir, "round_trip", &p, err, sizeof err) == 0);
    memset(&q, 0xEE, sizeof q);
    CHECK(preset_load(dir, "round_trip", &q, err, sizeof err) == 0);
    CHECK(params_equal(&p, &q));            /* bit-identical except run */

    char names[16][33];
    int count = -1;
    CHECK(preset_list(dir, names, &count, 16) == 0);
    CHECK(count == 1 && strcmp(names[0], "round_trip") == 0);

    /* 2. name sanitization */
    fill_params(&p);
    /* rejects */
    CHECK(preset_save(dir, "", &p, err, sizeof err) == -1);
    CHECK(preset_save(dir, ".", &p, err, sizeof err) == -1);
    CHECK(preset_save(dir, "..", &p, err, sizeof err) == -1);
    CHECK(preset_save(dir, "a/b", &p, err, sizeof err) == -1);
    CHECK(preset_save(dir, "/etc", &p, err, sizeof err) == -1);
    CHECK(preset_save(dir, "this_name_is_way_too_long_for_a_preset_x", &p,
                      err, sizeof err) == -1);         /* 33 chars */
    CHECK(preset_save(dir, "bad!name", &p, err, sizeof err) == -1);
    CHECK(preset_save(dir, "semi;colon", &p, err, sizeof err) == -1);
    /* allows: 1..32 chars from [A-Za-z0-9 _-] */
    CHECK(preset_save(dir, "Ok_Name-01 x", &p, err, sizeof err) == 0);
    CHECK(preset_save(dir, "abcdefghijklmnopqrstuvwxyzAB", &p,
                      err, sizeof err) == 0);          /* exactly 32 */
    /* rejected names must not have written files */
    count = -1;
    CHECK(preset_list(dir, names, &count, 16) == 0);
    CHECK(count == 3);

    /* 3. list alphabetical */
    CHECK(preset_save(dir, "mid", &p, err, sizeof err) == 0);
    CHECK(preset_save(dir, "beta", &p, err, sizeof err) == 0);
    CHECK(preset_save(dir, "alpha", &p, err, sizeof err) == 0);
    count = -1;
    CHECK(preset_list(dir, names, &count, 16) == 0);
    CHECK(count == 6);
    /* alphabetical: alpha, beta, mid, Ok_Name-01 x, ... — byte order puts
     * uppercase 'O' (0x4F) before lowercase; assert the full ordering */
    CHECK(strcmp(names[0], "Ok_Name-01 x") == 0 ||
          strcmp(names[0], "alpha") == 0);
    /* strictly ascending strcmp order */
    int ordered = 1;
    for (int i = 1; i < count; i++)
        if (strcmp(names[i - 1], names[i]) > 0) ordered = 0;
    CHECK(ordered);

    /* 4. invalid file rejected with error, params unchanged */
    fill_params(&p);
    char badpath[512];
    snprintf(badpath, sizeof badpath, "%s/bad.json", dir);
    FILE *f = fopen(badpath, "w");
    CHECK(f != NULL);
    fputs("{{{ totally not a preset", f);
    fclose(f);
    memset(&q, 0x5A, sizeof q);
    CHECK(preset_load(dir, "bad", &q, err, sizeof err) == -1);
    CHECK(err[0] != '\0');                     /* an error is reported */
    synth_params_t keep = q;
    /* truncated valid file also rejected, params unchanged */
    CHECK(preset_save(dir, "good", &p, err, sizeof err) == 0);
    snprintf(badpath, sizeof badpath, "%s/good.json", dir);
    if (truncate(badpath, (long)0) != 0) { }   /* start clean below */
    {
        /* overwrite good.json with the first half of its content */
        CHECK(preset_save(dir, "good", &p, err, sizeof err) == 0);
        FILE *fr = fopen(badpath, "r");
        char body[16384];
        size_t got = fread(body, 1, sizeof body, fr);
        fclose(fr);
        FILE *fw = fopen(badpath, "w");
        fwrite(body, 1, got / 2, fw);
        fclose(fw);
    }
    CHECK(preset_load(dir, "good", &q, err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* missing file rejected too */
    CHECK(preset_load(dir, "no_such_preset", &q, err, sizeof err) == -1);
    CHECK(params_equal(&q, &keep));

    /* 5. OPHIS_DEFAULTS sanity (IMPLEMENTATION.md §2) */
    const synth_params_t *d = &OPHIS_DEFAULTS;
    CHECK(d->master == 70 && d->run == 0);
    /* GLOBAL tempo (AMENDED, owner order, PLAN §6): one top-level bpm 60 */
    CHECK(d->bpm == 60);
    /* A: 55.0 saw, level 100, vel 100, subs off, A.05/D.2/S100/R.5, LP800 */
    CHECK(fexactf(d->chain[0].freq_hz, 55.0f) && d->chain[0].wave == WAVE_SAW);
    CHECK(d->chain[0].level == 100 && d->chain[0].vel == 100);
    CHECK(fexactf(d->chain[0].adsr.a, 0.05f) &&
          fexactf(d->chain[0].adsr.d, 0.2f) &&
          d->chain[0].adsr.s == 100 && fexactf(d->chain[0].adsr.r, 0.5f));
    CHECK(d->chain[0].fmode == FILT_LP && fexactf(d->chain[0].fc, 800.0f));
    /* B: 55.5 square, level 50, vel 100, subs silent (O2 voices:
       div 1 sine level 0), A.05/D.2/S100/R.5, LP600 */
    CHECK(fexactf(d->chain[1].freq_hz, 55.5f) && d->chain[1].wave == WAVE_SQUARE);
    CHECK(d->chain[1].level == 50 && d->chain[1].vel == 100);
    CHECK(fexactf(d->chain[1].adsr.a, 0.05f) &&
          fexactf(d->chain[1].adsr.d, 0.2f) &&
          d->chain[1].adsr.s == 100 && fexactf(d->chain[1].adsr.r, 0.5f));
    CHECK(d->chain[1].fmode == FILT_LP && fexactf(d->chain[1].fc, 600.0f));
    CHECK(d->chain[1].sub[0].div == 1 &&
          d->chain[1].sub[0].wave == WAVE_SINE &&
          d->chain[1].sub[0].level == 0);
    CHECK(d->chain[1].sub[1].level == 0);
    /* C: 110.0 sine, level 30, vel 100, subs off, A.1/D.3/S80/R1.0, LP1200 */
    CHECK(fexactf(d->chain[2].freq_hz, 110.0f) && d->chain[2].wave == WAVE_SINE);
    CHECK(d->chain[2].level == 30 && d->chain[2].vel == 100);
    CHECK(fexactf(d->chain[2].adsr.a, 0.1f) &&
          fexactf(d->chain[2].adsr.d, 0.3f) &&
          d->chain[2].adsr.s == 80 && fexactf(d->chain[2].adsr.r, 1.0f));
    CHECK(d->chain[2].fmode == FILT_LP && fexactf(d->chain[2].fc, 1200.0f));
    CHECK(d->chain[2].sub[0].level == 0 &&
          d->chain[2].sub[1].level == 0);
    /* A subs silent too */
    CHECK(d->chain[0].sub[0].level == 0 &&
          d->chain[0].sub[1].level == 0);
    /* filter expansion defaults: res 10, envamt 0, poles 12 on ALL chains
     * (approved plan: recorded in preset/defaults.c) */
    for (int c = 0; c < 3; c++) {
        CHECK(d->chain[c].res == 10 && d->chain[c].envamt == 0 &&
              d->chain[c].poles == 12);
    }
    /* square DUTY defaults (approved change eacb368+fix): duty 50 on
     * every main osc and every sub */
    for (int c = 0; c < 3; c++) {
        CHECK(d->chain[c].duty == 50);
        CHECK(d->chain[c].sub[0].duty == 50 &&
              d->chain[c].sub[1].duty == 50);
    }
    /* delay / reverb factory defaults (approved change, exact recorded
     * values — silent: time 0 s, feedback 0, tone 20000 Hz, mix 0;
     * reverb all-zero) */
    CHECK(fexactf(d->delay.time_l, 0.0f) && fexactf(d->delay.time_r, 0.0f));
    CHECK(d->delay.feedback == 0 && d->delay.tone == 20000 &&
          d->delay.mix == 0);
    CHECK(d->reverb.size == 0 && d->reverb.damp == 0 &&
          d->reverb.mix == 0 && d->reverb.width == 0);
    /* master safety filter default ON (owner-ordered FX amendment) */
    CHECK(d->safety == 1);
    /* seq: all off, div 1 (approved tabbed-UI amendment), mode fwd (0),
     * once loop (0) (owner-ordered modes amendment), all steps {55.0 Hz,
     * vel 100} — NO per-seq bpm (GLOBAL, AMENDED, PLAN §6) */
    for (int s = 0; s < 3; s++) {
        CHECK(d->seq[s].on == 0);
        CHECK(d->seq[s].div == 1);
        CHECK(d->seq[s].mode == 0 && d->seq[s].once == 0);
        for (int st = 0; st < 8; st++)
            CHECK(fexactf(d->seq[s].steps[st].pitch_hz, 55.0f) &&
                  d->seq[s].steps[st].vel == 100);
    }
    /* xmod: all OFF */
    for (int x = 0; x < 3; x++)
        CHECK(d->xmod[x].src == 0 && d->xmod[x].dest == 0 &&
              fexactf(d->xmod[x].amount, 0.0f));

    /* 6. preset_dir() — THE ONLY rule (user-set): presets/ next to the
     * running executable, computed at runtime from /proc/self/exe.
     * No env var, no HOME, no fallbacks, no error taxonomy. Pins:
     * byte-exact resolution, argument validation (B3), exact-fit buffer
     * boundary. The /proc/self/exe-unreadable guards (B1/B2/B4) cannot
     * be triggered in-process — code-covered only, per
     * PLAN_PRESET_DIR_REVIEW_FIXES.md. */
    {
        char db[512], exe[1024], want[1024];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        CHECK(n > 0);
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        CHECK(slash != NULL);
        int m = snprintf(want, sizeof want, "%.*s/presets",
                         (int)(slash - exe), exe);
        CHECK(m > 0 && m < (int)sizeof want);
        CHECK(preset_dir(db, (int)sizeof db) == 0);
        CHECK(strcmp(db, want) == 0);      /* byte-exact */
        /* argument validation: NULL out / non-positive length -> -1,
         * never UB */
        CHECK(preset_dir(NULL, 512) == -1);
        CHECK(preset_dir(db, 0) == -1);
        CHECK(preset_dir(db, -1) == -1);
        /* exact-fit boundary: strlen(want) chars need strlen+1 bytes */
        CHECK(preset_dir(db, (int)strlen(want)) == -1);
        CHECK(preset_dir(db, (int)strlen(want) + 1) == 0);
        CHECK(strcmp(db, want) == 0);
    }

    /* 6b. preset_ensure_dir (F1a, user order 2026-11-05): mkdir -p over
     * every missing prefix, EEXIST tolerated. LOAD opens the presets dir
     * through this, so a fresh install finds an empty-but-existing
     * folder. -1 on NULL/empty/too-long/not creatable. */
    {
        const char *base = "build/tests/ensure_dir_probe";
        char deep[128];
        CHECK(preset_ensure_dir(NULL) == -1);
        CHECK(preset_ensure_dir("") == -1);
        char big[1200];
        memset(big, 'a', sizeof big - 1);
        big[sizeof big - 1] = '\0';
        CHECK(preset_ensure_dir(big) == -1);
        /* one call creates the whole chain; rerun is EEXIST-ok */
        CHECK(snprintf(deep, sizeof deep, "%s/a/b/c", base) > 0);
        CHECK(preset_ensure_dir(deep) == 0);
        struct stat st;
        CHECK(stat(deep, &st) == 0 && S_ISDIR(st.st_mode));
        CHECK(preset_ensure_dir(base) == 0);
        /* a FILE in the middle of the path is not creatable (ff: not the
         * outer FILE *f) */
        FILE *ff = fopen("build/tests/ensure_file", "w");
        CHECK(ff != NULL);
        if (ff) fclose(ff);
        CHECK(preset_ensure_dir("build/tests/ensure_file/x") == -1);
        unlink("build/tests/ensure_file");
        /* cleanup */
        rmdir("build/tests/ensure_dir_probe/a/b/c");
        rmdir("build/tests/ensure_dir_probe/a/b");
        rmdir("build/tests/ensure_dir_probe/a");
        rmdir("build/tests/ensure_dir_probe");
    }

    /* 8. preset_path behavior pins (X2, PLAN_CODE_REVIEW_FIXES.md
     * Phase 8): the lexical normalizer's accepted behavior, pinned BEFORE
     * the component-stack normalizer is simplified to the dir-walk walk —
     * the same pins must stay green after. Categories: absolute /
     * relative components, ".." pops, "." drops, duplicate components,
     * empty components. The navigator only feeds real subdir names and
     * "..", but the pins document the full accepted behavior. */
    {
        char pb[1024];
        /* absolute components */
        CHECK(preset_path(pb, (int)sizeof pb, "/a/b", "c") == 0);
        CHECK(strcmp(pb, "/a/b/c") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "/", "a") == 0);
        CHECK(strcmp(pb, "/a") == 0);              /* "//a" empties dropped */
        CHECK(preset_path(pb, (int)sizeof pb, "//a", "b") == 0);
        CHECK(strcmp(pb, "/a/b") == 0);            /* duplicate slashes */
        CHECK(preset_path(pb, (int)sizeof pb, "/a", "b/../c") == 0);
        CHECK(strcmp(pb, "/a/c") == 0);            /* component ".." pops */
        CHECK(preset_path(pb, (int)sizeof pb, "/a/..", "c") == 0);
        CHECK(strcmp(pb, "/c") == 0);
        /* absolute "..": pops, clamped at the root */
        CHECK(preset_path(pb, (int)sizeof pb, "/a/b", "..") == 0);
        CHECK(strcmp(pb, "/a") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "/a", "..") == 0);
        CHECK(strcmp(pb, "/") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "/", "..") == 0);
        CHECK(strcmp(pb, "/") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "", "..") == 0);
        CHECK(strcmp(pb, "/") == 0);     /* empty dir joins "/" -> absolute */
        /* relative components */
        CHECK(preset_path(pb, (int)sizeof pb, "a", "b") == 0);
        CHECK(strcmp(pb, "a/b") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "a/b", "..") == 0);
        CHECK(strcmp(pb, "a") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "a/b/c", "..") == 0);
        CHECK(strcmp(pb, "a/b") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "../a", "b") == 0);
        CHECK(strcmp(pb, "../a/b") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "a", "../../b") == 0);
        CHECK(strcmp(pb, "../b") == 0);   /* below-root ".." kept literally */
        CHECK(preset_path(pb, (int)sizeof pb, "a/..", "b") == 0);
        CHECK(strcmp(pb, "b") == 0);
        /* relative pop to empty -> "." (degenerate) */
        CHECK(preset_path(pb, (int)sizeof pb, "a", "..") == 0);
        CHECK(strcmp(pb, ".") == 0);
        /* "." components drop */
        CHECK(preset_path(pb, (int)sizeof pb, "/a", ".") == 0);
        CHECK(strcmp(pb, "/a") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, ".", "a") == 0);
        CHECK(strcmp(pb, "a") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, ".", ".") == 0);
        CHECK(strcmp(pb, ".") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, ".", "..") == 0);
        CHECK(strcmp(pb, "..") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "a/b", "./c") == 0);
        CHECK(strcmp(pb, "a/b/c") == 0);
        /* duplicate components are NOT deduplicated */
        CHECK(preset_path(pb, (int)sizeof pb, "a", "a") == 0);
        CHECK(strcmp(pb, "a/a") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "a", "../a") == 0);
        CHECK(strcmp(pb, "a") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "a//b", "c") == 0);
        CHECK(strcmp(pb, "a/b/c") == 0);
        /* empty components: empty name resolves to dir itself */
        CHECK(preset_path(pb, (int)sizeof pb, "/a", "") == 0);
        CHECK(strcmp(pb, "/a") == 0);
        CHECK(preset_path(pb, (int)sizeof pb, "a", "") == 0);
        CHECK(strcmp(pb, "a") == 0);
        /* errors */
        CHECK(preset_path(NULL, 8, "/a", "b") == -1);
        CHECK(preset_path(pb, 0, "/a", "b") == -1);
        CHECK(preset_path(pb, (int)sizeof pb, NULL, "b") == -1);
        CHECK(preset_path(pb, (int)sizeof pb, "/a", NULL) == -1);
        CHECK(preset_path(pb, 4, "/a/b", "c") == -1);  /* out too small */
        CHECK(preset_path(pb, (int)sizeof pb, "/a", "b") == 0);
    }

    /* cleanup temp dir */
    snprintf(badpath, sizeof badpath,
             "rm -rf '%s'", dir);
    if (system(badpath) != 0) { /* best-effort cleanup */ }

    HARNESS_REPORT();
}
