# ophis-drone — Drone Synth Plan (BINDING)

Per AGENTS.md: this plan is binding. No plan-related decisions during
implementation — not by the lead, not by a subagent. If a blocker forces a
change, implementation stops and the change comes back for approval.
Implementation is done exclusively by subagents, ONE AT A TIME.
All implementation is TDD: the milestone's tests are written and confirmed
failing before any implementation code exists.

## 1. Verified platform facts (researched 2026-11-01)

| | Desktop | R36S (handheld) |
|---|---|---|
| SoC/CPU | x86-64 Linux | Rockchip RK3326, 4× Cortex-A35 (aarch64) |
| OS | user's distro | ArkOS — Ubuntu-based 64-bit Linux userspace (github.com/christianhaitian/arkos) |
| RAM | n/a | 1 GB DDR3L |
| Screen | windowed 640×480 | 3.5" IPS 640×480 (4:3) |
| Audio | ALSA (mandatory) | ALSA (mandatory), mono speaker + 3.5mm jack |
| Network | n/a | none (no WiFi/BT per handhelds.wiki) → deploy via SD card or USB OTG; SSH/Samba optional in ArkOS "Enable Remote Services" |

Consequences: one C codebase, two builds. Cross-compile for aarch64
(aarch64-linux-gnu toolchain + static or container-built libs) OR build on an
aarch64 environment. **glibc risk**: ArkOS lineage is Ubuntu 19.10 (glibc
2.28) — static linking of SDL2/ALSA (or a build inside a matching container)
is decided in M0, not later.

## 2. Scope and hard requirements

- **This is a drone synth. There are no musical notes, no semitones, no
  scales anywhere.** Oscillator parameters are frequencies in Hz; sequencer
  tones are frequencies in Hz.
- 3 chains (A, B, C). Each chain: 1 oscillator + 2 sub-oscillators
  (modulate the main osc in various ways) + ADSR + filter (mode-selectable
  HP/LP/BP/BR) + level + velocity. Chain velocity is the retrigger level
  used by TRIGGER and by direct mode when RUN is ON.
- Mix: A is the carrier; B and C both sum into the master AND can modulate A
  (cross-mod matrix).
- 3 sequencers, one per chain, each sends TONE (frequency, Hz) + VELOCITY,
  each can be ON or OFF. The ON/OFF state is a **UI parameter** (edited in
  the UI), never a button. Sequencers act only while RUN is ON.
- TRIGGER button: fires the 3 chains once (one-shot retrigger), never the
  sequencers. TRIGGER **overrides RUN OFF**: it opens the master gate for
  one envelope and the gate closes again once the envelopes have released.
- RUN (on/off) button: when ON, each chain is triggered either via its
  sequencer (if that sequencer is ON) or directly (if OFF), using the
  frequency parametrized in the chain's oscillator. When OFF (and no TRIGGER
  pending), output is gated silent.
- Presets: all synth parameters (everything in `synth_params_t` except the
  RUN transport flag) are savable/loadable as JSON files.
- Menu: an overlay window, opened via the menu key (Start / Esc) on ANY
  screen. Items: NEW, SAVE, LOAD, PANIC, EXIT, BACK.
  - NEW: confirm prompt; on confirm, discard everything and set factory
    defaults.
  - SAVE: prompt for a preset name; writes a JSON preset file.
  - LOAD: explorer listing all preset files; selecting one loads all
    parameters from it.
  - PANIC: immediately stop/reset all audio processing (gate closed with
    no ramp, all DSP state reset, RUN cleared, sequencers stopped).
  - EXIT: close audio, quit the app. Exit is menu-only — no dedicated quit
    key on either target.
  - BACK: close the menu, return to the screen the window was opened from.
- **All oscillators — main and both sub-oscillators — support exactly:
  sine, tri, saw, square, white noise, pink noise.**
- **Threading: the host thread (engine + audio) and the UI thread are
  separate and communicate WITHOUT LOCKS** — lock-free channels only
  (detailed in §4). No mutex, no condition variable between the two
  threads.
- **Desktop verification: measured ALSA xruns must be exactly 0.** If the
  synth xruns on desktop, the implementation is incorrect — it is a bug to
  fix, not a result to report.
- Audio backend: **ALSA only** (mandatory). No other audio paths.
- FX: **delay then reverb, in the master path after the mixer and before
  the master stage**: chains -> mixer (sum + tanh + cross-mod) -> delay ->
  reverb -> SAFETY FILTER -> master level -> master gate.
  **AMENDED (owner order, contract change): the master gate MOVES OUT of
  `mixer_render` into the FX/master stage.** The mixer now ends at
  `tanh(1.5 × (A+B+C))`; gate application happens after the FX/safety/master
  level, per-sample, in the FX/master stage (PLAN §5).
- Safety filter: master boolean (default **ON**). ON = 4th-order rolloff
  via cascaded biquads — HP at 60 Hz + LP at 18 kHz. OFF = bypass (dry).
- UI: **SDL2**, identical UI code on both targets. UI is laid out at a fixed
  640×480 logical resolution and rendered scaled by one UI SCALE constant
  (R36S = 1, desktop default = 2 → 1280×960 window).
- Same code on both targets **except**: the key bindings and the UI scale
  constant, which live in one `config.h` block selected by one build macro
  (see §9). No other target-specific code, no `#ifdef` anywhere else.

## 3. Tech stack

- C23, single binary, no runtime. Deps: SDL2, ALSA (libasound2). Nothing
  else.
- Build: Makefile. `make` (desktop native) passes `-DTARGET_DESKTOP`;
  `make handheld` (aarch64 cross) passes `-DTARGET_R36S`. That macro is read
  in exactly one place: `src/config.h`. Every other source file is
  byte-identical between the two builds.
