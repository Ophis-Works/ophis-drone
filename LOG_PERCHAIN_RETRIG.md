# LOG_PERCHAIN_RETRIG.md — implementation log.
# Plan: PLAN_MOD_REVAMP_SEQ_MARKER_RETRIG.md (item 3).

## M2 — per-chain value-edit retrig

- Owner order 2026-11-06: "when retriggering buttons to increase/decrease
  we should not retrigger the whole synth, we are just changing that
  specific parameter." The M-valkey implementation fired
  mailbox trigger_req = ALL chains on every value change — wrong scope.
- mailbox.h: `_Atomic int retrig_req` (bit c = chain c; UI ORs; host
  fetch-exchanges). main.c: fetch per block, passed to the engine.
- engine.h/.c: engine_render_block gains `retrig_mask` — bit c set ->
  chain c retrigs at its own chain vel (direct-mode semantics); other
  chains untouched. trigger_req (the TRIGGER key) unchanged = all chains.
- ui.c val_edit: on the OSC tabs, ORs `1 << chain_of(ui)` (the edited
  parameter's chain); MASTER items have no chain -> no retrig; the
  whole-synth TRIGGER stays reserved for the TRIGGER key.
- Pins: test_ui_menu valkey test flipped — retrig_req bit 0 on chain A
  edits, bit 1 on OSC 2 edits, trigger_req stays 0 throughout, release
  clears; test_engine_seq — mask 0b010: chain B envelope rises, A/C stay
  0. All call sites of engine_render_block updated (signature +1 arg).
- Verification: suite 99582/99582; desktop relink + capped smoke XRUNS 0;
  handheld gate OK; zip rebuilt.
