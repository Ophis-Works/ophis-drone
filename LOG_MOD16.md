# LOG_MOD16.md — PLAN_MOD_MATRIX_16.md milestone log (subagent-written)

## M1 — P1 red pin (2026-11-09)

### PIN (red)
- Scope: tests/test_mixer.c ONLY (Edit tool).
- Pin: P1, PLAN_MOD_MATRIX_16.md §1.3 (lines 64-68) = PLAN_HACK_REVIEW_FIXES
  F3 red pin: all 3 xmod slots target chain A (tgt 0), dest MOD_PITCH,
  amount 0.1f, source 1.0 (m.src[0], id 1 = B.osc), driven through the real
  matrix_render; assert fexact((double)m.xm[0].detune_hz, 3.0*(double)0.1f).
- Patterns reused from tests/test_mixer.c: settable src values (m.src[0],
  line 54 pattern), mksp()/mkp() helpers (lines 16-27), direct matrix_render
  call (line 176 pattern), harness fexact (tests/harness.h:23).
- Rationale: float xmod_in_t.detune_hz (src/dsp/chain.h:14) accumulates
  3×0.1f in float -> 0.30000001192092895508; the double reference
  3.0*(double)0.1f = 0.30000000447034835815. Not bit-equal -> pin RED now,
  GREEN after M1 replaces the fields with double accumulators.

### Commands + output
- Baseline (pre-edit): `make build/tests/test_mixer && ./build/tests/test_mixer`
  -> "35/35 passed", exit 0.
- Post-edit: `make build/tests/test_mixer` -> compiled clean, zero warnings
  under the full -Werror flag set (no flag changes).
  `./build/tests/test_mixer` ->
      FAIL tests/test_mixer.c:251: fexact((double)m.xm[0].detune_hz, 3.0 * (double)0.1f)
      35/36 passed
      exit=1
- Probe (throwaway /tmp C program, removed after): float acc
  0.30000001192092895508 vs double ref 0.30000000447034835815, memcmp
  bit-equal = no. Matches the plan's probe note (line 67-68).

### State
- RED proven. Not committed (per workflow instruction). No source files
  touched; only tests/test_mixer.c modified + this log created.

## M1 — IMPLEMENT (F2+F3) + P1 GREEN (2026-11-09)

### Edits (Edit/Write tool only, per ask scope)
- NEW src/dsp/exact_eq.h: static inline fexactf (float) + fexact (double),
  memcmp-representation equality, -Wfloat-equal convention documented,
  __attribute__((unused)) for header use (F2). Replaces fc_cache_eq
  (chain.c) and the two harness.h helpers (both deleted).
- src/dsp/chain.c: fc_cache_eq deleted, include "exact_eq.h"; fc-cache
  compares -> fexactf((float)fc, c->last_fc) (chain.c:128, 139 — float
  operands, same bits as before); res_mod_active -> !fexact(xm->res_mod,
  0.0) (chain.c:110) after the field became double; redundant (double)
  field casts dropped at chain.c:44-46, 52, 112 (detune_hz, amp_mod,
  filter_mod, phase_mod, res_mod). (float)fc casts on last_fc kept
  (last_fc stays float — its cache contract is unchanged).
- src/dsp/chain.h: xmod_in_t fields float -> double (detune_hz, amp_mod,
  filter_mod, phase_mod, res_mod) (F3: single rounding per term).
- src/dsp/mixer.c: the five (float) casts in mod_apply dropped
  (MOD_PITCH/AMP/PHASE/FILTER/FRES/FM sites; double += double, no
  -Wconversion warning).
- tests/harness.h: two helpers deleted, includes "dsp/exact_eq.h"
  (-Isrc on every test compile line; test call sites keep the names —
  zero test churn).

### Consumer audit before the type change
- All xmod_in_t field consumers grepped: chain.c, mixer.c (edited),
  engine.c:158 (pointer pass-through only), tests/test_chain.c:68/201
  (float literals widen exactly to double fields — compile-clean, file
  outside scope, untouched), tests/test_mixer.c (fabs tolerance pins +
  the P1 fexact pin).

