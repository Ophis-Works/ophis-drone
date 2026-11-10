/* src/preset/preset.h — preset file I/O over json.c (all preset functions
 * take explicit paths/dirs, testable). */
#ifndef OPHIS_PRESET_H
#define OPHIS_PRESET_H

#include "../dsp/types.h"

/* Default dir: "presets/" next to the running executable, computed at
 * runtime from /proc/self/exe. THE ONLY rule — no env var, no HOME, no
 * fallbacks. Returns 0, or -1 (out NULL/too small, /proc/self/exe
 * unreadable or >= 1023 bytes). */
int preset_dir(char *out, int out_len);

/* List *.json files in dir, alphabetically. Fills names[][33] (names
 * WITHOUT the .json extension, NUL-terminated), *count = found.
 * Returns 0, or -1 on error (dir unreadable). */
int preset_list(const char *dir, char names[][33], int *count, int max);

/* mkdir -p equivalent: create every missing path
 * prefix of dir, EEXIST tolerated. Returns 0, or -1 (NULL/empty dir,
 * path >= 1024 bytes, prefix not creatable — e.g. a file in the way). */
int preset_ensure_dir(const char *dir);

/* Save p as <dir>/<name>.json. Sanitizes name first: 1..32 chars from
 * [A-Za-z0-9 _-]; rejects empty, ".", "..", anything containing "/",
 * length > 32, bad chars. Creates dir if missing (preset_ensure_dir).
 * Returns 0, or -1 with err filled (err may be NULL). */
int preset_save(const char *dir, const char *name, const synth_params_t *p,
                char *err, int err_len);

/* Load <dir>/<name>.json into *p. Name sanitized per preset_save.
 * On error returns -1, *p untouched, err filled (err may be NULL). */
int preset_load(const char *dir, const char *name, synth_params_t *p,
                char *err, int err_len);

/* LOAD folder navigator: list the subdirectories of dir plus
 * ".." (to go up), alphabetically. Fills names[][33] (NUL-terminated
 * entry names, WITHOUT a trailing "/"), *count = found.
 * Returns 0, or -1 on error (dir unreadable). */
int preset_list_dirs(const char *dir, char names[][33], int *count, int max);

/* Resolve <dir>/<name> into out (the LOAD navigator uses this to enter a
 * listed subdirectory, including name ".."; the joined path is lexically
 * normalized so entering ".." yields the parent path). Returns 0, or -1
 * if out_len is too small. */
int preset_path(char *out, int out_len, const char *dir, const char *name);

#endif /* OPHIS_PRESET_H */
