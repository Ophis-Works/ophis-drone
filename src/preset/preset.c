/* src/preset/preset.c — preset file I/O over preset/schema.c
 * (IMPLEMENTATION.md §2,
 * §7). All functions take explicit dirs (testable). Name rules: 1..32
 * chars from [A-Za-z0-9 _-]; reject empty, ".", "..", "/", length > 32,
 * bad chars; stored as <name>.json. preset.c uses only C stdio + mkdir(2)
 * (EEXIST tolerated). Also the LOAD folder navigator
 * contracts: preset_list_dirs (subdirs + "..", alphabetical) and
 * preset_path (resolve dir/name) per the amended IMPLEMENTATION.md §2.*/
#include "preset.h"
#include "schema.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PRESET_NAME_MAX 32

static int set_err(char *err, int err_len, const char *msg)
{
    if (err && err_len > 0)
        snprintf(err, (size_t)err_len, "%s", msg);
    return -1;
}

static int name_ok(const char *name)
{
    if (!name || name[0] == '\0') return 0;
    size_t len = strlen(name);
    if (len > PRESET_NAME_MAX) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static int build_path(char *out, int out_len, const char *dir,
                      const char *name)
{
    int n = snprintf(out, (size_t)out_len, "%s/%s.json", dir, name);
    return (n > 0 && n < out_len) ? 0 : -1;
}

/* Default preset folder (preset.h): presets/ next to the running
 * executable, computed at runtime from /proc/self/exe. THE ONLY rule —
 * no env var, no HOME, no fallbacks. Normal case: /proc/self/exe always
 * resolves and is absolute. Guards below cover the impossible-but-UB
 * paths (readlink failure, truncation, bad args) — they return -1, they
 * never read or write garbage (PLAN_PRESET_DIR_REVIEW_FIXES.md B1..B4). */
int preset_dir(char *out, int out_len)
{
    if (!out || out_len <= 0) return -1;
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0 || n >= (ssize_t)(sizeof exe - 1)) return -1;
    exe[n] = '\0';
    const char *slash = strrchr(exe, '/');
    if (!slash) return -1;
    int m = snprintf(out, (size_t)out_len, "%.*s/presets",
                     (int)(slash - exe), exe);
    return (m > 0 && m < out_len) ? 0 : -1;
}

static int cmp_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int preset_list(const char *dir, char names[][33], int *count, int max)
{
    if (!dir || !names || !count || max <= 0) return -1;
    *count = 0;
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len < 6 || strcmp(e->d_name + len - 5, ".json") != 0)
            continue;
        if (*count >= max) break;
        memcpy(names[*count], e->d_name, len - 5);
        names[*count][len - 5] = '\0';
        /* H-1/L-3: reject over-long or dotted base names (name_ok is the
         * single name contract); skip before counting */
        if (!name_ok(names[*count]))
            continue;
        (*count)++;
    }
    closedir(d);
    if (*count > 1)
        qsort(names, (size_t)*count, 33, cmp_names);
    return 0;
}

/* mkdir -p equivalent (IMPLEMENTATION.md §7, F1a user order 2026-11-05):
 * create every missing path prefix, EEXIST tolerated. LOAD opens the
 * presets dir through this, so a fresh install finds an empty-but-
 * existing folder. Returns 0, or -1 (NULL/empty, too long, a prefix
 * exists as a non-directory). */