### Commands + output
- `make build/tests/test_mixer && ./build/tests/test_mixer` -> compiled
  clean, zero warnings under the full -Werror flag set (visible in the
  recipe line; no flags changed): "36/36 passed", exit 0. P1 GREEN.
- `make clean && make all && make test` -> desktop app + all 22 test
  suites built clean under -std=c23 -O2 full WARNFLAGS -Werror; suites
  all green: TOTAL: 11204/11204 passed, exit 0. test_engine: xruns: 0
  (smoke, part of make test).
- P2 greps (PLAN_HACK_REVIEW_FIXES §2.2 step 3): fc_cache_eq zero hits
  repo-wide (grep rc=1); fexact/fexactf defined exactly once
  (src/dsp/exact_eq.h:16/20); exact_eq.h included by chain.c +
  harness.h.

### Pin movement
- None needed. P1 went green bit-exactly: 3 × 0.1f-as-double accumulates
  exactly (0.1f_d = 0xCCCCCD×2^-27; sums are exact in 53-bit double), equal
  to the reference 3.0×(double)0.1f. No existing pin moved in any ulp —
  every other xm-field pin is a fabs tolerance (1e-6/1e-9) on values that
  are exact either way. No last-ulp pin update performed; nothing
  escalated.

### State
- Suite + full rebuild green. Not committed (per workflow instruction).
- Not run in this ask (scheduled as later M1 workflow steps per
  PLAN_MOD_MATRIX_16.md §2.1): ASan on test_mixer/test_chain/
  test_engine_seq/test_ui_menu; standalone smoke target; make handheld
  gate. chain.c/mixer.c/chain.h are in the handheld build (HANDHELD_SRC
  includes DSP_SRC) — the handheld gate compiles them under the same
  -Werror flag set when it runs.

## M1 — AUDIT FIX: out-of-scope working-tree changes reverted (2026-11-09)

### Audit findings (both verified against `git diff HEAD --numstat`)
- LOG_DEAD_VARS.md: +12/0 — appended note "### Commit 78734c6 —
  committer (2026-11-08)" documenting an already-landed commit.
  Outside the M1 six-file list (PLAN_MOD_MATRIX_16.md:86-89).
- tests/test_seq_items.c: 6/6 — `(int)sizeof nb` -> `sizeof nb` at six
  ui_xsrc_display call sites (tests/test_seq_items.c:499-506); F1
  display-buffer follow-through from another plan, outside M1 scope.

### Fix
- Both files reverted to HEAD: `git restore LOG_DEAD_VARS.md
  tests/test_seq_items.c` (restores committed bytes exactly; no Edit,
  no content authored). Neither change belonged to this milestone;
  per scope rule the M1 tree carries only the six F2+F3 files.
- Work-loss preservation — the reverted LOG_DEAD_VARS.md note text,
  verbatim, for its owning plan to re-apply if wanted:
    ### Commit 78734c6 — committer (2026-11-08)
    Committed Makefile and LOG_DEAD_VARS.md as 78734c6 "Delete targets on
    recipe failure; a failed handheld gate cannot leave a stale binary"
    (unsigned, single line, 2 files, +57/-0). Verified in the commit
    session: staged status listed exactly Makefile + LOG_DEAD_VARS.md
    (`git add Makefile LOG_DEAD_VARS.md` then `git status --porcelain`);
    `git log -1 --stat` shows 2 files changed, 57 insertions(+), %G? = N
    (no signature). PLAN_*.md (19 files) and .zcode/ left untracked
    unstaged. No build or gate check re-run in this commit session — the
    §2c verification above stands as recorded from the fix session.
- The reverted tests/test_seq_items.c change (trivial re-apply for its
  owning plan): drop the `(int)` cast on the cap argument at the six
  ui_xsrc_display call sites in the block at tests/test_seq_items.c:496-510
  (`ui_xsrc_display(N, nb, sizeof nb)` — cap param is size_t at
  src/ui/seq_items.h:288).

