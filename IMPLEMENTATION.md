# ophis-drone — Implementation Plan (BINDING)

Companion to `PLAN.md` (binding design contract). This document is binding
for structure, types, signatures, and TDD sequencing. Where the two
documents disagree, **PLAN.md wins** and the disagreement is reported, not
silently resolved.

Same AGENTS.md rules apply: TDD (tests first, watched failing), subagents
one at a time, no plan-related decisions during implementation.

## 1. Parameters vs state (the core split)

- `synth_params_t` — every UI-editable value. Pure POD, ~400 bytes. The UI
  thread writes it; the audio thread reads it through the mailbox (§3).
  Also the preset serialization unit (minus the `run` flag, §7).
- State (audio-thread only, never in the mailbox): oscillator phases, ADSR
  state, biquad history, sequencer clocks, transport gate/one-shot,
  current `f_osc` per chain.

```c
/* src/dsp/types.h — shared enums/types, no SDL/ALSA */
typedef enum { WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_SQUARE,
               WAVE_WHITE, WAVE_PINK } wave_t;

typedef enum { MOD_OFF, MOD_PITCH, MOD_PHASE, MOD_AMP, MOD_FILTER,
               MOD_RING, MOD_FM } mod_dest_t;

typedef enum { FILT_LP, FILT_HP, FILT_BP, FILT_BR } filt_mode_t;

typedef struct { float a, d; int s; float r; } adsr_params_t;
/* a,d,r: seconds (0..60, log/exp in UI); s: percent 0..100 */

typedef struct {
  int div;                 /* subharmonic divider 1..16 (parent_f / div) */
  wave_t wave;             /* sine/tri/saw/square — subs are tonal (O2) */
  int duty;                /* square pulse width, percent 5..95, default 50 */
  int level;               /* percent 0..100 into the chain's OSC SUM */
} subosc_params_t;

typedef struct {
  float freq_hz;            /* oscillator frequency parameter (Hz) */
  wave_t wave;
  int duty;                 /* square pulse width, percent 5..95, default 50 */
  subosc_params_t sub[2];
  adsr_params_t adsr;
  filt_mode_t fmode; float fc;
  int res; int envamt; int poles; /* res: percent 0..100 -> Q 0.5..20 log;
                              envamt: percent -100..+100 (chain ADSR -> cutoff);
                              poles: 12 or 24 (dB/oct, biquad cascade) */
  int level; int vel;      /* percent 0..100; vel = retrigger level for
                              TRIGGER and direct mode (PLAN §7) */
} chain_params_t;

/* cross-mod source ids: 0=OFF, 1=B.osc 2=B.sub1 3=B.sub2
                          4=C.osc 5=C.sub1 6=C.sub2 */
typedef struct { int src; int dest; float amount; } xmod_params_t;

typedef struct { float pitch_hz; int vel; } seq_step_t;
/* vel: percent 0..100; vel 0 = silent step (there is no active flag) */

typedef struct {
  int on; int bpm; int div; int mode; int once; seq_step_t steps[8];
  /* bpm: integer 1..500; div: clock divider integer 1..16, default 1
     (step period = 60/bpm/4 * div, PLAN §6)
     mode: step order (PLAN §6): 0=fwd (default), 1=pingpong, 2=oddeven,
     3=random, 4=markov. once: 0=loop (default), 1=once (one pass then
     silent until restarted: RUN off->on, seq off->on, or PANIC reset). */
} seq_params_t;

typedef struct { float time_l, time_r; int feedback, tone, mix; } delay_params_t;
/* time_l/r: seconds 0..2 (UI step 0.01 s); feedback, mix: percent 0..100;
   tone: LP cutoff Hz 20..20000 (log 1/12-oct in UI) */
typedef struct { int size, damp, mix, width; } reverb_params_t;
/* all percent 0..100 */
typedef struct {
  chain_params_t chain[3];
  seq_params_t   seq[3];
  xmod_params_t  xmod[3];
  delay_params_t delay;      /* real DSP (FX milestone): stereo delay */
  reverb_params_t reverb;    /* real DSP (FX milestone): FDN reverb */
  int            bpm;         /* GLOBAL tempo 1..500 (AMENDED, owner order:
                                 one BPM for the whole synth, shared by all
                                 sequencers — PLAN §6; NOT in seq_params_t) */
  int            master;      /* percent 0..100 */
  int            safety;      /* master safety filter: 1 = ON (default),
                                 0 = bypass — see dsp/fx contract */
  int            run;         /* transport */
} synth_params_t;
```

## 2. Module contracts

