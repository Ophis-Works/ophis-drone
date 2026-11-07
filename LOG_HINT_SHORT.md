# LOG_HINT_SHORT.md — subagent log (PLAN_HINT_SHORT.md)

## PINS (subagent 1, 2026-11-07) — red first, no implementation

- tests/test_keymap_r36s.c: added P1/P2 (PLAN_HINT_SHORT §1.3).
  - P1: keymap_label pins for all ten bound handheld keys, caller-buffer
    pattern of tests/test_keymap.c (F5): KB_TAB_BACK->"L",
    KB_TAB_NEXT->"R", KB_SEL_UP->"UP", KB_SEL_DOWN->"DOWN",
    KB_VAL_DEC->"Y", KB_VAL_INC->"X", KB_RUN->"A", KB_TRIGGER->"B",
    KB_MENU->"START", KB_MENU2->"SELECT"; 16-byte buffers, buf-identity
    checked (`== lb`).
  - P2: fallback pin — SDL_CONTROLLER_BUTTON_GUIDE (5, no KB_* binds it;
    config.h binds 0,1,2,3,4,6,9,10,11,12,13,14) labels to the
    SDL_GameControllerGetStringForButton string uppercased, expectation
    computed from the SDL string in the test (no hardcoded guess), plus
    a fits-16-bytes check.
- tests/test_keymap_r36s.c header comment updated: the test is now
  SDL-linked (keymap_label lives behind SDL in src/ui/keymap.c); the
  `#define OPHIS_KEYMAP_NO_SDL` include guard was removed.
- Makefile: build rule for test_keymap_r36s now links src/ui/keymap.c
  with `pkg-config --cflags/--libs sdl2`, mirroring
  test_keymap_events_r36s (the old rule compiled SDL-free and cannot
  link keymap_label).
- NOT done (per owner order): no implementation, no commit.

Check run:

    make build/tests/test_keymap_r36s && ./build/tests/test_keymap_r36s

Output (red):

    FAIL tests/test_keymap_r36s.c:68: strcmp(lb, "L") == 0
    FAIL tests/test_keymap_r36s.c:70: strcmp(lb, "R") == 0
    FAIL tests/test_keymap_r36s.c:72: strcmp(lb, "UP") == 0
    FAIL tests/test_keymap_r36s.c:74: strcmp(lb, "DOWN") == 0
    FAIL tests/test_keymap_r36s.c:86: strcmp(lb, "SELECT") == 0
    34/39 passed
    exit=1

Reds = the five keys whose current label (SDL string uppercased:
LEFT SHOULDER / RIGHT SHOULDER / DPUP / DPDOWN / BACK) differs from the
short form. Already green (current behavior coincides with the target):
"Y", "X", "A", "B", "START" and the P2 GUIDE fallback — those pins lock
the short table so the implementation must not disturb them.

## IMPLEMENT (subagent 2, 2026-11-07) — short-name table, keymap.c only

- src/ui/keymap.c: added `button_short_label[SDL_CONTROLLER_BUTTON_MAX]`,
  a static `const char *const` table indexed by SDL_GameControllerButton
  (A=0 "A", B=1 "B", X=2 "X", Y=3 "Y", BACK=4 "SELECT", GUIDE=5 "GUIDE",
  START=6 "START", LEFTSHOULDER=9 "L", RIGHTSHOULDER=10 "R", DPUP=11
  "UP", DPDOWN=12 "DOWN", DPLEFT=13 "LEFT", DPRIGHT=14 "RIGHT"); all
  other entries NULL. keymap_label's button branch now checks the table
  FIRST — `if (KB_LABELS_ARE_BUTTONS && button_short_label[kb])` returns
  `label_copy(entry, buf, n, 1)` (flag 1 per owner order; entries are
  already uppercase so toupper is identity), else falls back to the
  existing SDL_GameControllerGetStringForButton uppercased copy. The
  label_copy contract (buf returned, NULL when it does not fit / s NULL)
  is untouched. No binding, no other file, no desktop-path change: on
  desktop the macro folds false so the table is never consulted, and the
  `!KB_LABELS_ARE_BUTTONS` ophis-id branch is byte-identical.
- Rationale: the long forms (LEFT SHOULDER, DPUP, BACK, ...) came from
  SDL string uppercasing; the bound keys needed the short hint-bar forms
  (PLAN_HINT_SHORT §1.1/§1.2) with the SDL-string fallback retained for
  untabled buttons (P2).
- NOT done (per owner order): no commit.

Check run 1:

    make -B test

Output (tail): TOTAL: 91660/91660 passed (test_keymap_r36s 39/39, i.e.
P1+P2 red->green; desktop test_keymap 46/46 unchanged).

Check run 2 (every binary individually, per owner order):

    for t in build/tests/test_*; do ...; done

Output: all 25 binaries exit=0, e.g. test_keymap_r36s "39/39 passed
(exit=0)", test_keymap "46/46 passed (exit=0)"; full list in the run
log — no red anywhere.