### Tree scope after fix
- `git diff HEAD --numstat` -> exactly the six M1 files:
  9/19 src/dsp/chain.c, 5/3 src/dsp/chain.h, 6/6 src/dsp/mixer.c,
  3/15 tests/harness.h, 23/0 tests/test_mixer.c; new file
  src/dsp/exact_eq.h untracked. LOG_DEAD_VARS.md and
  tests/test_seq_items.c: no diff.

### Gate re-run (post-fix, this ask)
- Command: `make clean && make all && make test` -> exit 0.
- Flags: no Makefile/flag change; every compile line ran under the
  baked-in set (Makefile:2-3): `-std=c23 -O2 -Wall -Wextra -Wpedantic
  -Werror -Wconversion -Wsign-conversion -Wfloat-equal -Wcast-qual
  -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wshadow
  -Wundef -Wpointer-arith -Wwrite-strings -Wredundant-decls
  -Wold-style-definition -Wmissing-field-initializers -Wswitch-enum
  -Wformat=2`. Zero warnings; -Werror would have failed the build
  otherwise. Desktop app built; all 22 test suites built + ran:
  TOTAL: 11204/11204 passed. test_engine: xruns: 0 (smoke, part of
  make test). Restored tests/test_seq_items.c: 567/567 (HEAD content
  compiles clean under the same set).
- Not run in this ask (separate scheduled M1 workflow steps per
  PLAN_MOD_MATRIX_16.md §2.1; the ask named only the suite gate):
  ASan on test_mixer/test_chain/test_engine_seq/test_ui_menu;
  standalone smoke target; make handheld gate.

## M1 — COMMIT (2026-11-09)

### Commit
- Message: "Accumulate matrix modulation in double; one shared
  exact-equality helper" (single line, imperative, unsigned).
- Staged exactly (7 files): src/dsp/exact_eq.h (new),
  src/dsp/chain.c, src/dsp/chain.h, src/dsp/mixer.c,
  tests/harness.h, tests/test_mixer.c, LOG_MOD16.md.
- Left unstaged/untracked: all PLAN_*.md, .zcode/, everything else.

## M2 — P2+P3 red pins (2026-11-09)

### PIN (red)
- Scope: tests/test_json.c (+77) and tests/test_mixer.c (+37) ONLY
  (Edit tool); this log. Tree verified: `git diff HEAD --numstat` ->
  exactly those two files; nothing else modified, nothing staged.
- P2 (PLAN_MOD_MATRIX_16.md §1.3 lines 69-72), test_json.c section 11,
  four assertion groups on a v2 doc built by the new build_doc_v2()
  helper (build_doc(3,2,8,n) + version 1 -> 2 replace, so every other
  key stays canonical):
  - 5 entries PARSE + positional mapping: entry 0 made ACTIVE
    (b_osc/pitch/50.0 — the pitch amount boundary ±50 already proven
    inclusive); assert slot 0 src==1/dest==MOD_PITCH, amount fexactf 50,
    slot 4 src==0 (5th entry), slots 5..15 dest==MOD_OFF (loop), and
    (int)(sizeof xmod/sizeof xmod[0]) == 16 (the slots must EXIST).
  - 17 entries REJECT with err naming the ARRAY (`strstr(err,"xmod")`) —
    proves the reject is the LENGTH gate, not the version gate.
  - version 1 REJECTS with err naming "version" (canonical build_doc
    v1 doc, 3 entries).
  - empty xmod array PARSES; all 16 slots dest==MOD_OFF (loop).
- P3 (PLAN_MOD_MATRIX_16.md §1.3 lines 73-75), test_mixer.c block after
  P1: nslots == 16 (sizeof-derived); slot 8 (the 9th slot) written via
  the runtime-bound h_xmod helper — src 1 (B.osc), dest MOD_FRES,
  amount 100.0f, tgt 1 (chain B); matrix_render; assert
  m.xm[1].res_mod == 0.5 (1e-6), m.xm[0]/m.xm[2].res_mod < 1e-9
  (slots with no params never fire).