```c
/* dsp/osc.c — pure, deterministic (no time() / rand()) */
void   osc_init(osc_t *o, wave_t w, uint64_t seed); /* phase=0, reset PRNG */
double osc_next(osc_t *o, double freq_hz, double sr); /* advance, -1..1 */
/* osc_t { double phase; wave_t wave; uint64_t rng[4];
           double pink[3]; int pink_init; }
   sine/tri/saw/square : phase += freq_hz/sr, wrapped.
   SQUARE honours o->duty (pulse width). 
   WHITE : uniform -1..1 from xoshiro256** over o->rng (seeded).
   PINK  : Paul Kellet 3-stage filter over o->pink, driven by xoshiro.
   Noise ignores freq_hz (freq param is inert, UI dims it). */

/* dsp/subosc.c — pure; owns its own phase (subosc_state_t { osc_t o; });
 * Spice/Subharmonicon voice: plays main_freq_hz / div (integer 1..16) */
double subosc_out(subosc_state_t *s, const subosc_params_t *p,
                  double main_freq_hz, double sr);

/* dsp/adsr.c — pure */
void  adsr_reset(adsr_state_t *e);
void  adsr_trigger(adsr_state_t *e);
float adsr_next(adsr_state_t *e, const adsr_params_t *p, double sr); /* 0..1 */
/* states: IDLE, ATTACK, DECAY, SUSTAIN, RELEASE; trigger from any state */

/* dsp/filter.c — RBJ biquad, pure */
void  biquad_set(biquad_t *b, filt_mode_t m, float fc, double sr, float q);
double biquad_next(biquad_t *b, double x);
/* poles=24 (PLAN §5): identical biquad pair sharing one cutoff/Q, cascade
   helper in filter.h (biquad_cascade_set or equivalent). */

/* dsp/chain.c — pure; per sample, render order per PLAN.md §5 */
typedef struct { float detune_hz, amp_mod, filter_mod; } xmod_in_t;
typedef struct { int retrigger; float vel; } chain_event_t;
double chain_render(chain_state_t *c, const chain_params_t *p,
                    const xmod_in_t *xm, const chain_event_t *ev, double sr);
/* chain_state_t { osc_t osc; subosc_state_t sub[2]; adsr_state_t env;
                   biquad_t filt; float vel; float f_osc;
                   double raw_osc_out; }  (raw_osc_out = mod source for xmod) */

/* dsp/mixer.c — pure */
void matrix_render(mixer_state_t *m, const mixer_params_t *p,
                   const double chain_out[3], double post[3]);
double master_sum(const mixer_params_t *p, const double post[3]);
double mixer_render(mixer_state_t *m, const mixer_params_t *p,
                    const double chain_out[3], double sr); /* = both */
/* p { const synth_params_t *synth; } — the mixer reads the LIVE params
   (S4, no copies). matrix_render computes the per-chain xm[] and the
   RING/MIX slot outputs post[3]; master_sum is
   tanh(1.5 * Σ (mixvol[c]/100 × post[c])) (M3); mixer_render is their
   composition (S2). Gate + master level live in the FX/master stage
   (dsp/fx.c) after the safety filter.
   reads raw mod sources passed via mixer_state_t->src[6] (set by engine) */

/* dsp/fx.c — pure; master-path FX + safety + master level + gate.
   AMENDED stage (owner order): mixer -> delay -> reverb -> SAFETY ->
   master level -> master gate. */
void fx_process(fx_state_t *fx, const fx_params_t *p,
                float in_l, float in_r, float *out_l, float *out_r,
                double sr);
/* "-> stereo out": the ask's signature returns the stereo pair; the two
   out pointers are how "stereo out" is materialized in C (flagged in the
   milestone report, not silently decided). */
/* fx_params_t { delay_params_t delay; reverb_params_t reverb;
                 int safety; int master; float gate; }  (gate from
                 transport, applied here per sample — NOT in mixer.c)
   fx_state_t: FIXED preallocated buffers — stereo delay lines of 2 s at
   44100 Hz (static arrays, no malloc in the RT path), reverb comb/allpass
   state, two cascaded biquads (HP 60 Hz + LP 18 kHz).
   Order per sample: delay (stereo, feedback, TONE LP in the loop, MIX
   wet/dry) -> reverb (FDN/Schroeder class, damping LP inside the loop,
   WIDTH = mid/side spread) -> safety (safety ? cascaded HP60+LP18k
   biquads : bypass) -> master level (percent/100) -> gate (10 ms ramp). */
void fx_reset(fx_state_t *fx);   /* PANIC: clear lines, biquads, reverb state */

/* seq/sequencer.c — pure (no timing deps; sample time in, events out) */
typedef struct { double next_step_time; int cursor;
                 uint64_t rng[4];       /* per-instance deterministic PRNG
                                           (xoshiro256**, seeded like osc,
                                           PLAN §6 random/markov) */
                 int prev;              /* last emitted step (random:
                                           no immediate repeat) / walk pos */
                 int pass_done;         /* once: set after one full pass */
               } seq_state_t;
/* seq_params_t carries on/div/mode/once/steps — BPM is NOT per-seq
   (AMENDED, owner order, PLAN §6): the GLOBAL tempo is passed by the
   caller. advances the clock; step period = 60/bpm/4 × div (bpm the
   global tempo, div per-chain); picks the next step per p->mode
   (fwd/pingpong/oddeven/random/markov, PLAN §6) and p->once (loop:
   repeats forever; once: after one full pass returns -1 forever until
   seq_reset, which is called on RUN off->on, seq off->on, or PANIC
   reset).
   returns step index 0..7 on boundary, else -1 */
int seq_tick(seq_state_t *s, const seq_params_t *p, int bpm, double now,
             double sr);
void seq_reset(seq_state_t *s);

/* seq/transport.c — pure */
typedef struct { int one_shot; float gate; double one_shot_deadline; }
        transport_state_t;
void transport_process(transport_state_t *t, int run, int trigger_req,
                       int panic_req, const float env_level[3],
                       double block_frames, double sr);
/* gate ramp 10 ms; one_shot: set by trigger_req, cleared when all envs == 0
   or after 30 s; gate = (run || one_shot)
   panic_req: run->0, gate = 0 instant (no ramp), one_shot cleared,
   one_shot_deadline = 0; caller (engine) then resets chain/seq state */

/* preset/defaults.c — factory defaults, no deps */
extern const synth_params_t OPHIS_DEFAULTS;
/* A: 55.0 Hz saw, level 100, vel 100, subs off, A.05/D.2/S100/R.5, LP 800
   B: 55.5 Hz square, level 50, vel 100, subs silent (div 1 sine
      level 0 — O2 voices), A.05/D.2/S100/R.5, LP 600 Hz
   C: 110.0 Hz sine, level 30, vel 100, subs off, A.1/D.3/S80/R1.0,
      LP 1200 Hz
   seq: all off, div 1, mode fwd (0), once loop (0),
         all steps {55.0 Hz, vel 100}
   xmod: all OFF; bpm 60 (GLOBAL, AMENDED); master 70; safety 1
   delay: {time_l 0.0, time_r 0.0, feedback 0, tone 20000, mix 0}
   reverb: {size 0, damp 0, mix 0, width 0}  (wet 0 -> FX silent by default)
   run 0 */

/* preset/schema_{tab,write,parse}.c + schema_priv.h — schema layer over
   the vendored sheredom/json.h (M2 split, PLAN_OPTIMIZATION.md O5):
   (third_party/json.h, Unlicense — see README VENDORS; PLAN_CODE_REVIEW_
   FIXES.md Phase 7 replaced the hand-rolled preset/json.{c,h}).
   Writer: synth_params_t -> char* buffer through json.h's minified writer
   (caller provides 8 KiB, fixed sizes so 8 KiB is provably enough).
   Parser: walks the json.h DOM; lenient about whitespace/unknown keys,
   strict about: app/version, required fields, array lengths, enum
   strings, number ranges, duplicate keys (rejected), over-long keys
   (> 15 chars, rejected). Errors are field paths, e.g.
   "seq.steps[2].vel". */
int  schema_write_params(char *buf, int buf_len, const synth_params_t *p);
int  schema_parse_params(const char *text, int text_len, synth_params_t *p,
                         char *err, int err_len);  /* 0 ok, -1 error */
/* enum name accessors (moved here from preset/json.c, Phase 7) */
const char *schema_wave_name(int v);   /* ... also dest/xsrc/filt/poles */

/* preset/preset.c — file IO over preset/schema.c */
int  preset_dir(char *out, int out_len);          /* presets/ beside the
                                                     executable (via /proc/self/exe;
                                                     no env var, no fallbacks) */
int  preset_list(const char *dir, char names[][33], int *count, int max);
                                           /* *.json, alphabetical, count out */
int  preset_list_dirs(const char *dir, char names[][33], int *count, int max);
                                           /* subdirectories + "..", alphabetical */
int  preset_path(char *out, int out_len, const char *dir, const char *name);
                                           /* resolved path of a file/dir */
int  preset_save(const char *name, const synth_params_t *p,
                 char *err, int err_len);         /* sanitizes name */
int  preset_load(const char *name, synth_params_t *p, char *err, int err_len);
/* name rules: 1..32 chars from [A-Za-z0-9 _-]; reject empty, ".", "..",
   anything with "/", length > 32, bad chars; stored as <name>.json */
```

