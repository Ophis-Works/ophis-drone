# LOG_HACK_REVIEW.md

## 2026-11-09 — M1 (F1, amended by owner ruling 2b) (workflow subagent
## "Implementer-M1")

PLAN_HACK_REVIEW_FIXES.md §1 F1: the snprintf buffer-size contract
becomes the type (`size_t`), not a comment. Both signatures `size_t
blen`; every `(size_t)blen` cast in ui_draw.c deleted; ui_internal.h and
the test extern mirror updated. Amendment 2b: ui_enum_display /
ui_dest_display / ui_xsrc_display (seq_items.h/.c) also take `size_t
cap`, and EVERY call site in the display chain updated so no narrowing
conversion remains (the original "call sites unchanged" claim was based
on a wrong fact — item_value forwards blen into those helpers; test
call sites passed `(int)sizeof buf`).

Pre-existing tree note: on start the working tree already carried
UNCOMMITTED modifications matching F1's core exactly (fmt_sec/item_value
`size_t blen` in ui_draw.c, ui_internal.h:59, test_ui_menu.c:340 extern
mirror — confirmed via `git diff` before any edit). This agent verified
those against the plan and did not re-apply them; all remaining M1 work
(seq_items signatures + all test call sites) was applied here.

### Files changed
- `src/ui/ui_draw.c` (pre-existing in tree, verified this session)
  - `fmt_sec(char *b, size_t blen, double t)` (ui_draw.c:56) and
    `item_value(..., char *b, size_t blen)` (ui_draw.c:63); all 20
    `snprintf(b, ...)` sites in both functions take `blen` bare; the
    "every caller passes sizeof-buf" comments deleted (the contract is
    the type now). Rationale: F1 — safety must not rest on a comment.
  - `(size_t)n1`/`(size_t)n2` in hint_binding and `(size_t)dy`/`(size_t)g`
    in font_atlas_build are NOT blen casts: the former is the
    keymap_label int-len contract (F1 non-goal, documented F5), the
    latter is atlas indexing — both kept, per plan.
- `src/ui/ui_internal.h` (pre-existing in tree, verified this session)
  - `void item_value(const ui_t *ui, item_t it, char *b, size_t blen);`
    (ui_internal.h:59).
- `src/ui/seq_items.h`
  - `ui_enum_display`, `ui_xsrc_display`, `ui_dest_display`: `int cap`
    -> `size_t cap` (seq_items.h:279,288,289). Rationale: amendment 2b —
    item_value forwards its `size_t blen` into these helpers; an int cap
    there is a narrowing conversion in the display chain.
- `src/ui/seq_items.c`
  - Definitions match (seq_items.c:139,155,173); `snprintf(buf,
    (size_t)cap, ...)` cast dropped in ui_xsrc_display (seq_items.c:164);
    loop counters `int i` -> `size_t i` in ui_enum_display and
    ui_dest_display (int i < size_t cap - 1 would warn under
    -Wsign-conversion; cap comes only from sizeof, so > 0 and the
    `cap - 1` semantics are unchanged). Rationale: amendment 2b +
    warning-free gate.
- `tests/test_ui_menu.c`
  - 14 `item_value(&u, it, b, (int)sizeof b)` sites -> `sizeof b`
    (test_ui_menu.c:372,378,385,390,397,402,423,427,431,435,442,449,477,594);
    5 `ui_enum_display(..., (int)sizeof b)` sites -> `sizeof b`
    (test_ui_menu.c:866,868,870,872,874). Rationale: `(int)sizeof` is a
    narrowing conversion at a `size_t` parameter — banned by amendment
    2b anywhere in the display chain.
  - NOT touched (own contracts, not display chain): `preset_dir(stage,
    (int)sizeof stage)` (:800), `preset_save(..., (int)sizeof err)` (:807),
    the snprintf-return `(int)sizeof p` compare (:61).
- `tests/test_seq_items.c`
  - 6 `ui_xsrc_display(N, nb, (int)sizeof nb)` sites -> `sizeof nb`
    (test_seq_items.c:499,501,503,505,507,509). Rationale: same as above
    — the amendment's "every call site" covers these; leaving them would
    keep narrowing conversions in the display chain.

### Method note (TDD)
Plan §2.2 assigns the red-first PIN to M2 only ("1. PIN (M2 only)"); M1
is defined as edits -> gates -> commit. The pre-existing uncommitted
implementation side already matched F1, so a reconstructible red state
would have required reverting ordered work — not done. Signature-level
verification is the -Werror compile gate below.

### Verification (executed this session)
- `git diff` on all five files before editing: confirmed the
  pre-existing core matched F1 exactly; seq_items.h/.c and the call
  sites untouched at start.
- `grep -c '(size_t)blen' src/ui/ui_draw.c` -> 0 (P1). Remaining
  `(size_t)` in ui_draw.c: 4 sites, none on the blen chain (see above).
- Repo-wide grep: every caller of the three display helpers and
  item_value enumerated (ui_draw.c bare `blen`; test_ui_menu.c 14+5;
  test_seq_items.c 6 — all cast-free now).
- Gate: `make clean && make all && make test` — both targets compile
  warning-free under the Makefile's baked-in `-std=c23 -O2` + full
  WARNFLAGS (`-Werror -Wconversion -Wsign-conversion -Wfloat-equal
  -Wswitch-enum ...`; no Makefile change). Tests: TOTAL 11203/11203
  passed, test_engine xruns: 0, EXIT=0. test_seq_items 567/567,
  test_ui_menu 224/224.