- Compile-red avoidance (test_json.c §10 precedent): xmod[5..15] and
  xmod[8] are out of bounds against today's xmod[3] (types.h:119), so
  all new slot access goes through a noinline runtime-bound accessor
  h_xmod() in each file — gcc cannot prove the subscript OOB, the
  pins stay RUNTIME reds. The loops are gated/bounded by the sizeof-
  derived slot count, so today they partly run over adjacent-stack
  garbage (0xEE-poisoned struct + neighbouring stack bytes): 8 of the
  16 empty-array dest checks and 8 of the 11 "5..15 OFF" checks read
  zero garbage and pass vacuously today — meaningless until M2 widens
  xmod[16]; the red state rests on the OTHER checks (listed below),
  and every read is in-bounds and semantic after M2.

### Commands + output
- Baseline (pre-edit, both binaries up to date from M1):
  `./build/tests/test_json` -> 560/560 passed, exit 0;
  `./build/tests/test_mixer` -> 36/36 passed, exit 0.
- Build: `make build/tests/test_json build/tests/test_mixer` -> rc=0,
  both recompiled under the baked-in flag set (Makefile:2-3, full
  -Werror set, no flag changes), zero warnings.
- Run: `./build/tests/test_json` ->
      FAIL tests/test_json.c:1264: schema_parse_params(...,&v2,...) == 0        (v2 + 5 entries REJECTED — version gate)
      FAIL tests/test_json.c:1266: h_xmod(&v2, 0)->src == 1 && ... MOD_PITCH    (positional mapping unreachable)
      FAIL tests/test_json.c:1268: fexactf(h_xmod(&v2, 0)->amount, 50.0f)
      FAIL tests/test_json.c:1269: h_xmod(&v2, 4)->src == 0
      FAIL tests/test_json.c:1271: h_xmod(&v2, s)->dest == MOD_OFF   (x3 — slots 5..7 real, != OFF)
      FAIL tests/test_json.c:1272: (int)(sizeof v2.xmod / sizeof v2.xmod[0]) == 16   (3 != 16)
      FAIL tests/test_json.c:1280: strstr(err, "xmod") != NULL       (17-entry reject fires the VERSION gate: err=="version")
      FAIL tests/test_json.c:1284: schema_parse_params(...) == -1    (version 1 doc PARSES — red per ask)
      FAIL tests/test_json.c:1292: schema_parse_params(...) == 0     (v2 + empty array REJECTED)
      FAIL tests/test_json.c:1295: h_xmod(&v2, s)->dest == MOD_OFF   (x8 — rest vacuous OOB zeros)
      583/602 passed
      exit=1
- Run: `./build/tests/test_mixer` ->
      FAIL tests/test_mixer.c:276: nslots == 16                      (sizeof says 3 — no 9th slot)
      FAIL tests/test_mixer.c:286: fabs(m.xm[1].res_mod - 0.5) < 1e-6   (slot 8 write skipped; B untouched)
      38/40 passed
      exit=1
- Green-today CHECKs, by design: test_json.c:1279 (17 entries == -1 —
  rejects today via the version gate; the err-content pin at :1280 is
  the red part) and :1283 (v1 build_doc succeeded — its partner :1284
  is red); test_mixer.c:287/288
  (A/C res_mod < 1e-9 — true today because NO slot fires; meaningful
  only after M2, when 15 idle slots exist beside slot 8).
- No source files touched; P2/P3 red does not require it (parser
  version gate schema_parse.c:479, length gate schema_parse.c:494;
  xmod[3] types.h:119, mixer slot loop mixer.c:81).

### State
- P2 + P3 RED proven. Not committed (per workflow instruction).
- Not run in this ask (scheduled as later M2 workflow steps per
  PLAN_MOD_MATRIX_16.md §2.1): the full suite gate, schema v2
  implementation (types.h MOD_SLOTS, schema_parse v2, schema_write
  active-only, defaults 16xOFF, migrate + presets), ASan, migrate
  verify.

## M2 — IMPLEMENT + VERIFY (2026-11-09)