```c
/* audio/alsa_pcm.c — the ONLY ALSA user (besides tests) */
typedef struct alsa_pcm alsa_pcm_t;
alsa_pcm_t *alsa_open(const char *dev, int rate, int period_frames);
int  alsa_write(alsa_pcm_t *a, const float *stereo_interleaved, int frames);
long alsa_xruns(const alsa_pcm_t *a);   /* cumulative count, from ALSA stats */
void alsa_close(alsa_pcm_t *a);
/* S32_FLOAT, 44100, period 256. On XRUN: snd_pcm_recover + drop the block
   + count it. DESKTOP GATE: the cumulative count must stay exactly 0 —
   any xrun on desktop means the implementation is wrong (PLAN §2/§10). */

/* audio/mailbox.c */
typedef struct {
  synth_params_t buf[2];
  _Atomic unsigned version;   /* published index: 0 or 1 */
} mailbox_t;
void mailbox_write(mailbox_t *mb, const synth_params_t *p); /* UI thread */
void mailbox_read(mailbox_t *mb, synth_params_t *out);      /* host thread */
/* host<->ui event flags (lock-free):
   _Atomic int trigger_req, panic_req;  set by UI, atomic_fetch_xchg by
   host at block start. RUN is read from the mailbox params (p.run).
   Status snapshot (host -> UI): two preallocated ui_status_t slots +
   _Atomic uint64 index (store-release / load-acquire); UI reads the
   published pointer. No mutex/condvar anywhere between the threads. */

/* audio/engine.c — audio thread body, pure except mailbox */
typedef struct {
  double sr, now;
  chain_state_t ch[3]; seq_state_t sq[3]; transport_state_t tr;
  mixer_state_t mx; fx_state_t fx; synth_params_t p;
} engine_t;
void engine_init(engine_t *e, double sr);
void engine_render_block(engine_t *e, float *out_stereo, int frames,
                         int trigger_req, int panic_req);
void engine_panic_reset(engine_t *e);   /* envs->IDLE, phases->0, biquads,
                                           seq clocks, fx_reset (delay
                                           lines + reverb + safety);
                                           called by render loop when
                                           transport saw panic */
```

