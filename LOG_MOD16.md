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

### Scope
- Content = the M1 IMPLEMENT (F2+F3) edits documented above; tree
  verified pre-commit via `git diff HEAD --numstat` (the six M1 files
  + this log) and `git status --porcelain` (only .zcode/ + PLAN_*.md
  + the seven staged paths).