- Tests: plain-C assert harness (`tests/harness.h`), `make test`. No
  frameworks, no network, no SDL/ALSA needed for unit tests (all DSP is pure).

## 4. Architecture

Single process, two threads:

1. **Host thread (engine + audio, real-time)**: owns ALSA PCM. 44100 Hz,
   float32, stereo, 256-frame period. Renders the full synth graph + runs
   sequencer scheduling + transport state machine. No malloc/printf/IO in
   the RT path.
2. **UI thread**: SDL2 window sized 640×UI_SCALE × 480×UI_SCALE (fullscreen
   on handheld, `KB_FULLSCREEN` flag from config.h), event loop, draws
   state, writes parameter changes.

Comms — **host and UI communicate without locks** (binding rule; no mutex
or condition variable between the threads). Three lock-free channels only:
1. **Parameter mailbox** — UI writes a `synth_params_t` snapshot into one
   half of a double buffer, then publishes it with an atomic version bump.
   Host copies the published snapshot at the start of each render block.
2. **Event flags** — TRIGGER / PANIC / RUN are single atomic flags the UI
   sets; host consumes (fetch-exchange) them at a sample/block boundary.
3. **Status snapshot** — host publishes a small immutable struct (gate,
   env levels, step cursor, RUN state) via an atomic pointer swap (two
   preallocated slots); UI reads the current pointer. No allocation in
   steady state on either side.

Sequencer→chain frequency events are internal to the host thread
(sample-accurate).

Module map:

```
src/
  dsp/osc.h/.c        oscillator: sine, tri, saw, square, white noise,
                      pink noise (PRNG + pink-state live in osc state)
  dsp/subosc.h/.c     sub-osc: freq (free Hz or ratio-to-main), waveform,
                      destination {OFF, PITCH_deteune_Hz, AMPLITUDE,
                      FILTER_cutfreq_ratio}, amount
  dsp/adsr.h/.c       sample-accurate A/D/S/R state machine
  dsp/filter.h/.c     RBJ biquad, mode {LP,HP,BP,BR}, fc 20 Hz–20 kHz
  dsp/chain.h/.c      chain graph: suboscs -> main osc -> AM -> ADSR->gain -> biquad
  dsp/mixer.h/.c      master sum, cross-mod matrix (B,C -> A), tanh soft-clip
                      (AMENDED: gate + master level moved out — see §5)
  dsp/fx.h/.c         master-path FX: stereo delay (2 s lines), reverb
                      (FDN/Schroeder class), safety filter (HP 60 Hz + LP
                      18 kHz cascaded biquads), master level, master gate
                      (10 ms ramp)
  seq/sequencer.h/.c  pattern, tempo, per-seq enabled flag (UI param),
                      sample-accurate clock
  seq/transport.h/.c  RUN/TRIGGER/PANIC state machine incl. one-shot gate
  audio/alsa_pcm.h/.c open/start/stop + render callback (thin wrapper)
  audio/engine.h/.c   audio thread: mailbox swap, render loop, seq+transport run
  audio/mailbox.h/.c  double-buffered synth_params_t handoff (UI -> audio)
  preset/defaults.c   factory defaults (synth_params_t constant)
  preset/json.c       minimal JSON writer/parser for the preset format
  preset/preset.c     preset file I/O: list, save (name sanitize), load
  ui/ui.h/.c          SDL2 UI: tabs + sub-tabs (incl. menu + preset windows),
                      drawing,
                      built-in 8x8 bitmap font
  ui/keymap.h         dispatch table: ui_action_t -> binding values
  app/main.c          init, threads, shutdown
  config.h            THE ONLY TARGET DIFFERENCE: one #if block with the
                      keymap binding values + UI_SCALE (see §9)
tests/                harness.h + per-module tests (see §10)
```

## 5. DSP specification

Per chain, render order per sample:

1. Main osc frequency: `f = f_osc + Δf` (Hz). `f_osc` = the chain
   oscillator's frequency parameter (Hz, set in the UI); while the chain's
   sequencer is ON and has sent a step, `f_osc` = that step's tone.
   `Δf` = Σ (own sub-osc PITCH detune + incoming cross-mod PITCH detune),
   in Hz, clamped so f stays within 0.1 Hz–20 kHz.
2. Main osc waveform → `x` (−1..1).
3. Amplitude mod: `x ×= (1 + amt×m)`, m = normalized sub/cross source
   (−1..1).
4. ADSR env (0..1) × velocity (0..1) → gain.
5. Filter: biquad, mode LP/HP/BP/BR, `fc` = base_cfcut × (1 + amt×m),
   clamped 30 Hz–18 kHz. Q from the RES parameter: 0–100% → 0.5–20, log
   (pos 0..50, Q = 0.5 × 40^(pos/50)). ENV AMT (−100..+100%): the chain's
   own ADSR also drives cutoff — `fc` is additionally scaled by
   `(1 + envamt/100 × env)`, clamped 30 Hz–18 kHz. POLES: 12 dB/oct
   (one biquad) or 24 dB/oct (two identical cascaded biquads); the cascade
   shares one cutoff/Q.
6. Chain level (0..1) → master bus.

**Master path (AMENDED, owner order — signal chain after the chains):**

```
chains -> mixer (sum + tanh + cross-mod)
       -> FX: delay, then reverb (stereo)
       -> SAFETY FILTER (master boolean, default ON:
          4th-order rolloff via cascaded biquads — HP 60 Hz + LP 18 kHz;
          OFF = bypass)
       -> master level
       -> master gate (10 ms ramp)
```

**AMENDED contract:** the master gate is no longer applied inside
`mixer_render`. The mixer output is
`master = tanh(1.5 × Σ (mixvol[c]/100 × out[c]))` —
gate and master level moved to the FX/master stage, applied per sample
after the safety filter.

