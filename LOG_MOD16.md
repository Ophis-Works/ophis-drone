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

## M3 — P4+P5 red pins (2026-11-09)

### PIN (red)
- Scope: tests/test_seq_items.c (+44) and tests/test_ui_menu.c (+92)
  ONLY (Edit tool); this log. Tree verified: `git diff HEAD --numstat`
  -> exactly those two files; nothing staged, nothing else modified.
- Baseline (pre-edit, binaries up to date from M2):
  `./build/tests/test_seq_items` -> 567/567, exit 0;
  `./build/tests/test_ui_menu` -> 224/224, exit 0.
- P4 (PLAN_MOD_MATRIX_16.md §1.3 lines 76-77), test_seq_items.c new
  block before HARNESS_REPORT: MOD_ITEMS_N == 6; names SLOT · SOURCE ·
  TARGET · DEST · DEPTH · LEVEL in order (owner order 2026-11-09, R4;
  the plan's short forms SRC/TGT/AMT/MIX = the rows SOURCE/TARGET/
  DEPTH/LEVEL); ids 1..5 = IT_XMOD_SRC/TGT/DEST/AMT/MIX; every row
  si == 0 (PR-D: the slot rides the ui state at use time, exactly like
  SEQ PITCH/VEL, whose table si is 0 — test_seq_items.c SEQ_ITEMS
  precedent); the SLOT spec pinned WITHOUT naming the new id (none
  exists yet — naming one would compile-red the whole file and hide
  the semantic reds, against the M2 compile-red-avoidance precedent):
  a scan demands EXACTLY ONE ITEM_SPECS entry with nav == NAV_STEPSEL
  && min 0 && max 15 && wrap 1 (the SEQ STEPSEL stays 0..7, pinned at
  :398-402, so the scan is unique post-M3), the SLOT row's id must BE
  that spec's id, and the spec's tgt == ITT_UI (PR-C: the slot lives
  in ui_t, no param commit).
- P5 (PLAN_MOD_MATRIX_16.md §1.3 lines 78-79), test_ui_menu.c new
  test_mod_slot_row(), called in main after test_valkey_hold_retrig()
  (BEFORE SDL_Quit — the val path reads SDL_GetTicks via val_edit),
  three legs on a raw memset ui (tab TAB_MOD, cursor slot
  TAB_MOD*SUB_COUNT = 9 parked on the SLOT row, mb linked):
  - leg 1: 6x ACT_NAV_RIGHT (SLOT 1 -> SLOT 7 = index 6), 4x
    ACT_SEL_DOWN onto the DEPTH row (flat 4 post-M3; LEVEL today —
    conveniently the same flat index), ACT_VAL_INC; assert
    content_item(4).id == IT_XMOD_AMT, xmod[6].amount == 1.0f
    (fexactf), xmod[0].mix == 0 (int compare — the LEVEL-row
    misdirect), xmod[0].amount == 0.0f.
  - leg 2 (roll-over 0 -> 15): one ACT_NAV_LEFT from fresh, then the
    same DEPTH drive; assert xmod[15].amount == 1.0f, xmod[0].mix == 0,
    xmod[0].amount == 0.0f.
  - leg 3 (roll-over 15 -> 0): 15x ACT_NAV_RIGHT to 15, DEPTH ->
    xmod[15].amount == 1.0f, then one more ACT_NAV_RIGHT (15 -> 0) and
    a second DEPTH edit -> xmod[0].amount == 1.0f and xmod[15].amount
    STILL 1.0f (the second write must not touch slot 15).
  - Edited slots get dest = MOD_PITCH first: an OFF dest dims DEPTH
    (ui.c item_dim IT_XMOD_AMT -> val_edit refuses), true today AND
    post-M3 (R4 keeps off-slot behavior). The selection is ui_t-
    internal (PR-C) and is observed ONLY through the DEPTH writes —
    no new ui_t field is named (same compile-red avoidance as P4).
- Green-today CHECKs, by design (repo precedent, M2 log): the two
  xmod[0].amount == 0 guards in legs 1-2 — post-M3 they prove the
  slot-6/15 edits don't touch slot 0; today they pass vacuously
  because the misdirected write goes to slot 0's MIX (caught by the
  xmod[0].mix == 0 CHECKs, which ARE red). Red state rests on the
  other 11 + 16 CHECKs.

