# LOG_REVIEW_2026-11-10.md

## 2026-11-10 — MS1: preset listing safety (H-1, L-3)

What changed:
- tests/test_preset.c: new section 3b — a 60-char base name `*.json`, an
  `a.b.json`, and a valid `ok.json` beside each other in a fresh temp dir;
  asserts `preset_list` reports count == 1 with names[0] == "ok".
- src/preset/preset.c: `preset_list` now validates the stripped base name
  with `name_ok` before counting; unsafe entries are skipped.

Why: H-1 — `memcpy(names[*count], e->d_name, len - 5)` copied unbounded
dirent bytes into a 33-byte row (stack corruption). L-3 — dotted names
(`a.b.json`) were listed but unloadable; `name_ok` rejects dots, lengths
over 32, and bad charset in one place.

Test results (TDD):
- RED: `FAIL tests/test_preset.c:170: c2 == 1`, `FAIL ...:171: strcmp(n2[0], "ok") == 0` — count was 3, first row corrupted by the overflow.
- GREEN after the guard: `test_preset 186/186 passed`.
- Full `make test`: `TOTAL: 11439/11439 passed`.

## 2026-11-10 — MS2: preset I/O robustness (M-2, M-3, L-4, L-7)

What changed:
- tests/test_preset.c: three new sections. 4b — a valid preset padded
  with 65536+16 trailing spaces; `preset_load` must return -1 with
  "preset too large". 4c — after `preset_save`, no `<name>.json.tmp`
  exists and the final file round-trips. 4d — `umask(0)` then
  `preset_ensure_dir` on a fresh path; `(st_mode & 0022) == 0`.
- src/preset/preset.c: `preset_load` — `static char buf[65536]` is now a
  plain local (L-7); oversize probe: full buffer + one extra byte read
  rejects with "preset too large" (M-2). `preset_save` — writes
  `<path>.tmp`, `fflush`+`fsync`+`fclose`, then `rename` to the final
  path; any failure unlinks the temp file (M-3). `preset_ensure_dir` —
  `mkdir(..., 0755)` (L-4).

Why: M-2 — files > 64 KiB were silently parsed from their truncated
leading JSON. M-3 — a crash mid-save tore the preset at the final path.
L-4 — presets dir was created world/group-writable. L-7 — shared static
buffer across calls (re-entrancy/state hazard, no benefit).

Test results (TDD):
- RED: `FAIL tests/test_preset.c:227: preset_load(d3, "big", ...) == -1`,
  `FAIL ...:228: strstr(err, "preset too large")`,
  `FAIL ...:260: (st.st_mode & 0022) == 0` — oversize file returned 0
  (parsed), dir was 0777 under umask(0). The 4c .tmp-absence CHECK was
  already green pre-fix, as the plan predicted (it guards the rename
  path going forward); the round-trip CHECK stayed green throughout.
- GREEN after implementation: `test_preset 199/199 passed`.
- Full `make test`: `TOTAL: 11452/11452 passed`.
