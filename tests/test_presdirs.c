/* tests/test_presdirs.c — LOAD folder navigator contracts:
 *   preset_list_dirs: subdirectories of dir + "..", alphabetical.
 *   preset_path: resolves <dir>/<name> (navigator uses it to enter a
 *   listed subdirectory, including "..").
 * Navigation scenario: list dir -> enter subdir -> ".." returns.
 * TDD: written BEFORE preset_list_dirs/preset_path are implemented
 * (declarations already in preset.h). */
#include "harness.h"
#include "../src/preset/preset.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static synth_params_t mkparams(void)
{
    synth_params_t p;
    memset(&p, 0, sizeof p);
    p.master = 50;
    return p;
}

static int has_name(char names[][33], int count, const char *n)
{
    for (int i = 0; i < count; i++)
        if (strcmp(names[i], n) == 0) return 1;
    return 0;
}

int main(void)
{
    char dirbuf[512], path[1024], path2[1024], err[256];
    char names[16][33];
    int count = -1;

    snprintf(dirbuf, sizeof dirbuf, "/tmp/ophis_test_presdirs.XXXXXX");
    char *root = mkdtemp(dirbuf);
    CHECK(root != NULL);

    /* layout:  root/
     *          root/alpha/           (contains a preset + subdir inner)
     *          root/alpha/inner/
     *          root/beta/
     *          root/preset.json      (file: must NOT appear in list_dirs) */
    snprintf(path, sizeof path, "%s/alpha", root);
    CHECK(mkdir(path, 0755) == 0);
    snprintf(path, sizeof path, "%s/beta", root);
    CHECK(mkdir(path, 0755) == 0);
    snprintf(path, sizeof path, "%s/alpha/inner", root);
    CHECK(mkdir(path, 0755) == 0);
    synth_params_t p = mkparams();
    CHECK(preset_save(root, "top", &p, err, sizeof err) == 0);
    CHECK(preset_save(root, "alpha", &p, err, sizeof err) == 0); /* file in
                                    a same-named dir must not confuse */

    /* 1. list_dirs: subdirs + "..", alphabetical, files excluded */
    CHECK(preset_list_dirs(root, names, &count, 16) == 0);
    CHECK(count == 3);                        /* "..", alpha, beta */
    CHECK(has_name(names, count, ".."));
    CHECK(has_name(names, count, "alpha"));
    CHECK(has_name(names, count, "beta"));
    CHECK(!has_name(names, count, "top"));    /* *.json excluded */
    CHECK(!has_name(names, count, "alpha.json"));
    /* strictly ascending strcmp order — ".." (0x2E) sorts first */
    int ordered = 1;
    for (int i = 1; i < count; i++)
        if (strcmp(names[i - 1], names[i]) > 0) ordered = 0;
    CHECK(ordered);
    CHECK(strcmp(names[0], "..") == 0);

    /* 2. preset_path resolves dir/name */
    CHECK(preset_path(path, sizeof path, root, "alpha") == 0);
    snprintf(path2, sizeof path2, "%s/alpha", root);
    CHECK(strcmp(path, path2) == 0);
    CHECK(preset_path(path, sizeof path, root, "..") == 0);
    {
        /* normalized: entering ".." yields the parent path itself
         * (root with its last component stripped) */
        char parent[1024];
        snprintf(parent, sizeof parent, "%s", root);
        char *slash = strrchr(parent, '/');
        CHECK(slash != NULL);
        if (slash != parent) *slash = '\0'; else slash[1] = '\0';
        CHECK(strcmp(path, parent) == 0);
    }

    /* 3. navigation scenario: list dir -> enter subdir -> ".." returns */
    CHECK(preset_list_dirs(root, names, &count, 16) == 0);
    CHECK(has_name(names, count, "alpha"));
    CHECK(preset_path(path, sizeof path, root, "alpha") == 0);   /* enter */
    CHECK(preset_list_dirs(path, names, &count, 16) == 0);
    CHECK(count == 2);                        /* "..", inner */
    CHECK(has_name(names, count, ".."));
    CHECK(has_name(names, count, "inner"));
    /* the preset files saved in root are NOT visible inside alpha */
    CHECK(!has_name(names, count, "top"));
    /* ".." navigates back up to root */
    CHECK(preset_path(path2, sizeof path2, path, "..") == 0);
    CHECK(strcmp(path2, root) == 0);
    CHECK(preset_list_dirs(path2, names, &count, 16) == 0);
    CHECK(count == 3 && strcmp(names[0], "..") == 0);
    /* presets saved inside alpha list via preset_list from inside */
    CHECK(preset_save(path, "deep", &p, err, sizeof err) == 0);
    {
        char fnames[16][33]; int fcount = -1;
        CHECK(preset_list(path, fnames, &fcount, 16) == 0);
        CHECK(fcount == 1 && strcmp(fnames[0], "deep") == 0);
    }

    /* 4. errors: unreadable dir rejected */
    CHECK(preset_list_dirs("/tmp/ophis_no_such_dir_zz", names, &count,
                           16) == -1);
    CHECK(preset_list_dirs("/etc/hostname", names, &count, 16) == -1);

    /* cleanup */
    snprintf(path, sizeof path, "rm -rf '%s'", root);
    if (system(path) != 0) { /* best-effort cleanup */ }

    HARNESS_REPORT();
}