NO commit made (stage order: commit is the workflow script's step).
M2 (F2+F3) not started.

## 2026-11-09 — M1 audit-fix round (workflow subagent "Gates-fixer")

The auditor rejected M1 with six findings. The amended M1 ask now
requires ZERO `(size_t)` casts in ui_draw.c and NO `(int)sizeof` in the
test call-site files; fixes and dispositions per finding:

### Fixed (code)
- F-a1 `src/ui/ui_draw.c:247,261,265` — `hint_binding` params
  `int n1, int n2` -> `size_t n1, size_t n2`; `(size_t)n1`/`(size_t)n2`
  at the snprintfs deleted. Sole caller (ui_draw.c:540, was :539)
  already passes bare `sizeof b`/`sizeof b2` where `char b[160],
  b2[160]` (ui_draw.c:351) — the contract is the type, matching F1;
  zero narrowing anywhere. keymap_label's size_t contract
  (keymap.h:53) untouched — F1 non-goal (F5) intact.
- F-a2 `src/ui/ui_draw.c:153-154` — atlas row pointer arithmetic
  `(size_t)dy`/`(size_t)g` -> bare. dy<=9, g<=95; max offset
  9*(96*10)+95*10 = 9590 < 9600 = ATLAS_GLYPHS*W*H, far below INT_MAX
  (OPHIS_FONT_W/H = 10, font8x8.h:10-11); int -> ptrdiff_t widening in
  pointer arithmetic is sign-preserving. Probe-verified: a snippet with
  this exact expression form compiled clean under the Makefile's exact
  WARNFLAGS before editing.
- F-a3 `tests/test_ui_menu.c` — `(int)sizeof` zero sites:
  :61 purge_dir rewrite to `int nw = snprintf(...);
  if (nw < 0 || (size_t)nw >= sizeof p) continue;` — same unlink set
  for every n >= 0; negative returns now skip (the old form unlinked a
  possibly-uninitialized p). `(size_t)nw` is an explicit conversion of
  snprintf's int return — the only cast-free way to compare against the
  size_t buffer; `(int)sizeof` banned. :800 `preset_dir(stage,
  (int)sizeof stage)` -> `sizeof stage`; :807 `preset_save(...,
  (int)sizeof err)` -> `sizeof err` — preset_dir/preset_save KEEP their
  int-len contracts (preset.h:12,28; outside the display chain, files
  outside the allowed list); a fitting `sizeof` constant at an int
  parameter is warning-free — probe-verified under the exact WARNFLAGS
  before editing.

### Disposition (no code change, recorded)
- F-a4 `tests/test_seq_items.c:499-509` — the 6-site change stays: it
  is exactly what owner ruling 2b mandates ("every call site is
  updated so NO narrowing conversion remains anywhere in the display
  chain", PLAN_HACK_REVIEW_FIXES.md §1 F1). The ask's allowed six-file
  list omitted a 2b-required file — an ask defect, not a rogue change.
  Reverting would reintroduce banned `(int)sizeof` narrowing.
- F-a5 `LOG_DEAD_VARS.md` (+12) — pre-existing at session start
  (conversation-start git status snapshot lists `M LOG_DEAD_VARS.md`);
  the entry records commit 78734c6's commit session, belonging to the
  dead-vars milestone's log, not M1. Not mine to revert (would destroy
  another session's record) or commit (commit forbidden this stage).
  Stands as-is.
- F-a6 `LOG_HACK_REVIEW.md` — correct by git definition: an untracked
  file can never appear in `git diff HEAD`. Mechanics note on the ask's
  phrasing; no change possible or needed.

### Verification (executed this session)
- Probe: /tmp/ophis-castcheck/probe.c compiled with the Makefile's
  exact WARNFLAGS — PROBE_CLEAN (bare-int atlas pointer arithmetic;
  constant `sizeof` at an int-len parameter; `(size_t)nw` compare).
- `grep -c '(size_t)' src/ui/ui_draw.c` -> 0 (was 4). Remaining
  `(int)sizeof` in ui_draw.c: :161 only — SDL_UpdateTexture's int-pitch
  contract (foreign API, constant 3840 fits, not a display-chain
  buffer-size cast, not in the findings).
- `grep -c '(int)sizeof' tests/test_ui_menu.c` -> 0 (was 3);
  `grep -c '(int)sizeof' tests/test_seq_items.c` -> 0.
- Gate: `make clean && make all && make test` — every target compiled
  under `-std=c23 -O2 -Werror` + full WARNFLAGS with zero warnings;
  TOTAL 11203/11203 passed; test_ui_menu 224/224; test_seq_items
  567/567; test_engine xruns: 0.

NO commit made (commit remains the workflow script's step).

## 2026-11-09 — M1 commit (workflow subagent "Committer")

Commit made per the workflow script's stage order. Message: "Take the
UI value buffer size as size_t; the per-site casts are gone".

### Committed (staged exactly these six)
- `src/ui/ui_draw.c`, `src/ui/ui_internal.h`, `src/ui/seq_items.h`,
  `src/ui/seq_items.c`, `tests/test_ui_menu.c`, `LOG_HACK_REVIEW.md`.

### Left unstaged (per the ask: "Leave everything else unstaged")
- `tests/test_seq_items.c` — its 6 `ui_xsrc_display` call-site changes
  (test_seq_items.c:499-509, per the M1 entry above) are part of M1 /
  owner ruling 2b but were not named in this commit's file list; the
  working-tree change remains uncommitted. Discrepancy recorded, not
  silently dropped. `LOG_DEAD_VARS.md` and the untracked PLAN_*.md /
  .zcode/ files likewise untouched.