### Commands + output
- Build: `make build/tests/test_seq_items build/tests/test_ui_menu`
  -> rc=0, both recompiled under the baked-in flag set (Makefile:2-3,
  full WARNFLAGS + -Werror, no flag changes), zero warnings. (First
  attempt failed under -Werror=conversion: fexactf against xmod[0].mix,
  an int field — fixed with plain integer comparison.)
- Run: `./build/tests/test_seq_items` ->
      FAIL tests/test_seq_items.c:552: MOD_ITEMS_N == 6                (count)
      FAIL tests/test_seq_items.c:557: ...strcmp(MOD_ITEMS[i].name, M6N[i])   (x6 — SLOT/SOURCE/TARGET/DEST/DEPTH/LEVEL vs the 15-item table)
      FAIL tests/test_seq_items.c:559: MOD_ITEMS[i].si == 0            (i=5: si 1)
      FAIL tests/test_seq_items.c:561: MOD_ITEMS[1].id == IT_XMOD_SRC  (is IT_XMOD_TGT)
      FAIL tests/test_seq_items.c:562: MOD_ITEMS[2].id == IT_XMOD_TGT  (is IT_XMOD_DEST)
      FAIL tests/test_seq_items.c:563: MOD_ITEMS[3].id == IT_XMOD_DEST (is IT_XMOD_AMT)
      FAIL tests/test_seq_items.c:564: MOD_ITEMS[4].id == IT_XMOD_AMT  (is IT_XMOD_MIX)
      FAIL tests/test_seq_items.c:565: MOD_ITEMS[5].id == IT_XMOD_MIX  (is IT_XMOD_SRC)
      FAIL tests/test_seq_items.c:578: slot_id != -1                   (no 0..15 NAV_STEPSEL spec exists)
      FAIL tests/test_seq_items.c:579: MOD_ITEMS[0].id == slot_id
      FAIL tests/test_seq_items.c:580: slot_id != -1 && item_spec(slot_id)->tgt == ITT_UI
      572/588 passed
      exit=1
