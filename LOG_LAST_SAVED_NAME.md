# LOG_LAST_SAVED_NAME.md — implementation log. Plan: PLAN_LAST_SAVED_NAME.md.

## 2026-11-10 — prefill SAVE with the last successful save's name

- ui_t gains last_saved_name[33] (ui.h); ui_init memsets it, so a fresh
  session starts with no record.
- menu_confirm SAVE case: prefill name/name_len from the record
  (snprintf copy, grid cursor stays 0,0 — it picks the NEXT character;
  the text starts fully typed).
- name_save: on SUCCESS ONLY, snprintf the saved name into the record.
  Failed saves (EMPTY NAME, I/O error) never change it.
- RED: test_save_prefills_last_name — 1. fresh session + failed save ->
  blank reopen; 2. save "abc" -> reopen shows "abc" typed (len 3);
  3. save "dude" -> reopen shows "dude". Two test-side corrections on
  the way: the record lives in ui_t so reopens stay on ONE ui instance
  (the fresh_ui-per-scenario helper wipes it — save_reopen added), and
  step 2 must reopen SAVE before confirming (confirming from MENU_OFF
  is inert, so the first attempt passed the OFF check vacuously).
- GREEN: 11341/11341; handheld gates PASS (R36S + RG28XX, GLIBC_2.17);
  desktop smoke XRUNS 0.