**Waveforms** (identical set for main osc and both sub-oscillators):
sine, tri, saw, square, white noise, pink noise — all normalized to −1..1.
Tonal waves are phase-accumulated (`phase += f/sr`). **SQUARE carries a
DUTY parameter** (pulse width, integer percent 5..95, default 50); duty
applies per oscillator (main and subs) and only to the square wave. White noise =
per-instance deterministic PRNG (xoshiro256**, fixed seed per instance so
tests are reproducible), uniform −1..1. Pink noise = small filtered-state
algorithm (Paul Kellet / Voss–McCartney class), state in osc state. For
noise waves the frequency parameter is ignored (samples advance per render
sample; the UI dims the freq field). PRNG + pink state are part of osc
state (resettable by PANIC).

Sub-osc (per chain, ×2): parameters = freq (free 0.1 Hz–20 kHz OR ratio
0.0625–4.0 × main), waveform (same set as main, incl. DUTY for square),
destination {OFF, PITCH (VIBRATO — depth Hz; the sub FREQ is the rate
in EVERY fmode, owner order 2026-11-05), PHASE (PM), AMPLITUDE, FILTER
(ratio of cutoff), RING (ring mod — AMT 0..100 dry/wet; negative AMT
inverts the modulator polarity, owner order 2026-11-06), FM (operator
FM — the audio-rate source drives the carrier phase with index
AMT/25 cycles, owner order 2026-11-06; the stage-3 MIX dest is GONE
(PLAN_MOD_ROUTING.md M4b/D4 "new format only" — the per-slot mix knob
is the mixing ADD function; presets are rewritten so nothing carries
dest "mix")}, amount (signed). Each cross-mod slot carries a TARGET
(0..2, preset key "tgt", default A — PLAN_MOD_ROUTING.md M1): the
slot's modulation and its ADD knob feed that chain, applied at the
target's OSC stage (before its ADSR/filter), sources read one sample
late; sources extend to chain A (ids 7..9, B/C ids unchanged — M2);
self-routing is legal (D3). CHFM is REMOVED (owner order
2026-11-06, §4 of PLAN_MOD_ROUTING.md): FM mode lives on in the
matrix as TYPE = FM. **PHASE (PM)**: the modulator output
adds to the carrier's phase; amount −100..+100% maps linearly to ±180°
(±π rad) of phase deviation. Sub-oscillators are Spice/Subharmonicon
AUDIO voices (PLAN_OPTIMIZATION.md O2, owner order 2026-11-07):
parent_f / div (integer divider 1..16), own wave (sin/tri/saw/square) +
duty + LEVEL (0..100) summing them into the chain's OSC output before
the envelope/filter; they have NO dest knobs — sub modulation goes
through the matrix (SRC = x.sub1/x.sub2, level-independent taps).

Mixer: `master = tanh(1.5 × Σ (mixvol[c]/100 × out[c]))` (MIX VOLUME =
per-chain master send, PLAN_MOD_ROUTING.md M3 — preset key "mixvol"
0..100 default 100, MASTER page MIX section; scales the post-ADSR/
FILTER signal only, raw mod sources untouched). AMENDED — master_level
× gate moved to the FX/master stage after the safety filter).
Cross-mod matrix (PLAN_MOD_ROUTING.md): 3 slots; slot = {source ∈
{OFF, B.osc, B.sub1, B.sub2, C.osc, C.sub1, C.sub2, A.osc, A.sub1,
A.sub2}, target ∈ {A, B, C}, dest ∈ {OFF, PITCH (vibrato — depth Hz ×
the source signal), PHASE (PM, ±180° mapping), FILTER (ratio), AMP,
RING (dry/wet 0..100), FM (operator index AMT/25)}, amount, mix (the
ADD knob — source audio blended into the target, 0..100)}. Modulation
is applied at the target's OSC stage, before its ADSR/filter.

## 6. Sequencers

Per chain i (A/B/C): `on` (bool — a **UI parameter**, never a button),
`div` (clock divider, integer 1..16, default 1 — a **UI parameter**),
**8 fixed steps**. Step = {pitch_hz (0.1 Hz–20 kHz — the frequency
sent to the chain), vel (0–100, integer percent)}. There is **no
active flag**: vel 0 = silent step.

**BPM is GLOBAL** (AMENDED, owner order): one integer 1–500 for the
whole synth — a top-level parameter next to `master`, NOT a
per-sequencer value. All three sequencers share it.

Each sequencer has a **MODE** selecting the step order:

- **fwd** (default): 0 → 7, repeats.
- **pingpong**: 0 → 7 then 7 → 0, bouncing.
- **oddeven**: odd 1-based indices first — steps 1,3,5,7 then 2,4,6,8.
- **random**: deterministic per-instance PRNG (like the oscillator noise
  PRNG: seeded per instance, no `rand()`), never repeating the same step
  immediately (a repeat is redrawn).
- **markov**: equally weighted 1D random walk — next = current ± 1 at
  p = 0.5 each, reflecting at the edges (0 → 1, 7 → 6); same PRNG.

Each sequencer has an **ONCE** option:

- **loop** (default): the mode's order repeats forever.
- **once**: one pass through the order, then the sequencer goes **silent**
  until it is restarted — by RUN toggled off → on, by the seq itself
  toggled off → on, or by a PANIC reset (PLAN §7). A "pass" is one
  traversal of the mode's full order (for random/markov, a pass is a
  fixed count of steps = 8, after which silence begins).

- Sequencers run only while RUN is ON.
- Step period = `60/BPM/4 × div` seconds (BPM the GLOBAL tempo, div
  per-chain; div = 1 → 16th notes).
  Scheduled by the audio thread from the sample counter (no timer/SDL
  timing).
