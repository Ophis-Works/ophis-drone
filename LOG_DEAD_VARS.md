# LOG_DEAD_VARS.md — PLAN_DEAD_VARS.md M1 implementation log

Owner order 2026-11-08 (R3/R4): ALL warnings fixed IN CODE — no -Wno-*,
no #pragma, no per-file downgrades; the full WARNFLAGS line in the
Makefile is untouched (byte-identical, asserted before/after: `grep -c
'^WARNFLAGS = ...' Makefile` = 1). Scope note: the plan's census (75
warnings / 10 files) covered `make all` only; the `make test` TUs
compile with the same WARNFLAGS and were ALREADY red under -Werror
before any change here (pre-existing). R1/R3 cover "src + tests", so
the test TUs are fixed in code too — same rules, no suppression.

Verification: `make clean && make all` → 0 "warning", 0 "error:" lines,
binary produced; `make test` → 0 "warning" lines, TOTAL 11203/11203
passed (run in this session).

## Dead variables (R1) — deleted

1. src/ui/ui_draw.c (was line 521) `int dim = item_dim(ui, it);` — dead
   since the dim checks moved into ui_digit_marker_active (M2/M3); the
   SEQ row loop never read it. Nothing else changed.
2. tests/test_engine_seq.c (were lines 19-20) `bbuf_on`, `bbuf_off`,
   `last_b_raw_on` — orphaned by the serial-FM removal. NB2C (only used
   by them) stays: the plan scopes the deletion to the 4 variables.

## src fixes

- src/audio/alsa_pcm.c (alsa_open): latency math rewritten with explicit
  conversions — `(unsigned long long)period_frames * 4ull * 1000000ull /
  (unsigned long long)(unsigned int)rate`, outer `(unsigned int)` kept.
  Value-preserving: period_frames/rate are the positive main.c SR/PERIOD
  constants (comment added).
- src/audio/engine.c (engine_init, engine_panic_reset, 4 sites): seed
  expression `(uint64_t)c * (uint64_t)0x1000ull` — the implicit
  unsigned long -> unsigned long long widening in the multiply was
  flagged by -Wsign-conversion; casting both operands makes every
  conversion explicit and value-preserving (c is 0..2). Verified by
  probe compile (/tmp/conv_probe.c): only this variant is clean.
