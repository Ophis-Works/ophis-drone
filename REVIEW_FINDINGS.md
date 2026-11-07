# Code-review findings — ophis-drone (2026-11-03)

Full findings list backing PLAN_CODE_REVIEW_FIXES.md. Tool sweep: cppcheck,
clang-tidy 21, scan-build 21, gcc -fanalyzer, flawfinder, iwyu, valgrind (all test
binaries clean) + manual read. Remediation mapping in PLAN_CODE_REVIEW_FIXES.md.

## Host bugs
- **H1** src/audio/mailbox.c:16-36 — parameter mailbox `version` is 1 bit; two UI
  commits between reader loads flip version back to v0 → torn snapshot accepted
  (v0==v1). Key-repeat makes it plausible. Fix: u64 seqlock (as the status channel
  already does).
- **H2** src/app/main.c:200 — `mailbox_status_read()` return ignored; torn
  ui_status_t can be drawn. Contract documented in mailbox.h:51-53.
- **H3** src/audio/engine.c:60 — `e->p.run = 0` on PANIC is overwritten next block
  from the mailbox (UI draft still run=1) → drone resumes ~5.8 ms after PANIC;
  comment ("mirrored to UI status next frame") is wrong.

## Sound-architecture bugs
- **S1** src/dsp/chain.c:112-129 + src/dsp/filter.c:47 — with envamt != 0, fc
  changes per sample → `biquad_set()` (which zeroes z1/z2) runs ~per sample →
  filter history wiped per sample (ENV→cutoff barely filters, resonance vanishes),
  plus cos/sin per sample.
- **S2** src/dsp/chain.c:116 — `pow(40, pos/50)` recomputed per sample per chain;
  depends only on the integer `res` knob.
- **S3** src/audio/engine.c:72-76 — `seq_reset()` on run rise / seq off→on leaves
  `cur_valid[c]=1` with the stale `cur_step[c]` → old pitch held until the first
  new boundary.

## UI bugs
- **U1** src/ui/keymap.c:44 + src/ui/ui.c:1005-1012 — `keymap_label()` returns ONE
  static buffer; `hint_binding` calls it 4× in one snprintf → on R36S (all four
  KB_* are buttons) every %s is the same pointer → hint reads "R/R:TABS … Y/Y:EDIT".
  Desktop unaffected.
- **U2** src/ui/ui.c:801-814 + src/app/main.c:186 — during MENU_NAME SDL text input
  is active but keymap_action still maps keydowns: z/x insert grid chars (double
  input), Space (KB_RUN) saves mid-name, q/e/Enter/Esc close/cancel.
- **U3** src/ui/ui.c:1293 — help line "ENTER:SAVE" stale (Enter = TRIGGER now;
  save is Space / A).
- **U4** src/ui/ui.c:776-838 — unhandled actions in menus (incl. ACT_NAV_LEFT/
  RIGHT, ACT_VAL_*) hit `default: menu_close` → d-pad left/right on R36S closes
  menu windows; inconsistent with d-pad up/down navigating.
- **U5** src/ui/ui.c:303-325 et al. — `d->seq[ci]` / `ui->seq_sel[ci]` with
  `ci = chain_of(ui)` (0..4) rely on the implicit invariant "SEQ item ids only on
  OSC tabs" (clang-tidy security.ArrayBound). Clamp ci in chain_of().

## Duplication
- **D1** PRNG block duplicated verbatim: src/dsp/osc.c:13-37 ≡ src/seq/sequencer.c:14-38.
- **D2** ui.c two parallel ~30-case switches (item_value / edit_item) + ~12
  copy-pasted percent clamps → item spec table.
- **D3** clamp ×3: chain.c:32, fx.c:126, inline in ui.c.
- **D4** enum display names in two places: json.c ent_t tables vs ui.c:216-224
  (case drift "12db" vs "12DB").
- **D5** SAVE grid charset literal twice in ui.c (menu_action + renderer).
- **D6** adsr_trigger duplicates ATTACK-entry code of adsr_next (adsr.c:20-26 vs 31-35).

## Dead code
- **D7** biquad_reset (filter.c:50) — zero callers.
- **D8** keymap_sdl_code (keymap.c:28) — zero callers in src and tests since rebind.
- **D9** ui_tick (ui.c:615) — no-op; header promises flash retirement.
- **D10** engine.c:60 `e->p.run = 0` dead write (see H3).
- **D11** ui_t.cursor[15] — only 11 slots reachable (ui.h:38).

## Bad practices
- **P1** json.c:727 `key[5] == 'l'` re-derives what the strcmp decided (brittle;
  scan-build flags garbage value).
- **P2** keymap.c:52 `s[i] != '\0' && i < sizeof buf - 1` — index before limit
  check (cppcheck arrayIndexThenCheck).
- **P3** main.c:179 `atoi(getenv(...))` — no validation; use strtol.
- **P4** preset.c:141 `static char buf[65536]` — 64 KB BSS, non-reentrant.
- **P5** json.c parse_string silently truncates over-long keys into char[16] →
  "time_lxxxx" aliases "time_l".

## Overly complex
- **X1** ui.c draw_text — one SDL_RenderFillRect per lit pixel; R36S frame-budget
  risk; glyph atlas deferred until measured.
- **X2** preset_path (preset.c:206-248) — component-stack normalizer heavier than
  the dir-walk invariant requires.
- **X3** ui.c 1,330 lines mixing state machine, editing, dialogs, rendering —
  split deferred.
- **X4** json.c (1,100 lines hand-rolled parser+writer) → replaced by vendored
  sheredom/json.h (public domain) + explicit schema layer (Phase 7).
