# PLAN_VALKEY_NO_RETRIG.md — value keys never trigger (bug correction)

## DESIGN PLAN

### Requirement
Owner order 2026-11-10: ONLY the TRIGGER key triggers. The value keys
(X/Y, pad and desktop) change values and nothing else — no retrig, no
TRIG flash, no mailbox writes. This supersedes the PLAN_VALKEY_RETRIG
"owner order 2026-11-05" attribution in LOG_VALKEY_RETRIG.md, which the
owner disowns: X/Y were never to trigger anything.

### Current behaviour (the bug)
val_edit (src/ui/valrepeat.c) on every applied value change:
  atomic_fetch_or(&ui->mb->retrig_req, 1 << chain_of(ui));
  ui->trig_flash_until = now + 300;
So pressing/holding X or Y retriggers the edited chain and flashes TRIG.

### New behaviour (inclusion principle)
The only code path that triggers is ACT_TRIGGER (ui.c: keymap ACT_
TRIGGER -> trigger_req + flash). val_edit applies the value, arms the
hold, and does NOTHING else. No other UI action triggers anything.

### UI/UX description
X/Y (and synthesized hold repeats) sweep values silently. TRIGGER key
(B / T) flashes TRIG and retriggers all chains as before. The hold
repeat keeps its 400 ms / 120 ms timing for value changes only.

### System requirements check
No new dependencies, no new state. Nothing missing.

## IMPLEMENTATION PLAN

### Milestone 1 — tests first (red)
- tests/test_ui_menu.c: rewrite test_valkey_hold_retrig into
  test_valkey_no_retrig — press, repeat and DEC pin:
  value applied (edit visible), retrig_req == 0, trigger_req == 0,
  trig_flash_until == 0 at every step; ACT_TRIGGER still sets
  trigger_req + flash (the single allowed path).
- Run: new assertions red against current val_edit.

### Milestone 2 — implementation (green)
- src/ui/valrepeat.c: delete the retrig_req write and the flash line
  from val_edit (the two lines above).
- All suite green (make test); desktop smoke; both handheld gates.

### Milestone 3 — docs + commit
- LOG_VALKEY_RETRIG.md: owner retraction note (2026-11-10) — the
  2026-11-05 retrig demand is disowned; trigger is exclusive to the
  TRIGGER key.
- Commit on main.