- Run: `./build/tests/test_ui_menu` ->
      FAIL tests/test_ui_menu.c:819: it.id == IT_XMOD_AMT              (leg 1: flat 4 is LEVEL)
      FAIL tests/test_ui_menu.c:821: fexactf(u.draft.xmod[6].amount, 1.0f)   (slot 6 never written)
      FAIL tests/test_ui_menu.c:822: u.draft.xmod[0].mix == 0          (the edit landed on slot 0's LEVEL)
      FAIL tests/test_ui_menu.c:843: it.id == IT_XMOD_AMT              (leg 2)
      FAIL tests/test_ui_menu.c:845: fexactf(u.draft.xmod[15].amount, 1.0f)  (NAV_LEFT inert: no 0->15 roll)
      FAIL tests/test_ui_menu.c:846: u.draft.xmod[0].mix == 0
      FAIL tests/test_ui_menu.c:867: it.id == IT_XMOD_AMT              (leg 3)
      FAIL tests/test_ui_menu.c:869: fexactf(u.draft.xmod[15].amount, 1.0f)  (15 NAV_RIGHTs inert)
      FAIL tests/test_ui_menu.c:870: u.draft.xmod[0].mix == 0
      FAIL tests/test_ui_menu.c:873: fexactf(u.draft.xmod[0].amount, 1.0f)   (after 15->0 NAV: inert, edit again hit LEVEL)
      FAIL tests/test_ui_menu.c:874: fexactf(u.draft.xmod[15].amount, 1.0f)
      226/237 passed
      exit=1
- No existing CHECK moved: 567 old seq_items + 224 old ui_menu CHECKs
  still pass (572/588 = 567+5, 226/237 = 224+2, the +5/+2 being the
  documented green-today guards). Red is EXCLUSIVELY the new pins.
- No source files touched: P4/P5 red does not require it (MOD_ITEMS_N
  15 at src/ui/seq_items.c:79/MOD_ITEMS :93-103; no MOD row has nav !=
  NAV_NONE in ITEM_SPECS :248-257; ui.c ACT_NAV NAV_NONE inert :562-563).

### State
- P4 + P5 RED proven. Not committed (per workflow instruction).
- Not run in this ask (scheduled as later M3 workflow steps per
  PLAN_MOD_MATRIX_16.md §2.1): the M3 IMPLEMENT (SLOT row + 6-item
  model, ui.c slot state, ui_draw SLOT display), then the forced-
  rebuild suite gate and ASan on the UI tests.
- Implement-time notes for the M3 implement agent: (a) the OLD MOD
  pins in tests/test_seq_items.c (:129-149 item table, :210-218
  MOD_SECTIONS, :446-458 the PLAN_OSC_NAMING P1 block, :434-435
  page_get) still encode the 15-item contract and must be rewritten
  WITH the implementation or the suite cannot go green; (b) the P4
  scan keys on nav == NAV_STEPSEL && 0..15 && wrap — the SLOT spec
  must carry those bounds and tgt ITT_UI, and MOD_ITEMS[0].id must BE
  that spec's id (any id enum name works); (c) P5 observes only
  through DEPTH writes, so the slot-selection mechanism (field name,
  NAV_STEPSEL extension) is unconstrained — but content_item must
  resolve the xmod rows' si at use time (PR-D) and val_edit's dim gate
  must follow the RESOLVED slot's dest.

### IMPLEMENT (green)
- Scope: src/ui/seq_items.h, src/ui/seq_items.c, src/ui/ui.h,
  src/ui/ui.c, src/ui/ui_draw.c (the charter's file list), plus the
  old-pin rewrites in tests/test_seq_items.c and
  tests/test_ui_menu.c noted in (a) above. Edit tool only; no DSP, no
  schema, no Makefile/flag change. NOT committed (per workflow
  instruction).
- seq_items.h: new id IT_MOD_SLOT (MOD-page SLOT row, ITT_UI);
  MOD_ITEMS_N 15 -> 6; MOD_SECTIONS_N 3 -> 1; new fmt ITF_SLOT (UI
  state selector, wraps, "SLOT %d" v+1 display — the ITF_STEPSEL
  prefix "STEP %d" stays the SEQ STEP row's).
- seq_items.c: MOD_ITEMS = SLOT · SOURCE · TARGET · DEST · DEPTH ·
  LEVEL, every si 0 (PR-D: the slot rides the ui state at use time);
  TARGET was OSC (renamed, owner order 2026-11-09); MOD_SECTIONS = one
  { "MOD", 0, 6 } label row (draw_sections draws items per section, so
  the single section must cover all 6 rows); ITEM_SPECS gains
  [IT_MOD_SLOT] = { ITF_SLOT, 0, 15, wrap 1, ITT_UI, PK_NONE,
  NAV_STEPSEL } — the ONE NAV_STEPSEL 0..15 entry (the SEQ STEPSEL
  stays 0..7, pinned test_seq_items.c:398-402 -> P4's uniqueness scan
  holds). File-top page descriptions updated (they still said
  "MOD 1..MOD 3 ... = 9").
- ui.h: ui_t gains int mod_slot (PR-C, alongside seq_sel; zero-init =
  SLOT 1; UI state only — never committed, not persisted).
- ui.c:
  - content_item binds ITT_XMOD rows' si to ui->mod_slot at use time
    (spec-driven tgt check, PR-D — same pattern as the SEQ
    PITCH/VEL si binding); param_get/param_set need NO change: the
    PK_XMOD_* routes take the resolved si, so SRC/TGT/DEST/DEPTH/LEVEL
    read/write draft.xmod[mod_slot]. The ITF_DEST re-clamp carries the
    resolved si too (amt = it; amt.id = IT_XMOD_AMT).
  - item_get/item_set: the ITT_UI holder is keyed on the SPEC's fmt
    (no id comparisons, R12a): ITF_SLOT -> ui->mod_slot, ITF_STEPSEL ->
    seq_sel[chain_of]. New const-aware statics ui_sel_get/ui_sel_ref
    (-Wcast-qual forbids the const-cast form).
  - ACT_NAV NAV_STEPSEL branch factored into sel_roll (spec bounds +
    spec-keyed holder); NEW: NAV_NONE rows on TAB_MOD fall back to
    sel_roll(item_spec(IT_MOD_SLOT)) — P5 leg 3 presses NAV_RIGHT on
    the DEPTH row (cursor flat 4) and demands the 15 -> 0 roll-over,
    so on the MOD page LEFT/RIGHT roll the SLOT selection from ANY
    row; the SLOT row itself stays fully spec-driven (its NAV_STEPSEL
    spec supplies the bounds). SEQ page row-scoped NAV pins
    (test_seq_nav_rows) and the MASTER page inertness are untouched:
    the fallback keys on !is_osc_tab && tab == TAB_MOD.
  - edit_item: ITF_SLOT joins the R11 wrap-formula group (a wrapping
    selector; item_set routes it to mod_slot; the tgt != ITT_UI gate
    skips the mailbox commit, exactly like the STEP row).
- ui_draw.c: item_value gains case ITF_SLOT -> "SLOT %d" (v+1,
  1-based like ITF_STEPSEL/ITF_TGT). draw_sections needed no change:
  the SLOT row renders through the generic path (label/value, cursor
  highlight); the digit marker's NAV_DIGITS gate simply never fires on
  it (existing marker/focus behavior, R13). ui_menu.c and
  valrepeat.c untouched (no item-model references / out of scope).
- Test rewrites (the log's note (a)):
  - test_seq_items.c: the 15-item MOD table pin -> the 6-item model
    (names/ids/si==0, spot check MOD_ITEMS[1] == IT_XMOD_SRC);
    MOD_SECTIONS pin -> 1 section "MOD" 0/6 (gcc had compile-red the
    old pin: MOD_SECTIONS[2] above the now-length-1 array bounds under
    -Werror=array-bounds); the PLAN_OSC_NAMING P1 block re-pins the
    new names (re-pin precedent, like its P2 does for MASTER).
    page_get(3,0) needed no edit (macro-keyed). The P4 pin block is
    untouched and passes as written.
  - test_ui_menu.c: test_item_value_enum_rows' MOD flat indices move
    to the 6-row model (DEST 2 -> 3, SOURCE 0 -> 1, TGT 1 -> 2; si
    still 0 via the resolved mod_slot) and gain ONE green-today guard
    (repo precedent, M2/M3 log): content_item(0) -> IT_MOD_SLOT,
    item_value -> "SLOT 1" (R4 display). P5's test_mod_slot_row is
    untouched and passes as written.
- Value-key note (no scope to change valrepeat.c): val_edit's inert
  gate is still `id == IT_SEQ_STEPSEL`, so X/Y on the SLOT row goes
  through edit_item -> wraps mod_slot 0..15 (bounded, no mailbox
  commit since tgt == ITT_UI, no retrig off the OSC tabs) with the
  standard edit pulse/hold — selector-row behavior, not STEP-row
  inertness. No pin covers it; logged as observed behavior.

### Commands + output (green)
- Build: `make build/tests/test_seq_items build/tests/test_ui_menu`
  -> rc=0, both recompiled under the baked flag set (Makefile:2-3,
  -std=c23 -O2 + full WARNFLAGS with -Werror; no flag changes), zero
  warnings.
- Run: `./build/tests/test_seq_items` -> 541/541 passed, exit=0
  (P4 GREEN; the total moved from 588 because the rewritten old pins
  loop over 6 rows instead of 15 — fewer CHECK executions, zero
  failures).
- Run: `./build/tests/test_ui_menu` -> 239/239 passed, exit=0
  (P5 GREEN; 237 -> 239 = the two new SLOT-display guard CHECKs).
- Full gate, exactly as asked: `make clean && make all` -> rc=0,
  single-TU desktop build, zero warnings (any warning aborts under
  -Werror); `make test` -> rc=0, all 26 binaries green,
  TOTAL: 11243/11243 passed, 0 FAIL lines (11254 -> 11243 = the same
  dynamic CHECK-count effect as above).
- Tree: `git diff HEAD --numstat` (final, after this append) ->
  LOG_MOD16.md 218/0 (the red ask's +114 plus this M3 IMPLEMENT
  record), src/ui/seq_items.c 20/19, src/ui/seq_items.h 19/7,
  src/ui/ui.c 51/13, src/ui/ui.h 5/0, src/ui/ui_draw.c 5/0,
  tests/test_seq_items.c 75/37, tests/test_ui_menu.c 106/3 — exactly
  the charter's file set; nothing staged, no untracked additions by
  this ask.

### State
- P4 + P5 GREEN proven; full forced-rebuild suite green under the
  baked -std=c23 -Werror WARNFLAGS. Not committed (per workflow
  instruction).
- Not run in this ask (scheduled as the next M3 workflow step per
  PLAN_MOD_MATRIX_16.md §2.3): ASan on the UI tests; commit.

## M3 — COMMIT (2026-11-09)

### Commit
- Message: "Select mod-matrix slots 1..16 on the MOD page with a SLOT
  row" (single line, imperative, unsigned).
- Staged exactly (8 files): src/ui/seq_items.h, src/ui/seq_items.c,
  src/ui/ui.h, src/ui/ui.c, src/ui/ui_draw.c,
  tests/test_seq_items.c, tests/test_ui_menu.c, LOG_MOD16.md.
- Left unstaged/untracked: all PLAN_*.md, .zcode/, everything else.
- Gates: not re-run in this commit ask (no code change made here);
  the M3 gates stand as recorded above (P4 541/541, P5 239/239,
  `make clean && make all` rc=0, `make test` 11243/11243, exit 0).
- ASan on the UI tests: NOT run in any recorded M3 ask, not run here —
  remains open per PLAN_MOD_MATRIX_16.md §2.3.

## M4 — R7 docs corrected (2026-11-09)

### Edits (Edit tool only; scope = ARCHITECTURE.md + MANUAL.md)
- ARCHITECTURE.md §2 diagram node: "3 matrix slots / SOURCE x OSC x
  DEST x DEPTH + LEVEL" -> "16 matrix slots / SOURCE x TARGET x DEST
  x DEPTH + LEVEL" (MOD_SLOTS 16 at src/dsp/types.h:82; the TARGET
  row name at src/ui/seq_items.c:99).
- ARCHITECTURE.md §4: heading "(version 1)" -> "(version 2)"; parser
  line now states version 2 only, version 1 files reject
  (src/preset/schema_parse.c:490); array lengths stated as exactly
  3 chains / 2 subs / 8 steps (schema_parse.c:8 header) with "xmod"
  0..16 entries — ACTIVE slots only, ascending, same 5 keys per
  entry, absent slots OFF, empty array = matrix fully off
  (writer emits active-only via xmod_is_off, schema_write.c:245-252;
  version wnumd(w, 2) schema_write.c:176); the "(3 slots: ...)"
  key-group text dropped.
- MANUAL.md §6: "Three slots." -> "16 slots."; documents the SLOT row
  selecting the slot (left/right or X/Z, roll-over) with the five
  rows below — SOURCE · TARGET (renamed from OSC) · DEST · DEPTH ·
  LEVEL — editing the selected slot (MOD_ITEMS order
  src/ui/seq_items.c:98-101; 1-based "SLOT %d" display
  src/ui/ui_draw.c:120; X/Z wrap verified by M3's value-key note,
  roll-over by P5 legs 2-3).
- No other lines touched in either file. NOT committed (per workflow
  instruction). The other M4 steps (full suite + smoke + ASan +
  make handheld zip + commit, plan §2.1) are separate scheduled asks,
  not run here.

### Check (the ask's named repo-wide grep, run post-edit)
- Command: `grep -rniE 'three slots|3 slots|xmod\[3\]|3 xmod|xmod
  \(3|3/2/8/3|3 objects'` and `grep -rniE 'version 1|fixed-length'`,
  both over --include md/html/txt --exclude-dir .zcode,build
  --exclude 'PLAN_*.md', rc=0 each.
- The two scope docs are CLEAN: ARCHITECTURE.md's only "version 1"
  hit is the new "version 1 files reject" sentence (no 3-slot hits);
  MANUAL.md zero hits on both patterns.
- Repo-wide the check still FAILS outside this ask's file scope —
  stale current-state claims remain, NOT edited (outside the
  two-doc Edit scope; flagged for owner dispatch):
  README.md:58 "Presets are JSON (version 1)" + :64 "`xmod[3]`";
  IMPLEMENTATION.md:78 + :340 "`xmod[3]`" + :558-559 "version 1,
  ... fixed-length arrays"; docs/manual.html:240 "Preset files
  (JSON version 1)" + :256 "xmod: [ 3 objects ]" + :299 "fixed:
  ... 3 xmod"; PLAN.md:246 "3 slots" + :568 "Preset JSON format
  (version 1)" + :597 fixed-length 3 xmod slots + :606 "Format
  **version 1**".
- Not stale, left alone: BUILD_NOTES.md:36 "version 1 (SYSV)" is an
  ELF readout; LOG_MOD16.md and LOG_OSC_NAMING.md "3 slots" /
  "version 1" lines are dated historical red-pin/session records
  (LOG_MOD16.md:191 says "today's xmod[3]" of the pre-M2 state),
  accurate as history.

## M4 — AUDIT FIX: stale claims in README/PLAN/IMPLEMENTATION/manual.html (2026-11-09)

### Audit findings — all nine verified this ask
- Ran the auditor's greps; every quoted line reproduced exactly:
  README.md 15/33/58/64; PLAN.md 246/568/597/606;
  IMPLEMENTATION.md 78/340/558-559; docs/manual.html 240/256/299
  (tracked, hand-maintained — no generator: `grep -n manual Makefile`
  no match; repo-wide 'manual.html' script/Makefile reference grep no
  match; tools/ = handheld_gate.sh, migrate_presets.c,
  r36s-launcher.sh only). All outside the previous ask's two-doc scope
  (LOG_MOD16.md M4 section had flagged them for owner dispatch).

### Fixes (Edit tool only; the nine findings)
- README.md:15 "(3-slot matrix)" -> "(16-slot matrix)".
- README.md:33 the 3-slot SRC/DEST/AMT sentence -> 16-slot matrix with
  the SLOT row (1-16, roll-over) + SOURCE · TARGET · DEST · DEPTH ·
  LEVEL (the M3 model, src/ui/seq_items.c:97-99, ui_draw.c:119).
- README.md:58 "(version 1)" -> "(version 2)".
- README.md:64 "`xmod[3]` (`src`, `dest`, `amount`)" -> "`xmod`
  (0..16 entries — the active slots only; `src`, `tgt`, `dest`,
  `amount`, `mix` per entry; slots absent from the array are off)";
  :66 "Fixed array lengths" scoped to chains/subs/steps with xmod the
  variable-length exception.
- PLAN.md:246 "3 slots" -> "16 slots (MOD_SLOTS)" with the widening
  plan cited (src/dsp/types.h:82).
- PLAN.md §9: heading :568 -> "(version 2)"; example `"version": 1`
  :572 -> 2; example xmod :586 -> one 5-key active entry; :597
  fixed-length bullet -> 3/2/8 fixed + `xmod` 0..16 active-only,
  ascending, >16 invalid; :606 -> "Format **version 2** (version 1
  files are rejected by the parser)".
- IMPLEMENTATION.md:78 `xmod[3]` -> `xmod[16]`; :340 `mxp = {xmod[3]}`
  -> `{xmod[16]}` (sketch style keeps literals, as chain[3]/seq[3]).
- IMPLEMENTATION.md:558-559 -> "(version 2, enum strings, fixed arrays
  3 chains / 2 subs / 8 steps, `xmod` 0..16 active-slot entries, `run`
  excluded)".
- docs/manual.html:240 heading -> version 2; :256 `"xmod": [ 3
  objects ]` -> `[ 0..16 objects ]` + active-only comment; :299-300 ->
  fixed 3/2/8 + `xmod` 0..16 active-only/ascending/empty=off.

### Same-plan same-class fixes in the ask's files (NOT in the findings; logged for the record)
- README.md:32 "MOD into MOD 1..3" -> "MOD into MOD" (M3:
  MOD_SECTIONS_N 3 -> 1).
- docs/manual.html:163-178 MOD tab: sections MOD 1/2/3, three slots,
  SRC/TGT/DEST/AMT/MIX -> one MOD section, SLOT row (LEFT/RIGHT from
  any row or X/Z, roll-over) + SOURCE/TARGET/DEST/DEPTH/LEVEL (row
  names per seq_items.c; the AMT-range and ADD-knob facts kept);
  :250 example `"version": 1` -> 2; xmod bullet retitled "entry (one
  per active slot)".
