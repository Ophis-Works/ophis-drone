# LOG_SEQ_MARKER_PERCHAIN.md — implementation log.
# Plan: PLAN_MOD_REVAMP_SEQ_MARKER_RETRIG.md (item 1).

## M1 — per-chain marker

- RED→GREEN: new test_engine_seq block runs ALL THREE sequencers at
  once (modes random/markov/random, distinct) and pins per-chain
  engine_seq_steps: each chain's window moves AND matches its own
  sq[c].prev per block (2023/2023 green on the first run). Verdict: the
  engine, status publish, mailbox and UI draw path are all per-chain
  correct — the device report of "OSC 1 only" matches the pre-B-SEQ1
  zip (frozen marker) rather than the current code; the per-chain pins
  now guarantee it cannot regress.
- UI (owner: "UI should design what is currently selected"): the SEQ
  page draws a gray "SEQUENCER - OSC n" header above the rows, naming
  the chain being edited (ui_draw.c SEQ branch; visual only, not a
  cursor item).
- Verification: suite 99574/99574; desktop relink. Smoke/handheld with
  the M2 commit (shared verification pass).