- Step with vel > 0 → chain event: set chain frequency = step pitch_hz,
  retrigger ADSR with step vel. **Step-window semantics (owner order):
  a step's pitch_hz plays for THAT step's whole time window** (F1..F8
  each play in their step); a vel 0 step changes the pitch without a
  retrigger (the envelope sustains). Sequencer OFF → direct mode
  returns to the oscillator's frequency parameter.
- Step with vel = 0 → no retrigger (drone sustains); its pitch still
  applies during its window.
- Sequencer OFF → no events; chain runs in direct mode on the frequency
  parametrized in its oscillator.

## 7. Transport

- **RUN ON**: master gate opens (10 ms ramp); per chain: if its sequencer
  is ON it drives the retriggers; if OFF, direct mode — one retrigger at
  the chain's VELOCITY and sustain at the oscillator's parametrized
  frequency.
- **RUN OFF**: gate closes (10 ms); sequencers stop; all chains get a
  release trigger.
- **TRIGGER**: one-shot retrigger of all 3 chains at their chain
  VELOCITY, at their current frequencies. Does not touch sequencers.
  **Overrides RUN OFF**: sets a one-shot flag that holds the gate open;
  Owner order 2026-11-05: holding a VALUE key (X/Y on the R36S, Z/X on
  desktop) also retrigs — every applied value change fires the TRIGGER
  path, and the hold repeats value+retrig (400 ms delay, 120 ms rate)
  until release; pad buttons never auto-repeat, so the app synthesizes
  the repeat.
  the flag clears when all three envelopes have reached zero (or a 30 s
  safety timeout), then the gate closes again. While RUN is ON the flag is
  inert (gate already open).
- **PANIC** (from menu, no confirm): immediate, no ramp. RUN → OFF, gate →
  0 instantly, one-shot cleared, all envelopes → IDLE, all oscillator/sub
  phases → 0, all biquads reset, sequencer clocks reset. Consumed by the
  audio thread as an atomic event flag (same mechanism as TRIGGER).

## 8. UI (SDL2, same code both targets)

### 8.1 Interaction paradigm (binding, user-final)

Sliders for numeric values, selection labels for categorical values
(waveform, modulation type, …). Labels have nothing to do with sliders —
they cycle options. Multiple small screens because the screen is small.
Discrete presses only: **no long-press, no auto-repeat, no scroll.**

One selection cursor per screen. **Up/Down moves the selection** (wraps).
**Left/Right is navigation only** (sub-tab cycling on the sub-tab row,
SEQ step-rectangle selection; inert in content lists). **Z/X (desktop) /
X/Y (R36S) change the selected item's value** (slider: ±1 step; label:
cycle option, wraps). **Q/E (desktop) / L/R shoulder (R36S) = top-tab
back/next** across the 6 tabs (wraps; inactive inside menu windows).
**Space = RUN, T = TRIGGER, Enter = CONFIRM (desktop); A = RUN,
B = TRIGGER (R36S). Start/Esc = MENU.** (Owner-ordered rebind,
2026-11-05: desktop Enter selects in menus, Esc is the desktop's only
menu cancel, Space/T are inert inside menus; the R36S map is unchanged —
A confirms via KB_RUN_CONFIRMS_MENU, B cancels via
KB_TRIGGER_CANCELS_MENU, KB_CONFIRM unbound.) Z/X (desktop) / X/Y (R36S) are value keys (AMENDED,
PLAN_KEYMAP_REBIND; exception: SAVE name entry — value key inserts the
selected grid char, the other deletes).

**Menu window**: opened by Start/Esc on ANY screen; overlays the current
screen; the BACK item (or Start/Esc) closes it back to that screen. While
the menu window (incl. its sub-screens) is open: CONFIRM key = select
(desktop Enter; handheld A via KB_RUN_CONFIRMS_MENU), Start/Esc = close,
TRIGGER = cancel on the handheld only (KB_TRIGGER_CANCELS_MENU); Space
and T are inert on desktop; NAV and value keys are inert (U4 — they do not
close the window), except the SAVE grid picker (NAV moves the grid
cursor, X/Y insert/delete). Outside it the keys keep their normal
meanings.

**Hint bar** (bottom of every screen): the keymap in effect on that
screen, in this target's binding words.
**Status strip** (top of every screen): RUN on/off, gate 0.00–1.00,
TRIGGER/PANIC flash; on chain screens the chain's live envelope bar; on
seq screens the live step cursor. (Displayed, not selectable.)

### 8.2 Tabs (6 top tabs, no flat screens)

The UI is a tabbed interface: **5 top tabs** shown as
`OSC 1 | OSC 2 | OSC 3 | MOD | MASTER` (AMENDED, owner order: MIX is
renamed MOD — it holds the cross-MODulation matrix; the FX tab
DISAPPEARS — its delay/reverb parameters are sections of MASTER).
Desktop: **Q = tab back, E = tab next** (wraps). R36S: **L/R shoulder
buttons cycle top tabs** (wraps). Q/E are inactive inside menu windows.

Each **OSC X** tab has **3 sub-tabs**: `OSC | ENV+FLT | SEQ` (AMENDED,
owner order: the old SUB sub-tab is merged into OSC as sections, and
FILT is renamed ENV+FLT).

**Sections (AMENDED, owner order — every non-SEQ content page is a list
of labeled sections):** each section is a gray non-selectable **label
row**, followed by its item rows; section items are **indented 2 spaces
(20 px) relative to the section label**; **one empty line** separates
the end of a section from the next section's label. Labels/gaps are
visual rows only — the cursor walks the flat item list of the page.