### Red (fixture rewrites, before any source edit)
- v1 fixtures -> v2 (Edit only): test_json.c — build_doc now emits
  `"version":2`; build_doc_v2 is the plain 3/2/8 alias; wrong-version
  test flipped to `2 -> 1` reject; the old 2-entry WRONG-length case
  became a 2-entry PARSE (slots 0..2 checked OFF via h_xmod) + 17-entry
  reject; section 11's v1 doc now built by `2 -> 1` replace; both inline
  wave-mapping docs bumped to version 2; test_preset.c fill_params now
  sets TWO ACTIVE slots at 0..1 (src 2/dest 3/66.0f; src 5/dest 7/
  -20.0f) — prefix-canonical, because the v1 fixture (slot 1 active,
  0/2 off) is NOT representable under the active-only writer.
- Run (current binaries, pre-implementation):
  `make build/tests/test_json build/tests/test_preset build/tests/test_mixer`
  -> rc=0, zero warnings;
  `./build/tests/test_json` -> 469/606 passed (137 FAILs — every
  build_doc v2 doc rejected by the v1 parser) = the schema RED;
  `./build/tests/test_mixer` -> 38/40 (P3 pins, unchanged);
  `./build/tests/test_preset` -> 179/179 — green-before by
  construction: the prefix fixture round-trips through the v1
  writer/parser too, so it cannot go red pre-implementation (stated,
  not hidden).

### IMPLEMENT (Edit tool only)
- src/dsp/types.h: `#define MOD_SLOTS 16` (after xmod_params_t, R1);
  `synth_params_t.xmod[MOD_SLOTS]`.
- src/preset/schema_parse.c: header comment -> schema version 2
  contract; version gate `ver != 2` REJECTS (v1 gone, R2);
  walk_array(exact) -> walk_array(lo, hi) naming the ARRAY path on a
  length miss; call sites 3,3 / 3,3 / 2,2 / 8,8 unchanged, xmod is
  `0, MOD_SLOTS` (entry i -> slot i positional, xmod_elem warg idx;
  absent slots stay at the memset-zero OFF of the parse-into local).
- src/preset/schema_write.c: version `wnumd(w, 2)`; pools re-sized for
  the 16-active-entry worst case (hand-counted: vals 290, strings 290,
  numbers 165, object elements 240, objects 60, array elements 55,
  arrays 9) -> W_STR_N 304, W_NUM_N 176, W_EL_N 256, W_OBJ_N 72,
  W_AEL_N 64 (W_VAL_N 448, W_ARR_N 12 kept); xmod loop emits ACTIVE
  slots only, ascending, same 5 keys — active = NOT memcmp-equal to the
  static all-zero OFF (`xmod_is_off`; memcmp not `==` so a -0.0 amount
  slot stays distinct and round-trips) (R3).
- src/preset/defaults.c: `XMOD_OFF` designated-init constant, 16 entries
  in OPHIS_DEFAULTS (R5).
- src/dsp/mixer.c: matrix slot loop `slot < 3` -> `slot < MOD_SLOTS`
  (mixer.c:81) — the DSP widen of plan M2 ("schema + DSP widen", gate
  P3 GREEN; R1 "every loop bound to 3 moves to MOD_SLOTS"). mixer.h
  `xm[3]` is per-TARGET-chain, correctly 3, untouched.
- src/preset/schema.h: header + writer/parser comments to the v2
  contract.
- tools/migrate_presets.c: header documents the version-2 gate (the
  parser's v2-only gate rejects v1 input loudly — the tool is not a v1
  converter); plus `(size_t)w` on the fwrite — the tool had never been
  compiled under the full WARNFLAGS set and `-Wsign-conversion` (part
  of the baked-in Makefile:2-3 set used for the verify build) rejected
  it; in-scope file, mechanical cast, flagged here.