- IMPLEMENTATION.md:490-491 (§6 kept-current spec) MOD 1/2/3 9-item
  bullet -> the M3 6-row SLOT model, widening cited.
- PLAN.md:391 page table MOD row -> SLOT (1..16, rolls over) +
  SOURCE · TARGET · DEST · DEPTH · LEVEL (P5 legs 2-3 prove the
  roll-over; suite green 239/239 M3 log).

### Check (this ask)
- `grep -rniE 'three slots|3 slots|xmod\[3\]|3 xmod|xmod \(3|3/2/8/3|3
  objects' --include='*.md' --include='*.html' --include='*.txt'
  --exclude-dir=.zcode --exclude-dir=build --exclude='PLAN_*.md' .`
  -> rc=0; and the same for `version 1|fixed-length` -> rc=0.
- The four fixed files are clean: the ONLY pattern hit left in them is
  PLAN.md:611 "(version 1 files are rejected by the parser)" — the
  corrected statement itself. Remaining repo-wide hits, all verified
  NOT stale: LOG_MOD16.md/LOG_OSC_NAMING.md dated historical records;
  BUILD_NOTES.md:36 "version 1 (SYSV)" ELF readout;
  ARCHITECTURE.md:83 the new "version 1 files reject" sentence;
  docs/manual.html:256-257 `"chain"/"seq": [ 3 objects ]` — TRUE under
  v2 (exactly 3 chains / 3 seq objects unchanged; parser bounds 3,3 /
  3,3 / 2,2 / 8,8, xmod 0..16); PLAN.md:599 the corrected
  fixed-length sentence scoped to 3/2/8.