| Tab | Sub-tab | Sections → items (selection order) |
|---|---|---|
| OSC 1/2/3 | OSC | **MAIN**: FREQ · WAVE · DUTY · LEVEL · VEL, then **SUB 1**: FMODE · FREQ · WAVE · DUTY · DEST · AMT, then **SUB 2**: same |
| OSC 1/2/3 | ENV+FLT | **ADSR**: ATTACK · DECAY · SUSTAIN · RELEASE, then **FILTER**: MODE · CUTOFF · RES · ENV · POLES |
| OSC 1/2/3 | SEQ | unchanged step page (below) |
| MOD | — | **MOD 1**: SRC · DEST · AMT, then **MOD 2**: same, then **MOD 3**: same |
| MASTER | — | **MASTER**: MASTER · SAFETY · BPM (the GLOBAL tempo, §6), then **DELAY**: TIME L · FEEDBACK · TONE · MIX, then **REVERB**: SIZE · DAMP · MIX · WIDTH |

MOD and MASTER have **no sub-tab row**.

**FX parameters (AMENDED — now sections of the MASTER tab, no FX tab):**
DELAY items — TIME L (seconds 0..2, **0.01 s steps**
— seconds, not the §8.3 log frequency mapping), FEEDBACK (percent 0..100),
TONE (LP cutoff, Hz 20..20 kHz, log 1/12-octave per §8.3), MIX (percent
0..100); REVERB items — SIZE, DAMP, MIX, WIDTH (percent 0..100 each).
MASTER tab gains **SAFETY** (boolean ON/OFF, default ON — the master
safety filter, PLAN §2).

**Navigation (binding):** Q/E = top tabs everywhere (except inside menu
windows). Arrows START on the sub-tab row (OSC tabs): LEFT/RIGHT on the
sub-tab row cycles sub-tabs; DOWN enters the sub-tab's content; UP from
content returns to the sub-tab row. In content: UP/DOWN move the cursor
(wrap), Z/X (desktop) / X/Y (R36S) edit (slider ±1 step, label cycles
one option, wraps); LEFT/RIGHT is inert in content. MOD/MASTER: focus
starts directly on the content. Menu overlay, status
strip, hint bar and keymap context remap are unchanged (§8.1/§8.4).
Cursor/focus persists per tab+sub-tab.

