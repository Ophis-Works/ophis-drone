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