- presets/*.json (Edit, 6 files): version 2; active slots preserved
  exactly — bass-synth + drone-220: `"xmod":[]` (all off);
  drone-ambient / drone-evolving / noise-drone / rhythmic-mech: entries
  0..1 kept verbatim, trailing all-off entry 2 dropped (R6).

### Commands + output (verify)
- `make clean && make all` -> rc=0, zero warnings/errors under the
  baked-in `-std=c23 -O2` + full WARNFLAGS (Makefile:2-3, -Werror, no
  flags changed/disabled).
- `make test` -> `TOTAL: 11254/11254 passed`, rc=0. (First run after the
  fixture rewrite had 4 FAILs at test_json.c:475/483/489/495
  params_equal — my 2-entry SUCCESS parse committed q while `keep`
  holds the earlier 3-entry parse with slot-0 src c_sub2; fixed by the
  file's own convention `q = keep;` restore, then the full clean gate
  re-run above.)
- P2/P3 GREEN, run directly: `./build/tests/test_json` -> 606/606
  passed; `./build/tests/test_mixer` -> 40/40 passed (P3: nslots 16;
  slot 8 FLT_RES 100 -> m.xm[1].res_mod 0.5; A/C < 1e-9 with 15 idle
  slots live).
- migrate verify: tool compiled under the full WARNFLAGS (-Werror, rc
  0); `./build/migrate_presets presets/*.json` -> all six
  "migrated (N bytes, params verified)" = parse -> write -> re-parse ->
  memcmp identical, rc=0; SECOND run identical output and
  `md5sum presets/*.json` before/after runs byte-identical
  (BYTES-STABLE-ACROSS-RUNS) — the hand Edits already produce canonical
  writer bytes; sizes 1865/1976/1930/1912/1907/2015.
- v2 gate end-to-end: `sed 's/"version":2/"version":1/'` of drone-220
  into /tmp, tool -> `FAIL ...: does not parse: version`, rc=1 (v1
  REJECTED, R2).
- Active-slot preservation: python json dump of all six post-migration
  files shows exactly the pre-migration active slots (b_osc/ring/35/t2
  + c_osc/pitch/8/t0 etc.; two presets []), version 2 (R6).
- `git diff HEAD --numstat` -> exactly: this log, presets/*.json (6),
  src/dsp/{mixer.c,types.h}, src/preset/{defaults.c,schema.h,
  schema_parse.c,schema_write.c}, tests/{test_json.c,test_mixer.c
  (pins),test_preset.c}, tools/migrate_presets.c. Nothing else, nothing
  staged.

### Not run / not in this ask
- ASan gates, smoke XRUNS 0, handheld build+zip: scheduled per plan
  §2.1 for M1-already-done/M4 — not M2 steps, not run here.
- M3 UI (SLOT row) untouched: ui.c/seq_items.c still address slots
  0..2 (compiles, suite green) — their widen is M3 (P4/P5).

### State
- M2 gates GREEN: P2/P3 pins, full suite, presets migrate memcmp.
- Not committed (per workflow instruction).

### Scope
- Content = the M1 IMPLEMENT (F2+F3) edits documented above; tree
  verified pre-commit via `git diff HEAD --numstat` (the six M1 files
  + this log) and `git status --porcelain` (only .zcode/ + PLAN_*.md
  + the seven staged paths).

## M2 — AUDIT FIX (2026-11-09)

### Findings (both verified this ask)
- F1 (medium): src/dsp/mixer.c modified vs HEAD but absent from the
  M2 ask's ONLY-list. Verified: `git diff HEAD -- src/dsp/mixer.c` is
  exactly ONE hunk — `for (int slot = 0; slot < 3; slot++)` ->
  `slot < MOD_SLOTS` at mixer.c:81 (matrix_render). The hunk is
  plan-mandated: PLAN_MOD_MATRIX_16.md R1 (lines 11-14) "every
  loop/table bound to 3 moves to MOD_SLOTS"; M2 is titled "schema +
  DSP widen" (line 90); P3 (M2's red pin, lines 73-75) requires slot
  9 to fire through matrix_render. Reverting it leaves P3 red and
  slots 3..15 unrendered — M2 cannot pass its own scheduled gates
  without it. The ask's list omission vs plan requirement is a
  contradiction outside this agent's reach (the ask's list is
  workflow-owned) -> ESCALATED for owner disposition. NOT reverted;
  NOT worked around.
- F2 (low): stray untracked binary tools/migrate_presets (gitignore
  covers only build*; Makefile has NO rule for this tool — previous
  verify compiled it by direct gcc, LOG_MOD16.md M2 verify section).
  Fix: used for the named re-run below, then `rm tools/migrate_presets`.
  `git status --porcelain` afterwards: no migrate/tools untracked
  entry; tools/ holds only tracked sources. Rebuild command (recorded
  for reproducibility; no Makefile change made — the tool is a
  source-only dev tool):
      gcc -std=c23 -O2 $(WARNFLAGS full set, Makefile:2-3) \
        -D_POSIX_C_SOURCE=200809L -isystem third_party -Isrc \
        tools/migrate_presets.c src/preset/schema_parse.c \
        src/preset/schema_write.c src/preset/schema_tab.c \
        -o tools/migrate_presets -lm
  (first link attempt without -isystem third_party / -lm failed:
  json.h not found; then undefined reference to `floor` — both
  expected, resolved by matching the Makefile's include/lib set.)

### Gate re-run (this ask)
- `make clean && make all && make test` -> exit 0 (GATE_EXIT=0).
  Compile lines under the baked-in Makefile:2-3 set: `-std=c23 -O2`
  + full WARNFLAGS + `-Werror`, no flag changes. TOTAL: 11254/11254
  passed; test_engine xruns: 0.
- tools/migrate_presets re-run (the ask's named check), built per the
  command above — zero warnings under the full -Werror set:
  - `./tools/migrate_presets presets/*.json` -> rc=0, all six
    "migrated (N bytes, params verified)"; sizes 1865/1976/1930/
    1912/1907/2015 — byte-identical to the M2 log record.
  - Second run: identical output; md5sum of all six presets identical
    across runs (RUN2_BYTES_IDENTICAL) and identical to the pre-run
    hand-edited bytes (HAND_EDIT_BYTES_PRESERVED — canonical writer
    output; presets/*.json diff vs HEAD unchanged by the runs).
  - v1 gate end-to-end: `sed 's/"version":2/"version":1/'` of
    drone-220 to /tmp -> tool `FAIL ...: does not parse: version`,
    rc=1 (R2).
- Note: auditor evidence said `git diff HEAD --name-only` lists 17
  files; this ask counts 16 (all within the ONLY-list except
  src/dsp/mixer.c). The substantive F1 claim is verified regardless;
  the count discrepancy is recorded, not resolved.

## M2 — COMMIT (2026-11-09)

### Commit
- Message: "Widen the mod matrix to 16 slots; preset format v2 lists
  active slots only" (single line, imperative, unsigned).
- Staged exactly (17 files): src/dsp/types.h, src/dsp/mixer.c,
  src/preset/schema.h, src/preset/schema_parse.c,
  src/preset/schema_write.c, src/preset/defaults.c,
  tools/migrate_presets.c, presets/{bass-synth,drone-220,
  drone-ambient,drone-evolving,noise-drone,rhythmic-mech}.json,
  tests/{test_json.c,test_preset.c,test_mixer.c}, LOG_MOD16.md.
- Ask-listed but UNCHANGED vs HEAD, not staged: src/preset/schema_priv.h,
  tests/test_presdirs.c (`git diff HEAD --numstat` lists neither).
- F1 disposition: src/dsp/mixer.c included per this commit ask's list —
  resolves the M2 AUDIT FIX F1 escalation.
- Left unstaged/untracked: all PLAN_*.md, .zcode/, everything else.
- Note (observed, not fixed — log content authored by the M2 sessions):
  a stray "### Scope" block at the old lines 347-351 sits inside the
  M2 section but carries the M1 IMPLEMENT scope text verbatim; flagged
  here for the record.
- Gates: not re-run in this commit ask (no code change made here);
  the M2 gates stand as recorded above (full suite 11254/11254,
  migrate run-twice bytes-identical).