## 3. Host ↔ UI channels (all lock-free — binding rule)

**The host thread (engine + audio) and the UI thread never share a lock.**
No mutex, no condition variable, no blocking call between them. Exactly
three channels (contracts in §2):

1. **Parameter mailbox** (UI → host): UI edits a local `synth_params_t
   draft`, calls `mailbox_write` (debounced per event) — writes the
   inactive half, publishes with an atomic version bump. Host copies the
   published snapshot into `engine_t.p` at the start of each render block.
   Worst case: one stale block (256 frames ≈ 5.8 ms) — acceptable,
   documented.
2. **Event flags** (UI → host): `trigger_req`, `panic_req` as atomic
   fetch-exchange; `run` travels inside the mailbox params.
3. **Status snapshot** (host → UI): atomic pointer swap between two
   preallocated `ui_status_t` slots; UI never writes it.

## 4. Engine block loop (host thread)

```
for each 256-frame block:
  mailbox_read → e.p
  transport_process(&e.tr, e.p.run, trigger_flag_xchg(0),
                    panic_flag_xchg(0), env_levels, 256, sr)
  if e.tr just panicked: engine_panic_reset(e); e.p.run = 0 (mirrored to
     UI status next frame)
  for c in 0..2:
    ev = {0, 0.0f}   /* ev.vel filled on retrigger paths: chain vel/100.0
                        (direct mode, TRIGGER) or step.vel/100.0 (seq) */
    if e.tr.gate rising and seq off:  ev = retrigger (direct mode, PLAN §7)
    if e.tr.gate falling:             ev = release
    if trigger_req:                   ev = retrigger (all chains, PLAN §7)
     for c in 0..2 with seq on and run:
       st = seq_tick(...); if st >= 0:
         e.cur_step[c] = st; e.cur_valid[c] = 1     /* enter that step's
                                                        time window */
         if step.vel > 0: ev.retrigger = 1; ev.vel = step.vel / 100.0
     /* step-window semantics (owner order, PLAN §6; standard CV/gate
        step sequencer): while a chain's sequencer is ON, every block
        renders at the CURRENT step's pitch_hz — the step's knob value
        is the chain frequency for that step's whole time interval.
        vel > 0 additionally retriggers the ADSR at that velocity
        (gate); vel 0 = rest — no retrigger, the envelope sustains, but
        the pitch still follows the step. Sequencer OFF clears
        cur_valid[c] (direct mode = the oscillator's freq param). PANIC
        reset clears it too. */
     cp[c] = p.chain[c]; if seq on and cur_valid[c]:
       cp[c].freq_hz = p.seq[c].steps[e.cur_step[c]].pitch_hz
     xm = xmod_from_raw_sources(e, c)          /* for c==0 from B/C raw */
     out[c] = chain_render(&e.ch[c], &cp[c], &xm, &ev, sr)
  set mx.src[6] from e.ch[1], e.ch[2] raw osc/sub outputs
  AMENDED block loop (owner order — FX/master stage after the mixer):
  mxp = {xmod[3]}            /* gate + master level are NOT here anymore */
  fxp = {p.delay, p.reverb, p.safety, p.master, gate: e.tr.gate}
  for f in 0..255:
    l = r = mixer_render(&e.mx, &mxp, out, sr)      /* ends at tanh(...) */
    fx_process(&e.fx, &fxp, l, r, &l, &r, sr)       /* delay -> reverb ->
                                                       safety -> master
                                                       level -> gate */
    out_stereo[2f] = l; out_stereo[2f+1] = r
  alsa_write(...)
```
(Chains are block-level for sequencer events and gate changes; intra-block
sample accuracy is kept by per-sample osc/adsr/biquad inside
`chain_render`. This is the spec; do not "simplify" per-sample vs
per-block without flagging a plan deviation.)

## 5. Build system

```
make            → build/desktop/ophis-drone   (-DTARGET_DESKTOP)
make handheld   → build/r36s/ophis-drone      (-DTARGET_R36S, aarch64 cross)
make test       → build/tests + run all, print "N/M passed"
make clean
```
- `CFLAGS = -std=c11 -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L`
- deps via `pkg-config --cflags --libs sdl2 alsa` (desktop).
- Cross: `CC = aarch64-linux-gnu-gcc`; SDL2/ALSA aarch64 libraries +
  static-link strategy decided in M0, recorded in `BUILD_NOTES.md`
  (decision delegated by PLAN.md §11 M0).