**SEQ page** (per chain, inside the OSC X tab's SEQ sub-tab):

(AMENDED, owner order) The page is **driven by the item table**
(`src/ui/seq_items.c` — pure data, testable): cursor index, item and
drawn row are 1:1, no separate hand mapping. **Single column — one
parameter per row, NO side-by-side parameters.** Cursor order (= visual
top-to-bottom order): **SEQ ON · DIV · MODE · ONCE · STEP · PITCH ·
VEL** (7 items — BPM is NOT here: the GLOBAL tempo is visible and
edited on the MASTER tab, owner order).

- SEQ ON/OFF, DIV (integer 1..16, default 1; step period =
  `60/BPM/4 × div`), **MODE** (label cycling fwd · pingpong · oddeven ·
  random · markov, §6) and **ONCE** (label cycling loop · once, §6) —
  each on its own row: name left, value right. The MODE and ONCE
  selectors MUST be visible on the page (regression-tested by
  tests/test_seq_items.c).
- **2 blank lines, then a "Steps" label** above the first cell — the
  sequencer section is visually separated and titled.
- STEP row — the 8 step rectangles strip; LEFT/RIGHT selects a
  rectangle. **(AMENDED, owner order) Step markers:** the currently
  SELECTED rectangle (the one PITCH/VEL edit) has a **thicker
  THEME_SEQ_SEL border** (PLAN_KEYMAP_REBIND §5); the step **playing
  now** is shown by a **dot inside the
  square** (live cursor from the status snapshot). Cells are filled
  when vel > 0, empty when vel = 0.
- PITCH row and VEL row — sliders **bound to the selected rectangle**;
  editing changes that step live, and vel 0 draws the rectangle empty.

When a waveform is a noise type (white/pink), its FREQ item is dimmed
(inert) — frequency does not apply.

### 8.3 Ranges and steps (binding, user-stated)

- Frequency-typed (FREQ, SUB FREQ in Hz, FILTER CUTOFF, SEQ STEP PITCH):
  0.1 Hz–20 kHz, **log, 1/12-octave steps** — value = 0.1 × 2^(pos/12),
  pos 0..212, clamped ≤ 20000.0 Hz. SUB FREQ ratio mode: 0.0625–4.0, log
  1/12-octave — value = 0.0625 × 2^(pos/12), pos 0..72.
- ADSR ATTACK / DECAY / RELEASE: **log/exp, 0 s–60 s** — pos 0 = 0.0 s;
  pos 1..50 = 0.001 s × (60000)^((pos−1)/49); pos 50 = 60 s.
- Percentages (SUSTAIN, LEVEL, VEL, STEP VEL, MASTER): **integer percent
  0–100, 1 percentage-point steps.** Sustain is a volume (0–100%).
- BPM (**GLOBAL**): **integer 1–500, step 1 — visible and edited on the
  MASTER tab (owner order), never on the SEQ pages.** SEQ DIV: **integer 1–16, step 1,
  default 1.** SEQ MODE is a **label** cycling the 5 options fwd ·
  pingpong · oddeven · random · markov (default fwd, §6). SEQ ONCE is a
  **label** cycling loop / once (default loop, §6).
- Everything else (SUB AMT, XMOD AMT): the range divided into **100 linear
  steps**. AMT range depends on DEST: PITCH → −50..+50 Hz (1 Hz/step);
  AMPLITUDE / FILTER → −100..+100 percent (1 pp/step); OFF → slider
  dimmed/inert, value kept. Switching DEST re-clamps AMT to the new range.

### 8.4 Menu window (overlay, any screen)

Vertical item list — NEW, SAVE, LOAD, PANIC, EXIT, BACK; cursor starts on
BACK (the safe choice). Up/Down moves the selection (wraps); CONFIRM key
= select (desktop Enter / handheld A); Start / Esc = close (Esc only on
desktop; TRIGGER also closes on the handheld).
- **NEW** → confirm sub-window (YES / NO). YES discards all parameters and
  loads factory defaults (`preset/defaults.c`), one mailbox commit.
- **SAVE** → name-entry sub-window. Name: 1..32 chars, charset A–Z, 0–9,
  `-`, `_`, space. Desktop: type with the keyboard, Enter saves, Esc
  cancels. R36S: character-grid picker — d-pad selects a cell, X inserts
  the selected char at the text cursor, Y deletes the char before the
  cursor, A saves, B cancels. Errors shown inline.
- **LOAD** → sub-window: a folder navigator. It opens in the default
  preset dir, creating the dir if missing (owner order 2026-11-05); a
  dir that cannot be opened falls back to its parent (file-picker
  behavior; CANNOT OPEN DIR only at the root). It shows the CURRENT
  FOLDER PATH on its title line. The list
  shows subdirectories (suffix "/", entered with the confirm key, ".." to
  go up) and *.json preset files alphabetically; up/down selection wraps
  the visible window, which follows the selection (no scroll key).
  Confirm on a preset loads all parameters (one mailbox commit); errors
  shown inline, not loaded. Beyond the default dir, any readable folder on
  the system may be navigated.
- **PANIC** → executes immediately on selection (no sub-window).
- **EXIT** → "QUITTING" flash (1 s) → close audio, quit.
- **BACK** → close the window, return to the screen it was opened from.

### 8.5 Preset files

JSON, one file per preset. Location: `presets/<name>.json` beside the
executable (resolved at runtime from /proc/self/exe; no env var, no
fallbacks). A preset serializes the
whole `synth_params_t` except the `run` transport flag (§9 format). Loading
replaces all parameters and commits them to the mailbox in one write.

### 8.6 Rendering

UI is laid out at a fixed 640×480 logical resolution; the SDL window is
640×UI_SCALE × 480×UI_SCALE and the renderer scales the logical buffer
(UI_SCALE from config.h: 1 on R36S, 2 on desktop). Embedded bitmap font
(no external assets), event-driven redraw, live status refresh
≤ 10 Hz. Handheld: window created fullscreen (`KB_FULLSCREEN` from
config.h).

(AMENDED, owner order — layout)
- **Letter size +2 pts**: glyphs render in **10×10 cells** (the
  embedded 8×8 face scaled up per-pixel; a "line" = 10 px).
- **Top tab bar and sub-tab bar are double-width** (2-line = 20 px
  filled strips), and **2 blank lines after the sub-tab bar** before
  any content.
- **1–2 blank lines between SUB1 and SUB2** on the SUB sub-tab
  (visual gap, not cursor rows).
- **2 blank lines + a "Steps" label** before the step rectangles
  (PLAN §8.2 SEQ page).
- The hint bar may wrap to two lines (10 px letters do not always fit
  the old single line).
- **Item row pitch is 16 px** (10 px letters + 6 px leading) so the
  largest sectioned page (OSC sub-tab: 3 labels + 17 items + 2 gaps =
  22 rows) fits 640×480 above the two-line hint bar.

## 9. Target difference (the ONLY one)

One file — `src/config.h` — is the only source that differs between builds.
The Makefile passes exactly one macro per target (`-DTARGET_DESKTOP` /
`-DTARGET_R36S`) and `config.h` is the only place that macro is read:

```c
#if defined(TARGET_R36S)
  #define UI_SCALE      1
  #define KB_FULLSCREEN 1
  #define KB_TRIGGER    <R36S button>
  /* ... remaining KB_* bindings ... */
#else /* TARGET_DESKTOP */
  #define UI_SCALE      2
  #define KB_FULLSCREEN 0
  #define KB_TRIGGER    OPHIS_KEY_RETURN
  #define KB_RUN        OPHIS_KEY_SPACE
  /* ... remaining KB_* bindings ... */
#endif
```

Binding values: `ui_action_t {TRIGGER, RUN, SEL_UP, SEL_DOWN, NAV_LEFT,
NAV_RIGHT, VAL_DEC, VAL_INC, TAB_BACK, TAB_NEXT, MENU}` → SDL key /
gamepad button.
Up/Down = selection; Left/Right = navigation only (sub-tab row cycle,
SEQ step-rectangle selection; inert elsewhere); Z/X (desktop) / X/Y
(R36S) = value/cycle; Q/E (desktop) / L/R shoulder (R36S) = top-tab
back/next (§8.2). No
per-sequencer buttons — sequencer ON/OFF is a UI parameter. No quit
action — quit is menu-only.

Desktop: UI_SCALE=2, windowed 1280×960; Up/Down=selection,
Left/Right=navigation, Z=value dec, X=value inc, Q=tab back, E=tab next,
Space=RUN, T=TRIGGER, Enter=CONFIRM, Esc=MENU (owner-ordered rebind,
2026-11-05; supersedes the PLAN_KEYMAP_REBIND table for desktop).
R36S: UI_SCALE=1, fullscreen; d-pad up/down=selection, d-pad
left/right=navigation, X=value inc, Y=value dec, L shoulder=tab back,
R shoulder=tab next, A=RUN, B=TRIGGER, Start=MENU,
Select=unassigned (SAVE name entry: X insert, Y delete).
**R36S layout verified against the physical device in M0; the binding
values in config.h are the sanctioned change point.**

### Preset JSON format (version 1)

```json
{
  "app": "ophis-drone", "version": 1, "master": 70, "bpm": 60,
  "safety": true,
  "chain": [
    { "freq_hz": 55.0, "wave": "saw", "level": 100, "vel": 100,
      "sub": [ { "fmode": "hz", "freq": 0.5, "wave": "sine",
                 "dest": "pitch", "amount": 2.0 },
               { "fmode": "off", "freq": 0.0, "wave": "sine",
                 "dest": "off", "amount": 0.0 } ],
      "adsr": { "a": 0.05, "d": 0.2, "s": 100, "r": 0.5 },
      "filter": { "mode": "lp", "fc": 800.0, "res": 10, "env": 0,
                  "poles": "12db" } } ],
  "seq": [ { "on": false, "div": 1,
              "mode": "fwd", "once": false,
              "steps": [ { "pitch_hz": 55.0, "vel": 100 } ] } ],
  "xmod": [ { "src": "off", "dest": "off", "amount": 0.0 } ],
  "delay": { "time_l": 0.38, "time_r": 0.51, "feedback": 35,
             "tone": 4500.0, "mix": 30 },
  "reverb": { "size": 85, "damp": 35, "mix": 30, "width": 100 }
}
```
- Enums serialized as lowercase strings (`sine|tri|saw|square|white|pink`,
  `hz|ratio`, `off|pitch|amp|filter`, `lp|hp|bp|br`,
  `off|b_osc|b_sub1|b_sub2|c_osc|c_sub1|c_sub2`).
- Integer percent fields (0..100): `master`, `level`, `vel`, `s`, step
  `vel`.
- Fixed-length arrays (3 chains, 2 subs, **8 steps**, 3 xmod slots) — a
  file with a different array length is invalid.
- Seq objects carry `div` (clock divider, integer 1..16, default 1 —
  REQUIRED per seq; step period = `60/BPM/4 × div`, §6 — BPM is the
  top-level GLOBAL tempo, REQUIRED, integer 1..500). Seq objects also
  carry REQUIRED `mode` ("fwd"|"pingpong"|"oddeven"|"random"|"markov",
  default fwd) and `once` (boolean; true = once, false = loop, default
  loop — §6).
- Filter objects carry `res` (integer percent 0..100), `env` (integer
  percent -100..+100) and `poles` ("12db"|"24db"). Format **version 1**
  (unchanged version number — nothing was ever shipped in the old shape).
- Osc and sub objects carry `duty` (integer percent 5..95, default 50,
  square only). `dest` gains "phase" (PM, amount percent ±100 = ±180°).
- Top-level `delay` {time_l, time_r seconds 0..2; feedback, mix percent
  0..100; tone Hz 20..20000} and `reverb` {size, damp, mix, width percent
  0..100} are REQUIRED fields — **AMENDED: they now describe real DSP**
  (the master-path delay and reverb, PLAN §2/§5), not reserved placeholders;
  units unchanged.
- Top-level `"safety"` boolean is REQUIRED, default **true** (master safety
  filter ON: HP 60 Hz + LP 18 kHz cascaded biquads; false = bypass).
- Loader: every listed field is required; unknown keys are ignored; any
  parse/length/version error rejects the whole file (no partial load).
- JSON is written and parsed by a minimal built-in implementation
  (`preset/json.c`) — no new dependencies.

## 10. TDD test plan (tests written FIRST, watched fail, then implemented)

| Test file | Asserts (counts measured at run) |
|---|---|
| test_osc.c | zero-crossing rate ±1 for sine/tri/saw/square; white: DC≈0, RMS≈1/√3, flat spectrum (Goertzel), seeded determinism; pink: DC≈0, ≈−3 dB/octave slope (Goertzel, ±1.5 dB tolerance); no \|x\|>1 for any waveform |
| test_mod.c | each destination alters only its parameter (detune→x-rate shift in Hz, amp→RMS, filter→10 kHz level); both sub freq modes (Hz, ratio) |
| test_adsr.c | A/D/S/R envelope shape at exact sample counts; ≤1.0; release from attack and from sustain |
| test_filter.c | LP/HP pass/stop dB at 100 Hz/10 kHz for fc=1 kHz; BP band, BR notch at fc; resonance: higher Q raises gain at fc (measured); poles 24db: 10 kHz stop slope ≈ 6 dB deeper than 12db; env amount shifts cutoff end-to-end |
| test_chain.c | velocity 0 → silence; trigger → audible; sub mod observable end-to-end |
| test_mixer.c | sum correctness; tanh keeps \|out\|≤1; cross-mod B→A detune measurable |
| test_fx.c | delay: impulse appears at TIME L/R (sample-accurate), feedback loop decays, TONE LP rolls off tails; reverb: wet-only tail decays, DAMP shortens HF tail, WIDTH alters mid/side spread, **STABILITY GRID: bounded output for EVERY size/damp combination and the shipped preset FX values (owner order — the old comb structure exploded and crashed the sound system)**; safety ON: −3 dB near 60 Hz/18 kHz corners, 4th-order stopband slope; safety OFF: bypass bit-identical; MIX 0 → dry passthrough; gate/master level after safety per the amended §5 order |
| test_sequencer.c | step period = 60/BPM/4 × div in samples (BPM the GLOBAL tempo); delivery order/values (pitch_hz + vel); on OFF → zero events; vel 0 step → zero events; tempo change; div change; silent while RUN off |
| test_transport.c | RUN off → gate 0; on → 1 after ramp; TRIGGER while RUN off → gate opens, exactly 3 one-shot retriggers at each chain's VEL, gate auto-closes when all envelopes reach 0; PANIC → gate 0 instant, all state reset, RUN cleared; sequencers untouched by TRIGGER |
| test_json.c | write→parse roundtrip bit-identical to input params; enum-string mapping; rejects: truncated file, garbage, missing field, wrong version, wrong array length; unknown keys ignored |
| test_preset.c | save→list→load roundtrip in a temp dir; name sanitization (rejects empty, >32 chars, `..`, `/`, bad charset); list alphabetical; invalid file rejected with error, params unchanged |
| test_engine.c | integration (desktop): render 1 s via ALSA; xrun count read from ALSA stats must be exactly 0 — any xrun fails the test |
| test_engine_seq.c | (AMENDED, owner order) engine-level seq application, pure (no ALSA): a fired step's pitch PERSISTS between step events (zero-crossing rate per step window == the step's pitch); step vel scales the sustained level; sequencer OFF returns the chain to the oscillator's frequency parameter |
| test_keymap.c | compile-level: desktop KB_* bindings are OUR ophis_key_t ids (no SDL needed); sanity: no SDLK_* leaked |
| test_theme.c | WCAG AA ≥ 4.5:1 for every text/background pair on the exported palette |
| test_seq_items.c | (AMENDED, owner order) the pure UI page/section model: SEQ_ITEMS (7, order SEQ ON · DIV · MODE · ONCE · STEP · PITCH · VEL, MODE and ONCE present, no BPM) and the sectioned pages — OSC sub-tab (MAIN 5 / SUB 1 6 / SUB 2 6), ENV+FLT (ADSR 4 / FILTER 5), MOD (3 × SRC · DEST · AMT), MASTER (MASTER 3 incl. BPM / DELAY 4 / REVERB 4): exact section labels, item orders and per-section counts |
| test_font.c | (AMENDED, owner order) font metrics: OPHIS_FONT_W/H == 10 (letters +2 pts); the 8×8→10×10 scaler is a faithful per-pixel scale (every scaled set bit maps back to a set source bit and every set source bit has ≥1 scaled pixel) |
| test_range.c | (AMENDED, owner order) the pure range helpers (src/ui/range.c): hostile inputs (0, negative, ±inf, NaN) never crash — log1p + isfinite guards; log round-trips pos↔value for the Hz and ratio ladders; every pos maps inside its range |