- Tree: `git status --porcelain` (tracked) -> ARCHITECTURE.md,
  IMPLEMENTATION.md, LOG_MOD16.md, MANUAL.md, PLAN.md, README.md,
  docs/manual.html — the prior M4 doc work plus this ask's four files;
  NO code file touched, nothing staged.
- Not run in this ask: build/tests/smoke/ASan/handheld — zero code
  changes (docs only), no gate named by the ask; the M4 ship gates
  remain scheduled asks per PLAN_MOD_MATRIX_16.md §2.1 and the M4 log
  above. Also left alone (pre-existing, NOT this plan's staleness, not
  flagged): README.md:60 + docs/manual.html:245,247 name
  `src/preset/schema.c`, which no longer exists (schema.h/
  schema_parse.c/schema_write.c/schema_tab.c/schema_priv.h); and
  README.md:91 "all 16 unit test suites" (the suite is larger from
  other plans' work). Flagged here for owner dispatch.

## M4 — COMMIT (2026-11-09)

### Commit
- Message: "Document the 16-slot matrix and the active-only preset
  format" (single line, imperative, unsigned).
- Staged exactly (7 files): ARCHITECTURE.md, MANUAL.md, LOG_MOD16.md,
  plus the four auditor-listed doc files from the M4 audit-fix findings
  (README.md, PLAN.md, IMPLEMENTATION.md, docs/manual.html — the
  stale-claim files the findings enumerated, fixes recorded above).
- Left unstaged/untracked: all PLAN_*.md (19 untracked), .zcode/,
  everything else. PLAN.md (tracked, modified, in the auditor's
  findings list) is staged — it does not match the PLAN_*.md glob.
- Pre-existing non-mod16 doc defects (schema.c references, "all 16
  unit test suites"), flagged for owner dispatch above: left untouched,
  committed as-is where already inside the staged files' edits.
- Gates: not re-run in this commit ask (docs-only change, no gate
  named); the M4 doc grep checks stand as recorded above (rc=0 both
  patterns; the four fixed files clean except the corrected-statement
  hit PLAN.md:611). M4 ship gates (full suite + smoke + ASan + make
  handheld zip) remain scheduled asks per PLAN_MOD_MATRIX_16.md §2.1.