- src/dsp/osc.c: real case labels, no behavior change. First switch:
  WAVE_SINE/TRIANGLE/SAW/SQUARE fall through to the tonal path (the
  switch's documented behavior). Second switch: WAVE_WHITE/PINK labeled
  with x = 0.0 — unreachable (returned above), matches the existing
  default (nearest case).
- src/dsp/mixer.c (mod_apply, 6 sites): the accumulators are float BY
  DESIGN (xmod_in_t channels); the double result is now explicitly cast
  to float at each `+=`, e.g. `acc->amp_mod += (float)((double)amount /
  100.0 * s)`. The double->float rounding is intended (field type);
  values are the same as before the cast.
- src/dsp/chain.c: `fc_cache_eq()` helper — EXACT float equality via
  memcmp (cache-invalidation must be exact; a tolerance would thrash the
  cache or skip needed updates; -0.0f/+0.0f counting as "changed" only
  forces one harmless extra coefficient pass). Applied at the 3
  -Wfloat-equal sites: res_mod_active, the else-if cache check, the
  only_fc test. res_mod is memset-0 + `+=` of (float)(...), so it never
  becomes -0.0 in practice.
- src/preset/schema_parse.c:
  - val_int: no ==/!= on doubles — NaN-safe range guard
    `!(d >= -2147483648.0 && d <= 2147483647.0)` (NaN fails both;
    ±inf fail the bounds) + relational integrality
    `floor(d) < d || floor(d) > d` (exactly `d != floor(d)` for finite
    values). Same accept/reject set as the old check, same perr path.
  - walk_keys KT_ENUM: `(int)tab[i].lo` — lo carries the enum count
    (6/4/2/5/10/8) as an integral double literal; exactly representable,
    cast value-preserving (comment added).
- src/ui/ui.c:
  - param_get/param_set: `case PK_NONE:` added to the default group —
    same behavior as before (read 0.0 / write inert).
  - edit_item: the 15 selector/integer ITF_* labels (WAVE, DUTY, PCT,
    SPCT, DIV, DEST, TGT, FILT, XSRC, ONOFF, SEQMODE, SEQONCE, STEPSEL,
    INT, XDIV) added to the default group — they always took the
    default's uniform R11 roll-over formula; explicit labels preserve
    that exactly.
  - ui_handle_action sub-tab cycle: computed in int
    `((int)ui->sub[ci] + dir + (int)SUB_COUNT) % (int)SUB_COUNT`
    (always in 0..SUB_COUNT-1, never negative), then
    `(sub_tab_t)(unsigned int)` — value-preserving; same wrap values as
    before.
  - shadow fix: NAV_STEPSEL's local `n` renamed `sn` (the outer `n` is
    the page item count). Same arithmetic.
  - nav switch: `case NAV_NONE:` labeled to the inert default (was the
    default's documented behavior; NULL-spec guard unchanged).
  - ui_event_filter: dropped the `(ui_t *)` cast — ui_text_entry_active
    already takes `const ui_t *` (ui.h:98). No const cast anymore.
- src/ui/ui_menu.c: ACT_TRIGGER and ACT_MENU labeled into the default
  close-path group of the MENU_MAIN / MENU_CONFIRM / MENU_NAME act
  switches, and ACT_TRIGGER / ACT_TAB_NEXT / ACT_TAB_BACK / ACT_MENU
  into MENU_LOAD's inner default — all were falling to default; the
  default logic (`if (act == ACT_TRIGGER && !KB_TRIGGER_CANCELS_MENU)
  break; menu_close(ui);`) is unchanged, so behavior is identical
  (TAB_BACK/NEXT remain pre-excluded in MENU_LOAD — the labels are
  unreachable, matching the nearest case).
- src/ui/ui_draw.c: every snprintf on an `int blen/n` param now casts
  `(size_t)blen` / `(size_t)n1` / `(size_t)n2` (fmt_sec, item_value,
  hint_binding) — all callers pass sizeof-buffer (> 0), so each cast is
  value-preserving (comment added at both helpers). Plus the dead `dim`
  deletion above.

## tests fixes (harness + TUs; pre-existing red, per R1 "src + tests")

- tests/harness.h: `fexactf(float,float)` / `fexact(double,double)` —
  exact equality via memcmp, documented. The pins assert exact equality
  on purpose (sample-exact envelopes, coefficient identity, round-trip
  bit equality, determinism); a tolerance would weaken the pin. float
  args promote exactly to double, so fexact stays injective.
- Exact-equality pins rewritten with fexact/fexactf (semantics
  unchanged): test_osc.c:150 determinism; test_adsr.c finished-holds-0,
  reset->0, the sample-exact trigger-vs-edge attack loop;
  test_filter.c:64 state-clear + the coefficient-identity/state block
  (inner `biquad_t b` renamed `bs`, shadow fix); test_fx.c safety-biquad
  coefficient identity + bitwise-identical double render;
  test_mixer.c ring/mix exact pins; test_range.c ADSR ladder endpoints;
  test_transport.c gate/deadline absolute pins (+ sq.next_step_time);
  test_ui_menu.c item_get/pitch_hz/param_get round-trip pins;
  test_preset.c OPHIS_DEFAULTS exact recorded values.
- Intended int->float / double->float field conversions made explicit
  (value-preserving: small ints / documented single rounding):
  test_adsr.c mkp() (a/d/r), test_engine_seq.c adsr a/d, test_json.c
  fill_params freq_hz (c <= 2), test_mailbox.c fill_params (tag <=
  TAG_MAX, c <= 2), test_preset.c fc, test_sequencer.c pitch_hz
  (i <= 7).
- test_json.c: unused `dests` array deleted (dead); snprintf size args
  made explicit — `(size_t)(16384 - n)` in build_doc, `sizeof doc -
  (size_t)k` in the doc builder (k is bounded by the 16384 buffer);
  from/to buffers widened 128 -> 256 to match err[256] (fixes
  -Wformat-truncation, no truncation ever happened at runtime).
- test_preset.c: inner `FILE *f` renamed `ff` (shadow of the outer
  FILE *f; same behavior).
- test_engine_seq.c: dead vars deleted (above) + adsr casts +
  fexactf pins.

## PLAN_DEAD_VARS.md §2b / M2 — handheld -Wmissing-prototypes fix (2026-11-08)

Change (single): src/compat/glibc_symver.c — explicit prototypes for the
five linker-called wrappers added immediately before the first
definition, signatures byte-identical to the definitions
(__wrap_pow, __wrap_log2, __wrap_exp, __wrap_pthread_create,
__wrap_pthread_join). Rationale: the wrappers are called only by
-Wl,--wrap (linker), never by C code, so they had no prototypes and
-Wmissing-prototypes + -Werror killed the TU (the ask's 5 red errors).
Nothing else changed — no Makefile edit, no flags, no other file.

Verified in this session:
- RED (given by the workflow ask): 5 missing-prototypes errors,
  glibc_symver.c:41/46/51/56/62.
- GREEN compile: `make handheld` recompiled the TU under the FULL
  WARNFLAGS (-Wmissing-prototypes active) + -Werror with ZERO warnings;
  link succeeded (build/r36s/ophis-drone, aarch64 ELF, 219152 bytes).
- GATE FAIL (new, out of scope): `tools/handheld_gate.sh
  build/r36s/ophis-drone` exits 1 — max required version GLIBC_2.38
  exceeds the GLIBC_2.28 ceiling. Cause (readelf -sW):
  __isoc23_strtol@GLIBC_2.38 and __isoc23_strtoumax@GLIBC_2.38 are
  imported UND — the host glibc >= 2.39 headers redirect
  strtol/strtoumax to the C23 __isoc23_* variants under strict
  -std=c23, and those symbols exist only since glibc 2.38 (R36S has
  2.28). Call sites: src/app/autoquit.c:16 (strtol, base 10) and
  third_party/json.h:2622/2851 (strtoumax from <inttypes.h>).
  This is unrelated to the prototypes fix — the same binary would have
  failed the gate even with the old file (the TU never compiled
  before, so the gate was unreachable).
- Run-2 artefact, NOT a pass: a second `make handheld` exited 0 with
  "HANDHELD OK" but never ran the gate (binary mtime newer than its
  objects after run 1's failed gate — no .DELETE_ON_ERROR in the
  Makefile). The direct gate run above (exit 1) is the authoritative
  result; the current zip contains a binary the gate rejects.

Blocked: fixing the gate requires more than the five prototypes
(e.g. .symver pins/wrappers for strtol/strtoumax in glibc_symver.c
after the owner rules on C23-vs-2.17 semantics, or a Makefile/flag
change, or replacing strtol in autoquit.c) — all outside this ask's
scope ("change NOTHING else"). Escalated for the owner's decision;
not committed.

### M2 follow-up — strtol/strtoumax GLIBC_2.38 pins (owner ruling, option (a))

Ruling: extend src/compat/glibc_symver.c only, no Makefile/flag change;
pin strtol/strtoumax to their GLIBC_2.17 aliases following the file's
existing symver pattern (semantics match: autoquit.c:16 uses base 10;
json.h integer parsing; C23 differences concern base 0/2 "0b" literals
only).

Change: added <stdint.h>; two .symver pins in the existing asm block
(compat_strtol_ref -> strtol@GLIBC_2.17, compat_strtoumax_ref ->
strtoumax@GLIBC_2.17); two extern _ref decls in the existing extern
block. Interception point differs from the __wrap_* five BY NECESSITY:
the Makefile's --wrap list is fixed, and the references in the other
objects are to the C23-redirected names, so the shims are DEFINED here
as `__isoc23_strtol` / `__isoc23_strtoumax` (prototypes first for
-Wmissing-prototypes); the linker then binds autoquit.o / the json TU
to these definitions instead of libc's GLIBC_2.38 versions. Same
symver mechanism, definition-based instead of wrap-based.

Verified (this session, `make clean && make handheld`):
- EXIT 0; zero "warning" lines in the full log (full WARNFLAGS active,
  -Werror).
- Gate ran inside the link rule on the fresh binary:
  "GATE PASS: build/r36s/ophis-drone (aarch64, <= GLIBC_2.28)",
  max required version = GLIBC_2.17.
- readelf -sW: 0 symbols bound to any glibc > 2.19; 0 UND isoc23
  references (the 4 remaining isoc23 entries are these definitions
  themselves, GLOBAL DEFAULT).
- Zip rebuilt: build/deploy/ophis-drone-r36s.zip (fresh mtime, after
  the passing gate), "HANDHELD OK".
- Scope: git diff --stat = src/compat/glibc_symver.c + LOG_DEAD_VARS.md
  only. Desktop build structurally unaffected (glibc_symver.c is in
  HANDHELD_SRC only, not in the `all` target's sources) — desktop
  suite NOT re-run in this ask (not ordered by the ruling).
- RED for this increment was the direct gate run exit 1 recorded above
  (GLIBC_2.38 required); GREEN is the passing clean rebuild. Nothing
  committed (no order to commit).

### Commit fbc589e — committer (2026-11-08)

Committed src/compat/glibc_symver.c and LOG_DEAD_VARS.md as
fbc589e "Declare the handheld linker-wrap shims; the R36S build
passes its gate under -Werror" (unsigned, single line, 2 files,
+117/-1). PLAN_*.md and .zcode/ left untracked/unstaged. Committer's
own verification in the commit session: `tools/handheld_gate.sh
build/r36s/ophis-drone` exit 0, max required version GLIBC_2.17;
`touch src/compat/glibc_symver.c && make handheld` exit 0 with 0
"warning" lines — the TU recompiled under the full flag line
(-std=c23 -Wall -Wextra ... -Werror -Wmissing-prototypes ...), gate
ran inside the link rule on the fresh binary (GATE PASS, aarch64,
<= GLIBC_2.28), zip rebuilt ("HANDHELD OK").

## PLAN_DEAD_VARS.md §2c / M3 — .DELETE_ON_ERROR: false-pass fix (2026-11-08)

Change (single): Makefile — one added line `.DELETE_ON_ERROR:` directly
below `.PHONY: all handheld test clean` (now Makefile:31). Rationale:
the handheld link rule runs tools/handheld_gate.sh LAST (Makefile:73
pre-fix numbering), so a gate failure left the just-linked binary in
place, newer than its objects; a second `make handheld` then exited 0
without re-running the gate and shipped the rejected binary in the zip
(FALSE PASS, reproduced at LOG_DEAD_VARS.md:159-163). GNU make's
.DELETE_ON_ERROR deletes a target whose recipe fails, so the stale
binary can no longer survive a failed gate. Global special target, no
recipe, no flags — R6 met, R7 met (git diff: Makefile +1 line only;
WARNFLAGS/CFLAGS/HCFLAGS untouched, R3/R5 stand). NOTHING else changed.

Verified (this session):
- RED (given by the workflow ask): scratch worktree /tmp/ophis-m3red at
  fbc589e — injected gate `exit 1`, `rm -rf build && make handheld`
  failed (make exit 2) but the binary SURVIVED (219216 bytes, mtime
  22:09:05); after restoring the gate the second make exited 0 with
  "HANDHELD OK", mtime UNCHANGED, 0 GATE lines — gate never re-ran.
- GREEN, real repo: `make handheld` first reported "Nada a fazer"
  (target up to date at HEAD, recipe did not run — recorded as such);
  then `make clean && make handheld` -> EXIT=0, "HANDHELD OK",
  build/deploy/ophis-drone-r36s.zip rebuilt; full WARNFLAGS active with
  -Werror, so EXIT=0 implies zero warnings (any warning would have
  aborted the build). Direct gate run `tools/handheld_gate.sh
  build/r36s/ophis-drone` -> "GATE PASS: ... (aarch64, <= GLIBC_2.28)",
  max required version GLIBC_2.17, exit 0.
- GREEN, plan 2c.3 pin, scratch worktree /tmp/ophis-m3red (same one
  line added there via Edit): injected gate `exit 1` + `rm -rf build &&
  make handheld` -> make exit 2 AND make itself deleted the target
  ("a eliminar ficheiro build/r36s/ophis-drone"); stat confirms the
  binary is GONE. Gate restored (`git checkout --
  tools/handheld_gate.sh`) -> second make relinked and RE-RAN the gate
  (3 GATE lines, GATE PASS, HANDHELD OK, EXIT=0). The false pass is
  structurally impossible now: a failed gate leaves no binary for the
  second make to consider up to date.
- Desktop suite: `make test` -> TOTAL: 11203/11203 passed, exit 0.

Scratch worktree /tmp/ophis-m3red left in place (untracked evidence;
scratch only). Nothing committed (no order to commit).