Gate: `make test` reports `N/M passed`; every milestone's DoD names the
exact test files it must pass. DSP is 100% unit-gated; UI and on-device
behavior are manual and will be reported as such (never claimed by
construction).
**Xrun gate (desktop): every machine-verified desktop run must report a
measured ALSA xrun count of exactly 0. A non-zero count is a failed
milestone — fix the implementation and re-run; it is never reported as a
pass.**

## 11. Milestones (one subagent each, strictly sequential)

- **M0 Spike & scaffold** — repo layout, Makefile (both targets, the
  TARGET_* macros), deps check (pkg-config sdl2/alsa, aarch64 cross
  toolchain), static-link/container decision for glibc-2.28 compat, minimal
  `main.c`: SDL window + ALSA silence on desktop. Exit: desktop hello runs;
  on-device checklist (sdl2/alsa present, glibc version, audio device name,
  screen mode) handed to user.
- **M1 DSP core** — TDD: osc, subosc, adsr, filter, chain, mixer
  (tests §10, first 6 files). No SDL/ALSA in this milestone.
- **M2 Sequencer + transport** — TDD: test_sequencer, test_transport
  (incl. one-shot gate and PANIC) → implementation.
- **M3 Presets** — TDD: test_json, test_preset → `preset/json.c`,
  `preset/preset.c`, `preset/defaults.c` per the §9 JSON format. No
  SDL/ALSA in this milestone.
