# LOG_PRESET_DIR_REVIEW_FIXES.md — implementation log (changes + rationale)
# Plan: PLAN_PRESET_DIR_REVIEW_FIXES.md. No subagents (user order 2026-11-05).

## M0 — Baseline

- Reviewed the uncommitted "presets/ beside the executable" change:
  PLAN.md §8.5, IMPLEMENTATION.md §2/§7, README.md, CHECKLIST_R36S.md,
  docs/manual.html, src/preset/preset.{c,h}, tests/test_preset.c,
  tests/test_ui_menu.c, Makefile.
- `make test`: 97415/97415 passed (all green BEFORE fixes — the bugs below
  are latent, not covered by the current tests).
- Findings B1..B7 recorded in the plan table. The preset-dir RULE itself
  (exe-relative only, no env/HOME/fallbacks) is correct and unchanged.
- Committed the reviewed change as its planned commit, then this plan+log.
  AGENTS.md (user's own rewrite) and .zcode/ stay uncommitted.

## M1 — preset_dir() hardening (TDD)

- RED: added contract CHECKs to tests/test_preset.c §6 —
  `preset_dir(NULL, 512)`, `(db, 0)`, `(db, -1)`, exact-fit boundary
  `strlen(want)` vs `strlen(want)+1`. Run: SEGFAULT (glibc writes through
  the NULL out under snprintf — B3 is reachable UB, not theoretical).
- FIX: src/preset/preset.c preset_dir() — restored `!out || out_len <= 0`
  guard (B3); `n <= 0` and `n >= sizeof exe - 1` readlink guards (B1/B2/
  B4 — no `exe[-1]` write, no uninitialized strrchr scan, no silent
  truncation); `!slash` guard (NULL-deref insurance; unreachable for a
  resolved /proc/self/exe). All guards return -1; the rule (exe-relative
  only, no env/HOME/fallbacks) unchanged.
- preset.h: doc contract updated to the real -1 set.
- GREEN: test_preset 170/170 (+6 CHECKs), full suite 97421/97421,
  ASan+UBSan build clean 170/170.
- The readlink-failure branch itself cannot be triggered in-process
  (procfs is always mounted here); it is guard-by-code + review, pinned
  in the plan (R4).

## M2 — test_ui_menu staging (TDD)

- RED: current binary run from /tmp → `FAIL test_ui_menu.c:150
  ui.load_count == 0`, 46/47. Cause B5: staging used CWD-relative
  "build/tests/presets" while the UI reads the exe-absolute dir — the
  two only coincide when CWD == repo root.
- FIX (tests/test_ui_menu.c): stage in `preset_dir()`'s own output
  (absolute, exe-relative) — CWD-independent; `preset_save` creates the
  dir (mkdir call dropped, sys/stat.h include with it). Cleanup before
  the empty-LOAD test now purges EVERY entry via opendir/readdir/unlink
  then rmdir (B6: no more ENOTEMPTY trap from interrupted-run leftovers).
  dirent.h added.
- GREEN: 49/49 from repo root AND from /tmp (+2 CHECKs: opendir, rmdir);
  full suite 97423/97423.

## M3 — Manual + full verification

- docs/manual.html §2.5 (B7): implementation named as `src/preset/schema.c`
  over vendored `third_party/json.h` (was `src/preset/json.c`, ×2 + the
  header source list ×1); field-list block gains top-level `"bpm"`
  (GLOBAL tempo 1..500); seq bullet no longer lists per-seq `bpm` (none
  exists) and says so; re-wrapped the two crammed lines from the earlier
  preset-location edit.
- B8 (found here): desktop target prerequisites omitted DSP/SEQ/PRESET
  sources → RED `touch src/preset/preset.c && make` = "nothing to do".
  FIX: added `$(DSP_SRC) $(SEQ_SRC) $(PRESET_SRC)` to
  build/desktop/ophis-drone's prerequisite list. Rebuilt, re-smoked.
- Verification: `make test` 97423/97423; valgrind (leak-check=full,
  error-exitcode) clean on test_preset 170/170 and test_ui_menu 49/49;
  desktop rebuild + smoke (SDL dummy video/audio, OPHIS_AUTOQUIT_MS=2000)
  last line XRUNS 0 — on a CURRENT binary this time; `make handheld`
  gate OK, zip lists binary + launcher + 6 factory presets +
  gamecontrollerdb.txt (12 files).
