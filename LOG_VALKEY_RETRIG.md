# LOG_VALKEY_RETRIG.md — implementation log. Plan: PLAN_VALKEY_RETRIG.md.
# Owner order 2026-11-05: holding X or Y (the value keys) must produce
# RETRIGGERS.

## M1/M2 — hold repeat + retrig (committed 6562dbd)

- Facts: SDL gamepad buttons never auto-repeat; the only repeat path was
  SDL keyboard repeat (desktop only). Holding X/Y on the pad gave ONE
  value change and nothing — the demanded retrigs were impossible by
  omission.
- ui_t gains val_held_dir / val_next_repeat; val_edit() (ui.c) applies
  the value change, fires the TRIGGER path (mailbox trigger_req + flash)
  on every APPLIED change, and arms the hold (400 ms delay, 120 ms
  rate); ui_tick(ui, now) synthesizes the repeat until release;
  ui_handle_event ends it on release. ui_tick takes the time explicitly
  (deterministic tests). Menu-internal VAL handling (name grid) never
  retrigs (the retrig lives in the non-menu branch only).
- RED: test_ui_menu test_valkey_hold_retrig — retrig on press, nothing
  before the first repeat, repeat fires and re-arms, DEC symmetric.
  Two test-side corrections during green: the item is inert on the
  sub-tab row until focus (ACT_SEL_DOWN first), and the second repeat
  ticks at t0+525 (re-arm is +120 after the +400 first repeat).
- GREEN: suite 98895/98895 → 98911/98911 with later additions; desktop
  smoke XRUNS 0; handheld gate OK. PLAN.md TRIGGER bullet documents the
  owner order.

## M3 — self-audit fix: the desktop runaway (release never cleared)

- Fresh-eyes audit of my own retrig commit found a CRITICAL bug: the
  hold was cleared ONLY on controller button-up. SDL KEYUP was never
  handled anywhere in the UI, so on desktop a Z/X hold armed
  val_held_dir and NOTHING cleared it — after release, ui_tick kept
  firing value change + retrig every 120 ms FOREVER (runaway). A pad
  hold across focus loss or controller disconnect would stick too.
- RED: test_ui_menu pins — KEYUP of the value key clears the hold;
  non-value KEYUP does not; WINDOWEVENT_FOCUS_LOST clears;
  CONTROLLERDEVICEREMOVED clears. keymap_events pins (desktop + R36S):
  keymap_val_release — KEYUP SDLK_x/z → +1/−1, other keys → 0, desktop
  cbutton UP → 0 (F6 semantics), handheld cbutton(2)/(3) UP → +1/−1.
- FIX: keymap.h/.c `keymap_val_release(e)` (+1/−1/0; KB_LABELS_ARE_
  BUTTONS macro hoisted above it — single definition); ui_handle_event:
  KEYUP/cbutton-UP → clear via the helper; FOCUS_LOST and
  CONTROLLERDEVICEREMOVED → clear. test_ui_menu release pins now use
  the desktop-valid KEYUP path (the cbutton-UP pin lives in the R36S
  keymap test, where that path is real).
- Environment note surfaced by ASan: the uncreatable-dir test chmods
  the stage's parent — impossible for exotic exe locations (ASan binary
  in /tmp); the block now self-skips with a printed notice when the
  parent cannot be locked (the normal build/tests location always can).
- Verification: suite 98910/98910; ASan+UBSan test_ui_menu clean
  (82/82 with the environment skip); desktop relink + capped smoke
  XRUNS 0; handheld gate OK; zip rebuilt.
- Observed, NOT fixed (pre-existing, outside this change): schema.c
  parse does not clamp sub amount per dest (UI ladder clamps ±50 for
  PITCH, ±100 otherwise; a hand-written preset could hold out-of-ladder
  amounts — with the vibrato fix that means up to ±100 Hz depth).