- **M4 ALSA engine** — alsa_pcm (incl. xrun counter), host-thread engine,
  lock-free mailbox, test_engine asserting measured ALSA xruns == 0;
  desktop smoke: audible drone, TRIGGER/RUN from stub keymap, 30 s run
  with measured xruns == 0.
- **M5 UI** — SDL2 UI per §8.2 (6 top tabs OSC 1/2/3/MIX/FX/MASTER with the
  OSC sub-tabs OSC/SUB/FILT/SEQ and the SEQ step page) + the menu window
  (overlay on any screen: NEW confirm, SAVE name entry — desktop typing +
  R36S grid picker, LOAD list, PANIC, EXIT, BACK), built-in font, keymap
  dispatch over config.h bindings incl. the menu context remap, desktop
  integration run (xruns == 0 gate applies).
- **FX stage (AMENDED, owner-ordered; TDD)** — `dsp/fx.h/.c` per §2/§5:
  delay → reverb → safety filter → master level → gate in the master
  path; `synth_params_t` gains `safety` (default ON); defaults give wet 0
  (FX silent); test_fx.c per §10. Contract change flagged: the master gate
  moves OUT of `mixer_render` into the FX/master stage.
- **M6.5 User manual & README** — HTML manual page (GitHub Pages-ready:
  console description, features, installation, build requirements,
  building) + a README.md covering the same, complete but concise, plain
  technical prose (no marketing/LLM filler).
- **M6 Handheld target & deploy** — the `#if defined(TARGET_R36S)` block in
  config.h (bindings + UI_SCALE=1), cross-build, glibc-2.28-compat link
  strategy, deploy packaging for ArkOS/EmulationStation: the program in a
  folder under `roms/ports/` and an executable `.sh` launcher OUTSIDE that
  folder (directly in `roms/ports/`, EmulationStation lists it). Required
  outputs: a deploy folder (gitignored) and a zip of that folder. On-device
  acceptance checklist for the user (audio, screen, each button, menu +
  presets on device, xruns).

Definition of done (every milestone): `make test` at or above previous
milestone count with the new files green; `-Wall -Wextra` clean; desktop
binary runs with measured ALSA xruns = 0 (the desktop xrun gate of §10);
milestone report states measured test counts (N/M) and which checks were
manual (not machine-verified).

## 12. Risks / open items

1. glibc mismatch (ArkOS ≈ 2.28) → static libs or 19.10-container build,
   decided in M0.
2. SDL2 video driver on ArkOS (KMS/fbdev) — M0 on-device spike.
3. ALSA device naming on RK3326 — M0 on-device `aplay -l`.
4. No WiFi → binary transfer via SD card or USB OTG; on-device iteration
   loop confirmed in M0.
5. R36S physical button layout — verify the §9 map in M0 (config.h
   binding values are the sanctioned change point). UI scale resolved:
   1 handheld / 2 desktop.