- Unit tests (M1–M3) compile `src/dsp/*` + `src/seq/*` + `src/preset/*`
  only — no SDL/ALSA. `test_engine.c` is the only test linking ALSA
  (device "null").
- **No new build dependencies**: JSON is the vendored single-header
  `third_party/json.h` (public domain) + the `preset/schema.c` layer
  (Phase 7, PLAN_CODE_REVIEW_FIXES.md).

## 6. UI (M5) details

- `ui/font8x8.c`: embedded bitmap font. Glyph set = ASCII 0x20–0x7E.
  Amended (owner): a nicer public-domain face (5×7-in-8×8 or 8×16 cell)
  replaces the original 8×8; still embedded data, no TTF/SDL_ttf. The
  theme is dark-purple based (constants in ui.c) and must pass WCAG AA
  (≥4.5:1 for every text/background pair), asserted in
  tests/test_theme.c on the exported palette constants.
- `ui/range.c` (AMENDED, owner order): pure range/position helpers, no
  SDL — `pos_to_hz`/`hz_to_pos` (0.1 Hz..20 kHz, 1/12-octave ladder) and
  `pos_to_ratio`/`ratio_to_pos` (0.0625..4.0). The pos-from-value
  conversions are **log1p-based with isfinite guards**: hostile values
  (0, negative, ±inf, NaN — e.g. from a preset or an edit race) can never
  reach log(0)/NaN and never hit UB in lround. Tested by
  tests/test_range.c. Consumed by ui.c (PLAN §8.3 ladders).
- `ui/seq_items.c` (AMENDED, owner order): the SEQ page item table as
  PURE data (item ids + order + the MODE/ONCE option-name arrays), no
  SDL. Consumed by ui.c; asserted by tests/test_seq_items.c (exact
  order SEQ ON · DIV · MODE · ONCE · STEP · PITCH · VEL — 7 items, NO
  BPM; MODE and ONCE present — the demanded presence regression). Also
  exports MASTER_ITEMS (MASTER · SAFETY · BPM) — the GLOBAL tempo is
  visible and edited on the MASTER tab; the test asserts BPM is
  present there.
