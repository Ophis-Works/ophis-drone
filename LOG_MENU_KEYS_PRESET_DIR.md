# LOG_MENU_KEYS_PRESET_DIR.md — implementation log (changes + rationale).
# Plan: PLAN_MENU_KEYS_PRESET_DIR.md. Owner decisions 2026-11-05:
# presets dir auto-created, LOAD falls back to parent like a file picker,
# desktop Enter = menu select, Esc = only menu cancel, T = TRIGGER,
# Space = RUN/STOP outside menus only. R36S unchanged. No subagents.

## P1 — presets dir auto-create + parent fallback

- RED 1: test_preset.c §6b — preset_ensure_dir contract (nested create,
  EEXIST rerun, -1 on NULL/empty/1200-char path, -1 when a FILE blocks a
  prefix). Compile fail = red.
- preset.c: preset_ensure_dir(dir) extracted from preset_save's mkdir -p
  walk (identical semantics, errno/EEXIST tolerated, 0777); preset_save
  now calls it. preset.h: declared + documented.
- GREEN 1: test_preset 179/179.
- RED 2: test_ui_menu.c — replaced the old "missing dir → load_count 0"
  empty-LOAD test (unreachable once auto-create exists) with three
  file-picker pins: existing-but-empty dir lists exactly ".." (count 1);
  a missing dir is recreated by the LOAD open (stat-pinned, load_err
  empty); an uncreatable dir (parent chmod 0555) opens the PARENT
  instead. purge_dir() helper (opendir/unlink/rmdir) extracted; tests
  allocate nothing (static/fixed stack buffers only).
- ui_menu.c: LOAD-open calls preset_ensure_dir(load_dir) (failure ignored
  — fallback covers); load_refresh falls back to the parent once via
  preset_path(load_dir, "..") (clamped at "/" = no parent), retry fails
  -> CANNOT OPEN DIR as before. Also fixed the latent stale-n bug: the
  files pass now resets n = 0 unconditionally (previously a preset_list
  failure with no error set re-copied the dir list as files).
- Test-order bug caught by red: chmod(parent,0555) BEFORE rmdir(stage)
  makes the rmdir fail (needs parent write) — swapped to rmdir-then-
  chmod.
- GREEN 2: test_ui_menu 64/64, full suite 97447/97447, valgrind clean
  (leak-check=full, 64/64).

## P2 — desktop keys

- RED: test_keymap.c (KB_TRIGGER==OPHIS_KEY_T, KB_CONFIRM==
  OPHIS_KEY_RETURN, flags 0/0, id sanity 0..11, labels "T"/"Return");
  test_keymap_events.c (SDLK_t->ACT_TRIGGER, SDLK_RETURN->ACT_CONFIRM,
  repeat gate covers CONFIRM); test_keymap_r36s.c (KB_CONFIRM==-1,
  both flags 1); test_ui_menu.c — confirms via ACT_CONFIRM (open_item,
  NEW/CONFIRM dialogs, SAVE, LOAD), explicit inert pins: Space confirms
  nothing in MAIN/CONFIRM/NAME/LOAD, T cancels nothing, Esc closes and
  reopens. Compile fail = red.
- keymap.h: OPHIS_KEY_T (before OPHIS_KEY_ESCAPE, ids now 0..11);
  ACT_CONFIRM appended.
- config.h: desktop KB_TRIGGER = OPHIS_KEY_T, KB_CONFIRM =
  OPHIS_KEY_RETURN, flags KB_RUN_CONFIRMS_MENU=0 /
  KB_TRIGGER_CANCELS_MENU=0; R36S KB_CONFIRM = -1 (dead case label, A
  keeps RUN/confirm via flag), flags 1/1. THE only target difference,
  per PLAN §9.
- keymap.c (byte-identical): key_to_sdl gains T->SDLK_t; map_code gains
  `case KB_CONFIRM: ACT_CONFIRM`.
- ui_menu.c: includes ../config.h; every menu branch: ACT_CONFIRM
  applies, ACT_RUN applies only when KB_RUN_CONFIRMS_MENU, default-
  cancel closes on ACT_MENU always and on ACT_TRIGGER only when
  KB_TRIGGER_CANCELS_MENU; LOAD-empty close set mirrors that. YES/NO
  apply extracted into confirm_dialog_apply() (no duplication).
- ui.c: ACT_CONFIRM outside menus = no-op case (-Wswitch).
- main.c U2 gate: during name entry ESC and ENTER keydowns pass
  (Enter saves the name; previously NO keyboard key could save on
  desktop — Space was gated off, so this also fixes that latent
  dead-end).
- ui_draw.c SAVE hint: save label = keymap_label(KB_CONFIRM) with RUN
  fallback (handheld shows A).
- GREEN: full suite 97467/97467, no warnings.

## P3 — docs + full verification

- README.md: key table gains CONFIRM row, TRIGGER row → T; menu
  paragraph (Enter confirms, Esc-only cancel, Space/T inert in menus);
  SAVE line (Enter saves).
- PLAN.md §8.2/§8.4/§9: rebind paragraph + menu-window context remap +
  LOAD (auto-create + parent fallback) + desktop binding line, each
  marked owner-ordered 2026-11-05.
- IMPLEMENTATION.md: ui_action_t enum + ACT_CONFIRM; KB_ list gains
  KB_CONFIRM / KB_RUN_CONFIRMS_MENU / KB_TRIGGER_CANCELS_MENU; context
  remap bullet; M4 smoke line T/Space.
- docs/manual.html §2.3/§2.4: menu select/cancel rules, SAVE line, key
  table (+CONFIRM row, TRIGGER T), LOAD folder-creation sentence.
- PLAN_KEYMAP_REBIND.md: dated addendum — desktop rows superseded,
  R36S rows unchanged, mechanism named.
- Verification: suite 97467/97467; valgrind (leak-check=full) clean on
  test_ui_menu 71/71; forced clean desktop rebuild (binary mtime was
  ahead of sources from the interrupted run — rm + make) + capped smoke
  XRUNS 0; make handheld gate OK, zip 12 files. Memory notes: every new
  buffer is fixed-size stack/static (no malloc); per user order after
  two host crashes, steps ran individually, valgrind not re-run, smoke
  capped with timeout 30; host showed 24 GB RAM free / swap 505/511 MB
  used at run time.
