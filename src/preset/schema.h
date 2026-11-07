/* src/preset/schema.h — preset format schema layer (PLAN §9 version 1)
 * over the vendored sheredom/json.h parser/writer (third_party/json.h,
 * Unlicense). Pure: no file IO. Replaces the Phase 1-6 hand-rolled
 * preset/json.{c,h} (PLAN_CODE_REVIEW_FIXES.md Phase 7): the parser walks
 * the json.h DOM and enforces the contract explicitly — strict
 * app/version, required keys, exact array lengths (3 chains / 2 subs /
 * 8 steps / 3 xmod), enum strings, integer percents 0..100, bpm 1..500,
 * duty 5..95, delay/reverb/safety objects REQUIRED, unknown keys
 * leniently skipped, over-long keys (> 15 chars) rejected, duplicate
 * keys REJECTED at every object level. */
#ifndef OPHIS_SCHEMA_H
#define OPHIS_SCHEMA_H

#include "../dsp/types.h"

/* Writer: synth_params_t -> buf (PLAN §9 format, version 1; `run`
 * excluded). Serializes through json.h's minified writer into the caller's
 * fixed buffer. Returns bytes written (excluding NUL), or -1 if buf_len is
 * too small (8 KiB must be provably enough). */
int schema_write_params(char *buf, int buf_len, const synth_params_t *p);

/* Parser: PLAN §9 version 1 (see header comment for the enforced
 * contract). On success copies into *p; on error (returns -1) *p is
 * untouched (IMPLEMENTATION.md §7). err (may be NULL) gets a field-path
 * message, e.g. "seq.steps[2].vel", when err_len>0. */
int schema_parse_params(const char *text, int text_len, synth_params_t *p,
                        char *err, int err_len);

/* ---- enum name accessors (Phase 5 item 4, moved here from preset/json.c
 * by Phase 7): the PLAN §9 enum tables in schema.c are the SINGLE SOURCE
 * of the enum names — the UI's per-enum name arrays read them through
 * these instead of keeping copies. Display form: UPPERCASE with
 * '_' -> '.' (the UI's historical casing, e.g. "b_sub1" -> "B.SUB1"),
 * handled ONCE here at the accessor; the tables themselves stay verbatim
 * PLAN §9 strings. One static buffer per accessor: the UI is
 * single-threaded and uses each name immediately. ---- */
const char *schema_wave_name(int v);   /* wave_t       -> "SINE" .. "PINK"  */
const char *schema_dest_name(int v);   /* mod_dest_t   -> "OFF" .. "FLT_RES" (RAW "flt_cut"/"flt_res" strings; PLAN_OSC_NAMING.md R0e) */
const char *schema_xsrc_name(int v);   /* xmod src     -> RAW preset strings ("off".."a_sub2"; S1: the display form lives in the UI) */
const char *schema_filt_name(int v);   /* filt_mode_t  -> "LP" .. "BR"      */
const char *schema_poles_name(int v);  /* poles        -> "12DB" / "24DB"   */

#endif /* OPHIS_SCHEMA_H */