- `ui/font8x8.c` (AMENDED, owner order): a real translation unit (was
  #included by ui.c) exporting the embedded 8×8 glyph table plus
  `font_glyph10()` — the 10×10 per-pixel scaled cache (letters +2 pts,
  PLAN §8.6). Metrics in ui/font8x8.h: OPHIS_FONT_W/H == 10. Tested by
  tests/test_font.c.
- `ui/ui.c` (X3, PLAN_CODE_REVIEW_FIXES.md Phase 8: split into three TUs
  over the private header `ui/ui_internal.h` — `ui.c` state machine,
  `ui/ui_menu.c` menu/dialog logic, `ui/ui_draw.c` rendering; public API
  unchanged in `ui/ui.h`):
  - window: `SDL_CreateWindow` 640×UI_SCALE × 480×UI_SCALE; on handheld
    `SDL_WINDOW_FULLSCREEN` when `KB_FULLSCREEN`;
    `SDL_RenderSetLogicalSize(r, 640, 480)` → all drawing in 640×480 coords.
  - layout (AMENDED, owner order, PLAN §8.6): 10×10 font cells (letters
    +2 pts); top tab bar and sub-tab bar are double-width 20 px filled
    strips; 2 blank lines after the sub-tab bar before content; text via
    the glyph-atlas draw path (X1: one SDL_TEXTUREACCESS_STATIC texture of
    the 96 scaled glyphs, white-on-transparent, alpha blended, tinted per
    call — one quad per glyph instead of one fill rect per lit pixel),
    bars/boxes via `SDL_RenderFillRect`. No assets. The
    status strip draws the centered OPHIS DRONE title (PLAN_KEYMAP_REBIND
    §4); TRIGGER/PANIC flash text sits at the right side (x≈560).
  - item model: every sub-tab's content is a flat ordered list of items —
    SLIDER (range, step, format per PLAN §8.3) or LABEL (option list).
    UP/DOWN moves the cursor (wraps); Z/X (desktop) / X/Y (R36S) edit it
    (slider ±1 step; label cycles one option, wraps). LEFT/RIGHT is
     navigation only (sub-tab row, SEQ rectangles); inert in content.
     Item tables are plain data arrays in ui/seq_items.c (pure, tested).
   - tab model (replaces the old 14-screen model, PLAN §8.2):

```c
typedef enum { TAB_OSC1, TAB_OSC2, TAB_OSC3, TAB_MOD,
               TAB_MASTER } top_tab_t;   /* AMENDED, owner order: 5 top
                                           tabs — MIX renamed MOD, FX
                                           tab gone (delay/reverb are
                                           MASTER sections) */
typedef enum { SUB_OSC, SUB_ENVFLT, SUB_SEQ } sub_tab_t; /* 3 sub-tabs:
                                           SUB merged into OSC as
                                           sections; FILT renamed
                                           ENV+FLT */
typedef struct {
  top_tab_t tab;        /* current top tab (Q/E or L/R shoulder cycles, wraps) */
  sub_tab_t sub[3];     /* per OSC tab: current sub-tab (sub_tab_t) */
  int focus;            /* 0 = sub-tab row, 1 = content (OSC tabs) */
  int cursor;           /* content cursor index, per tab+sub-tab */
} ui_focus_t;
/* MOD and MASTER have no sub-tab row: focus starts directly on content.
   Focus (tab + sub-tab + focus + cursor) persists per tab+sub-tab while
   navigating. Q/E are ignored while a menu window is open. */
```

  Navigation: arrows start on the sub-tab row; LEFT/RIGHT cycle sub-tabs;
  DOWN enters content; UP returns to the sub-tab row. In content
  UP/DOWN = cursor, Z/X (desktop) / X/Y (R36S) = edit, LEFT/RIGHT inert
  (per PLAN §8.2).
  - Live status (envelope bar, step cursor) read from
    `ui_status_t { run, gate, env[3], seq_cursor[3], step[3] }`,
    refreshed ≤ 10 Hz via the atomic snapshot set by the host thread.
  - Waveform labels cycle the 6 waves per PLAN §5 (sine, tri, saw,
    square, white, pink). When a wave is a noise type its FREQ slider is
    dimmed (frequency is inert for noise).
- Sectioned pages (AMENDED, owner order, PLAN §8.2): every non-SEQ
  content page is a list of **labeled sections** — gray non-selectable
  label row, then the section's item rows **indented 20 px (2 spaces)
  right of the label**, then **one empty row** before the next section
  label. Labels/gaps are visual rows only; the cursor walks the page's
  flat item list. Item row pitch 16 px (PLAN §8.6). Page/section tables
  live in ui/seq_items.c (pure) and are asserted by
  tests/test_seq_items.c:
  - OSC sub-tab: **MAIN** FREQ · WAVE · DUTY · LEVEL · VEL, then
    **SUB 1** FMODE · FREQ · WAVE · DUTY · DEST · AMT, then
    **SUB 2** same (17 items).
  - ENV+FLT sub-tab: **ADSR** ATTACK · DECAY · SUSTAIN · RELEASE, then
    **FILTER** MODE · CUTOFF · RES · ENV · POLES (9 items).
  - SEQ sub-tab (step page, per chain; AMENDED, owner order): SINGLE
    COLUMN — one parameter per row, no side-by-side parameters. The page
    is DRIVEN BY the ui/seq_items.c item table: cursor index, item and
    drawn row are 1:1 (no separate hand mapping). Visual = cursor order:
    SEQ ON · DIV (integer 1..16, default 1) · MODE
    (fwd/pingpong/oddeven/random/markov) · ONCE (loop/once) · STEP row
    (the 8 step rectangles strip; LEFT/RIGHT selects the highlighted
    rectangle — markers per PLAN §8.2 (amended, PLAN_KEYMAP_REBIND §5):
    the SELECTED step gets a thick THEME_SEQ_SEL border (5 nested rects
    drawn outward), the step PLAYING NOW gets an orange dot
    (THEME_ORANGE) inside the cell, filled when vel > 0, empty when
    vel = 0) · PITCH · VEL
    (sliders bound to the selected rectangle — editing changes that
    step live). NO BPM here — the GLOBAL tempo (PLAN §6) is edited on
    the MASTER tab. **2 blank lines + a "Steps" label above the first
    cell** (owner order). UP/DOWN move the cursor (wraps);
    Z/X (desktop) / X/Y (R36S) edit; LEFT/RIGHT moves the rectangle
    selection from anywhere on the page.
  - MOD tab content (AMENDED, owner order — MIX renamed MOD): sections
    **MOD 1** · **MOD 2** · **MOD 3**, each SRC · DEST · AMT (9 items —
    the cross-mod matrix slots, dsp/mixer).
  - MASTER tab content (AMENDED, owner order — the FX tab is GONE; its
    parameters are MASTER sections): **MASTER** section — MASTER
    (percent 0..100) · SAFETY (boolean ON/OFF, default ON — master
    safety filter, dsp/fx) · **BPM (the GLOBAL tempo, integer 1..500,
    PLAN §6 — visible and edited HERE, not on the SEQ pages)**; then
    **DELAY** section — TIME L (seconds 0..2, 0.01 s steps) · FEEDBACK
    (percent 0..100) · TONE (LP cutoff Hz 20..20k, log 1/12-oct) · MIX
    (percent 0..100); then **REVERB** section — SIZE · DAMP · MIX ·
    WIDTH (percent 0..100). 11 items total.

- **Menu window (overlay on any screen)** per PLAN.md §8.4:
  - `MENU`: items {NEW, SAVE, LOAD, PANIC, EXIT, BACK}, cursor on BACK.
    Select: NEW→CONFIRM, SAVE→NAME, LOAD→LOAD, PANIC→execute (atomic
    flag, no sub-window), EXIT→QUIT_MSG, BACK→close to the screen the
    window was opened from.
  - `CONFIRM` (NEW): "RESET TO FACTORY DEFAULTS?" YES/NO; YES →
    `draft = OPHIS_DEFAULTS`, one mailbox commit, close to the screen the
    window was opened from.
  - `NAME` (SAVE): name field (1..32, charset per PLAN §9) + charset
    grid (desktop: keyboard input; R36S: 8×5 grid of A–Z 0–9 - _ SPC DEL
    — d-pad selects, X inserts, Y deletes). Save → `preset_save()`, error
    shown inline on failure.
  - `LOAD`: folder navigator (PLAN §8.4) — opens in the default preset
    dir, current folder path on the title line, `preset_list_dirs()`
    subdirectories (suffix "/", ".." to go up) + `preset_list()` *.json
    files alphabetically; selection wraps, the visible rows follow the
    selection (no scroll key); select → `preset_load()` into `draft`, one
    mailbox commit; error → inline message, stay on list.
  - `QUIT_MSG`: "QUITTING" (1 s) then main() returns; audio closed in
    main after the UI loop ends.
  - **Context remap** (menu window + its sub-windows): CONFIRM key =
    select (desktop Enter; handheld A via KB_RUN_CONFIRMS_MENU), TRIGGER
    cancels on the handheld only (KB_TRIGGER_CANCELS_MENU), Esc closes;
    implemented as one `in_menu()` switch in the key
    handler. NAV and value keys are inert in the menu windows (U4: they
    used to fall through to the window close — d-pad left/right closed
    menu windows on the R36S) except where a sub-window uses them (grid
    picker: NAV moves the grid cursor, X/Y insert/delete).
- Key handling: `SDL_PollEvent` → `keymap_action()` (ui/keymap.c) →
  `ui_action_t` → item edit / transport flag / preset call. While SDL
  text input is active (desktop SAVE name entry), only ESC passes through
  `keymap_action()`; every other keydown goes to `ui_handle_event()`
  (TEXTINPUT/BACKSPACE path) so typed characters are not double-inserted
  and RUN/TAB keys do not fire mid-name (U2).

```c
/* ui/keymap.h */
typedef enum { ACT_TRIGGER, ACT_RUN, ACT_SEL_UP, ACT_SEL_DOWN,
               ACT_NAV_LEFT, ACT_NAV_RIGHT, ACT_VAL_DEC, ACT_VAL_INC,
               ACT_TAB_NEXT, ACT_TAB_BACK, ACT_MENU,
               ACT_CONFIRM } ui_action_t;
int keymap_action(SDL_Event *e, ui_action_t *out);  /* 1 if mapped */
/* config.h supplies KB_TRIGGER, KB_RUN, KB_CONFIRM (desktop Enter /
   R36S -1 = unbound; ACT_CONFIRM = menu select, owner order
   2026-11-05), KB_RUN_CONFIRMS_MENU, KB_TRIGGER_CANCELS_MENU,
   KB_SEL_UP, KB_SEL_DOWN,
   KB_NAV_LEFT (desktop Left / R36S d-pad left),
   KB_NAV_RIGHT (desktop Right / R36S d-pad right),
   KB_VAL_DEC (desktop Z / R36S Y), KB_VAL_INC (desktop X / R36S X),
   KB_TAB_NEXT (desktop E / R36S R shoulder),
   KB_TAB_BACK (desktop Q / R36S L shoulder), KB_MENU
   (+ UI_SCALE, KB_FULLSCREEN) */
```

## 7. Presets (M3)

- JSON format: exactly per PLAN.md §9 (version 1, enum strings,
  fixed-length arrays, `run` excluded).
- All preset functions take explicit paths/dirs (testable); `main.c`
  resolves `presets/` beside the executable (via /proc/self/exe; no env
  var, no fallbacks) and creates the dir on first save (`mkdir -p`
  equivalent, `mkdir(2)` with EEXIST tolerated).
- `schema_parse_params` never leaves `*p` half-written on error (parse into
  a local, copy on success).

## 8. M0–M6 TDD sequencing (one subagent each, strictly sequential)

**M0 Spike & scaffold** (no TDD — spike):
1. `git init`, commit scaffold as we go.
2. Repo layout per PLAN.md §4; Makefile with all 4 targets (test target
   runs a 1-assert sanity harness → must print "1/1 passed").
3. `src/config.h` with BOTH `#if` branches (desktop final values incl.
   Esc=MENU; R36S branch = proposed bindings incl. Start=MENU,
   UI_SCALE=1, Select unassigned).
4. Dep check: `pkg-config --modversion sdl2 alsa` (desktop),
   `aarch64-linux-gnu-gcc --version`.
5. Static-link/container decision for glibc-2.28 → `BUILD_NOTES.md`.
6. Minimal `main.c`: SDL window (640×480×UI_SCALE, "OPHIS-DRONE OK" text
   via a temporary 8×8 stub font), ALSA open → 1 s silence → close.
7. `CHECKLIST_R36S.md`: on-device verification questions for the user
   (does SDL2 exist, video driver, `aplay -l` device list, glibc version,
   confirm button layout, deploy path, preset dir writability).
- DoD: `make` builds, runs on desktop, exits cleanly; `make test` = 1/1.

**M1 DSP core** (TDD):
1. Write `tests/harness.h` (assert macro, `N/M passed` reporter) +
   `test_osc.c`, `test_mod.c`, `test_adsr.c`, `test_filter.c`,
   `test_chain.c`, `test_mixer.c` per PLAN.md §10. Run → must fail.
2. Implement in dependency order: osc → subosc → adsr → filter → chain →
   mixer; after each module, that module's test file must be green before
   the next starts.
- DoD: all 6 files green, `make test` reports exact N/M, `-Wall -Wextra`
  clean.

**M2 Sequencer + transport** (TDD):
1. `test_sequencer.c`, `test_transport.c` (per PLAN.md §10 incl. one-shot
   gate open/close, RUN-off silence, enabled-off zero events, PANIC →
   instant gate 0 + state reset + run cleared) → fail.
2. Implement `seq/sequencer.c`, `seq/transport.c`.
- DoD: both green; total N/M reported.

**M3 Presets** (TDD):
1. `test_json.c`, `test_preset.c` per PLAN.md §10 (roundtrip, rejections,
   sanitization, temp-dir file ops) → fail.
2. Implement `preset/json.c`, `preset/preset.c`, `preset/defaults.c`.
- DoD: both green; `make test` total N/M reported.

**M4 ALSA engine**:
1. `audio/alsa_pcm.c` (incl. `alsa_xruns`), `audio/mailbox.c`
   (lock-free channels per §3), `audio/engine.c` per §3–§4.
2. `test_engine.c`: engine renders 1 s (44100 frames) via ALSA,
   non-silent output (RMS > 1e-4) when running, and
   **`alsa_xruns()` must be exactly 0** — the test fails on any xrun.
3. Desktop smoke: `main.c` runs the real window + real device; TRIGGER/RUN
   via T/Space from config.h (owner-ordered rebind 2026-11-05).
- DoD: test_engine green incl. measured xruns == 0; 30 s desktop run on
  the real device with **measured ALSA xruns == 0** (a non-zero count is a
  failed milestone — fix and re-run); report what was audible to the user
  (manual check, labeled).

**FX stage (owner-ordered, TDD, runs after M4 — inserted between M4 and M5)**:
1. `test_fx.c` per PLAN.md §10 (delay timing/feedback/tone, reverb
   tail/damp/width, safety corners + bypass bit-identity, MIX 0 dry,
   amended §5 order) → fail.
2. Implement `dsp/fx.h`/`dsp/fx.c` per §2; engine §4 block loop switched to
   the amended order (mixer ends at tanh; gate applied in fx_process);
   `synth_params_t.safety` added; defaults updated (delay/reverb wet 0 →
   FX silent by default, safety 1).
- DoD: test_fx green; all previous tests stay green (`make test` at or
  above the previous count).

**M5 UI**:
1. `ui/font8x8.c` data (95 glyphs) + `ui/keymap.c` + `ui/ui.c` tabbed UI
   per §6 (6 top tabs incl. FX, OSC sub-tabs OSC/SUB/FILT/SEQ incl. the SEQ step
   page) + the menu window on any tab (confirm, name entry both input
   modes, LOAD folder navigator, quit message, context remap).
2. No unit tests for UI (PLAN §10); desktop integration run: navigate all
   tabs and sub-tabs, edit values, save + reload a preset through the
   menu, verify mailbox commits (log param deltas for the smoke run).
- DoD: desktop run; every tab/sub-tab reachable; every item editable;
  preset round-trip verified on disk; R36S keymap path compiles (build
  with `-DTARGET_R36S` on desktop toolchain for compile check only —
  cross build is M6).

**M6 Handheld target & deploy**:
1. Finalize R36S bindings in config.h (using M0 checklist answers from the
   user).
2. Cross-build per BUILD_NOTES.md decision; `file` confirms aarch64;
   ldd/static status reported.
3. `deploy.sh`: copy binary + launcher script to a path the user runs from
   (EASYROMS or $HOME) — exact path from M0 checklist; the r36s zip also
   stages `presets/*.json` into `ports/ophis-drone/presets/`
   (PLAN_KEYMAP_REBIND §6).
4. On-device acceptance (user-executed, reported back): window size, audio
   out device + level, each button works, menu + save/load a preset on
   device, 10 min soak, xrun count from ALSA stats (the zero-xrun gate is
   desktop-only per PLAN §2; on-device count is reported as measured).
- DoD: binary runs on the R36S; checklist completed; measured xrun count
  reported.

## 9. Reporting contract (every milestone)

Report must contain: tests run N / total M (per file), which gates are
machine-verified vs manual (manual explicitly labeled as such), the
**measured desktop ALSA xrun count (must be 0 on any desktop run)**,
warnings (0 expected), and any deviation flagged (there should be none).
Never
claim a property "by construction" — only measured.
