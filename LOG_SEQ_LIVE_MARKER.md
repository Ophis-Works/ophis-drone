# LOG_SEQ_LIVE_MARKER.md — implementation log. Plan: PLAN_SEQ_LIVE_MARKER.md.

## M1 — engine live-step window (TDD)

- Root cause (confirmed, not guessed): main.c:65-66 published
  `eng.sq[c].cursor` as the live step; `cursor` is the fwd/pingpong/
  oddeven ORDER cursor — next_index() never advances it in random (3) /
  markov (4) (their emitted step lives in sq.prev), so the UI marker
  froze at step 0 in those modes. The UI (ui_draw.c `st->step[ci] == q`)
  was already mode-agnostic — the feeder was not.
- RED: test_engine_seq.c new block — per mode 0..4, 200 rendered blocks
  (fixed win[256]/prv[256], no allocation): range pin, win[0] >= 0, fwd
  predicted order (windows at 43-block boundaries), random/markov NOT
  frozen + per-block window == sq.prev (test-side internal read only),
  seq off -> -1 (all chains), PANIC -> -1. Compile fail = red.
- GREEN: engine_seq_steps(const engine_t*, int out[3]) added
  (engine.h/.c): out = cur_valid ? cur_step : -1 — the engine's step
  WINDOW, mode-independent by construction. PANIC already clears
  cur_valid (verified engine_panic_reset).
- Test-side bug caught by red→debug: the equality pin first compared
  every block against the FINAL sq.prev (259 fails); fixed to a
  per-block prv[] snapshot. Scratch harnesses in /tmp (deleted) proved
  the engine per-block invariant before the test fix.

## M2 — status wiring + verification

- mailbox.h: ui_status_t.seq_cursor REMOVED (published from sq.cursor,
  consumed by nothing in src/) — the status no longer exposes order
  internals, so the UI cannot depend on the mode. step[] documented as
  the live window (-1 = nothing plays). test_mailbox.c: tag carrier
  moved to step[0]; fill_status/status_valid updated.
- main.c: audio thread publishes engine_seq_steps(&eng, st.step); the
  sq.cursor publishes are gone.
- Verification: full suite 98881/98881; ASan+UBSan on test_engine_seq
  1420/1420 clean; desktop relink + capped smoke (timeout 30)
  XRUNS 0; make handheld gate OK. No valgrind (host memory order);
  no new allocations anywhere (int[3] / fixed test arrays only).