int preset_ensure_dir(const char *dir)
{
    if (!dir || dir[0] == '\0') return -1;
    char tmp[1024];
    if (snprintf(tmp, sizeof tmp, "%s", dir) >= (int)sizeof tmp)
        return -1;
    for (char *q = tmp + 1; *q; q++) {
        if (*q != '/') continue;
        *q = '\0';
        if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
        *q = '/';
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
    return 0;
}

int preset_save(const char *dir, const char *name, const synth_params_t *p,
                char *err, int err_len)
{
    if (!dir || !p) return set_err(err, err_len, "bad arguments");
    if (!name_ok(name)) return set_err(err, err_len, "invalid preset name");

    if (preset_ensure_dir(dir) != 0)
        return set_err(err, err_len, "cannot create preset dir");

    char path[1024];
    if (build_path(path, sizeof path, dir, name))
        return set_err(err, err_len, "path too long");

    char buf[8192];
    int n = schema_write_params(buf, sizeof buf, p);
    if (n < 0) return set_err(err, err_len, "params do not fit preset JSON");

    FILE *f = fopen(path, "w");
    if (!f) return set_err(err, err_len, "cannot open preset file");
    size_t wrote = fwrite(buf, 1, (size_t)n, f);
    if (fclose(f) != 0 || wrote != (size_t)n)
        return set_err(err, err_len, "write failed");
    return 0;
}

int preset_load(const char *dir, const char *name, synth_params_t *p,
                char *err, int err_len)
{
    if (!dir || !p) return set_err(err, err_len, "bad arguments");
    if (!name_ok(name)) return set_err(err, err_len, "invalid preset name");

    char path[1024];
    if (build_path(path, sizeof path, dir, name))
        return set_err(err, err_len, "path too long");

    FILE *f = fopen(path, "r");
    if (!f) return set_err(err, err_len, "cannot open preset file");

    static char buf[65536];
    size_t got = fread(buf, 1, sizeof buf, f);
    int read_err = ferror(f);
    fclose(f);
    if (read_err) return set_err(err, err_len, "read failed");

    synth_params_t local;   /* parse into a local: *p untouched on error */
    char jerr[128];
    if (schema_parse_params(buf, (int)got, &local, jerr, sizeof jerr) != 0) {
        if (err && err_len > 0)
            snprintf(err, (size_t)err_len, "invalid preset: %s",
                     jerr[0] ? jerr : "parse error");
        return -1;
    }
    *p = local;
    return 0;
}

/* LOAD folder navigator (PLAN §8.4): subdirectories of dir + "..",
 * alphabetically. Entry names have no trailing "/" (the UI adds the
 * suffix when drawing). Files are NOT listed here — preset_list covers
 * *.json; the navigator merges both lists. */
int preset_list_dirs(const char *dir, char names[][33], int *count, int max)
{
    if (!dir || !names || !count || max <= 0) return -1;
    *count = 0;
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *n = e->d_name;
        if (n[0] == '.' && (n[1] == '\0' ||
            (n[1] == '.' && n[2] == '\0')))
            continue;                    /* skip "." and "." only ("..",
                                          * if a dir, is added explicitly) */
        /* only real subdirectories (opendir probe: names may be up to
         * 32 chars; also keeps the handheld binary off stat@GLIBC_2.33
         * — the R36S gate requires <= GLIBC_2.28) */
        char path[1024];
        if (snprintf(path, sizeof path, "%s/%s", dir, n) >= (int)sizeof path)
            continue;
        DIR *sub = opendir(path);
        if (!sub) continue;                  /* not a readable directory */
        closedir(sub);
        size_t len = strlen(n);
        if (len >= 33) continue;
        if (*count >= max) break;
        memcpy(names[*count], n, len + 1);
        (*count)++;
    }
    closedir(d);
    /* ".." always present (go up one level, PLAN §8.4), sorted in */
    if (*count < max) {
        memcpy(names[*count], "..", 3);
        (*count)++;
    }
    if (*count > 1)
        qsort(names, (size_t)*count, 33, cmp_names);
    return 0;
}

/* Resolve <dir>/<name> for the navigator (entering a listed entry,
 * including ".."). The joined path is lexically normalized by a single
 * dir walk (X2, PLAN_CODE_REVIEW_FIXES.md Phase 8): components are
 * consumed left to right and the normalized result is written as we go —
 * "" and "." components drop, ".." unwinds the last written component
 * (clamped at "/" for absolute paths; kept as a literal component when a
 * relative path walks above its root), duplicates are kept verbatim.
 * Output is bounded by the input: every output byte is an input byte
 * (component characters and separators, subsequence of the joined
 * string), and joined < 1024, so res < 1024 needs no per-append guard.
 * No ".json" suffix — used for directories. */
int preset_path(char *out, int out_len, const char *dir, const char *name)
{
    if (!out || out_len <= 0 || !dir || !name) return -1;
    char joined[1024];
    int n = snprintf(joined, sizeof joined, "%s/%s", dir, name);
    if (n <= 0 || n >= (int)sizeof joined) return -1;

    char res[1024];
    int k = 0;
    int absolute = (joined[0] == '/');
    if (absolute) res[k++] = '/';
    const int floor = absolute ? 1 : 0;   /* pop clamps at "/" / "" */

    for (const char *p = joined; *p; ) {
        while (*p == '/') p++;                     /* separators */
        const char *comp = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - comp);
        if (len == 0 || (len == 1 && comp[0] == '.'))
            continue;                              /* "" / "." drop */
        if (len == 2 && comp[0] == '.' && comp[1] == '.') {
            if (k > floor) {                       /* pop one component ... */
                k--;
                while (k > floor && res[k - 1] != '/') k--;
                if (k > floor) k--;                /* ...and its separator */
            } else if (!absolute) {                /* relative above its
                                                    * root: ".." stays */
                res[k++] = '.';
                res[k++] = '.';
            }
            /* absolute at the floor: "/.." clamps to "/" (dropped) */
            continue;
        }
        if (k > floor) res[k++] = '/';
        memcpy(res + k, comp, (size_t)len);
        k += len;
    }
    if (k == 0) res[k++] = '.';            /* degenerate: relative path
                                            * walked above its root */
    res[k] = '\0';

    if (k >= out_len) return -1;
    memcpy(out, res, (size_t)k + 1);
    return 0;
}
