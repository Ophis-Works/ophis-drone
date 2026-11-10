# PLAN_LAST_SAVED_NAME.md — prefill SAVE with the last saved name

## DESIGN PLAN

### Requirement
Owner order 2026-11-10: opening SAVE (MENU -> SAVE) must prefill the
name entry with the last successfully saved name, if any.

### Current behaviour
menu_confirm case 1 (src/ui/ui_menu.c:139-146) always clears the name
buffer: ui->name_len = 0, ui->name[0] = '\0'. Every save starts blank.

### New behaviour
- ui_t gains `char last_saved_name[33];` (same capacity as the SAVE name
  buffer). "Any" = `last_saved_name[0] != '\0'`.
- On SAVE open: if a last saved name exists, copy it into ui->name,
  set ui->name_len, clear save_err, and leave the char-grid cursor at
  0,0 (grid position is for picking the NEXT character; the text starts
  fully typed). If none exists, the buffer stays empty (unchanged).
- On successful name_save: copy the saved name into last_saved_name.
- Failed saves (EMPTY NAME, I/O error) do NOT update last_saved_name.
- The name survives across LOAD/NEW within the session (it is a name
  memory, not tied to the draft); resets only on app restart.

### UI/UX description
MENU -> SAVE: with a previous save, the entry shows the last saved name
fully typed; CONFIRM (Enter / A / RUN) immediately overwrites that file
(same-name overwrite is the preset_save contract). Editing before
confirm works exactly as today: grid picks append, backspace deletes,
desktop TEXTINPUT inserts. First-ever save (no previous save this
session) behaves exactly as today (blank entry, EMPTY NAME guard).

### System requirements check
No new dependencies; char capacity 33 already used by ui.h name[33].
Nothing missing.

## IMPLEMENTATION PLAN

### Milestone 1 — tests first (red)
- tests/test_ui_menu.c: three checks —
  1. fresh session: SAVE open leaves the name empty (regression pin);
  2. after a successful save of "abc", SAVE open prefills name == "abc"
     and name_len == 3;
  3. a failed save (empty name error) does not clobber the recorded
     name; the next SAVE open still prefills the last successful one.
- Run: new checks red (prefill absent), suite otherwise green.

### Milestone 2 — implementation (green)
- src/ui/ui_internal.h: add `char last_saved_name[33];` to ui_t.
- src/ui/ui_menu.c: prefill in menu_confirm case 1; record in
  name_save on success (only).
- All suite green (make test).

### Milestone 3 — commit
- Commit code + tests + this plan/log on main after verification.
