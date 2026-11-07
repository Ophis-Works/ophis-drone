/* src/dsp/types.h — shared enums/types, no SDL/ALSA.
 * EXACTLY as IMPLEMENTATION.md §1. */
#ifndef OPHIS_TYPES_H
#define OPHIS_TYPES_H

typedef enum { WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_SQUARE,
               WAVE_WHITE, WAVE_PINK } wave_t;

typedef enum { MOD_OFF, MOD_PITCH, MOD_PHASE, MOD_AMP, MOD_FILTER,
               MOD_RING, MOD_FM, MOD_FRES } mod_dest_t;
/* MOD_PHASE (PM, approved plan change eacb368+fix): the modulator adds to
 * the carrier phase; amount -100..+100 percent maps linearly to ±180°
 * (±pi rad) of phase deviation. Used by sub dest and xmod dest alike.
 * MOD_RING (owner order 2026-11-06): ring modulation — carrier × source;
 * amount 0..100 = dry/wet mix (negative amount inverts the modulator
 * polarity). MOD_FM (owner order 2026-11-06): true operator FM — the
 * audio-rate source drives the carrier phase with DX-style index
 * amount/25 cycles (±4 cycles at amount 100). MOD_FRES (owner order
 * 2026-11-07, PLAN_OSC_NAMING.md R0c, appended after MOD_FM — ids 0..6
 * never renumbered): the source sweeps the target's RESONANCE percent;
 * amount ±100. The per-slot mixing ADD function is the xmod mix knob
 * (xmod_params_t.mix), not a dest — the stage-3 MOD_MIX dest is GONE
 * from the format (PLAN_MOD_ROUTING.md M4b/D4, "new format only"). */

typedef enum { FILT_LP, FILT_HP, FILT_BP, FILT_BR } filt_mode_t;

typedef struct { float a, d; int s; float r; } adsr_params_t;
/* a,d,r: seconds (0..60, log/exp in UI); s: percent 0..100 */

/* Sub-oscillator, Spice/Subharmonicon style (owner order 2026-11-07,
 * PLAN_OPTIMIZATION.md O2): the sub plays parent_f / div (integer
 * subharmonic divider 1..16), has its own wave + square duty, and a
 * LEVEL (percent 0..100) mixing its audio into the chain's oscillator
 * output — level 0 = inaudible. Subs are NOT modulators: their mod
 * role lives in the MOD matrix (src ids x.sub1/x.sub2). */
typedef struct {
  int div;                 /* subharmonic divider 1..16 (parent_f / div) */
  wave_t wave;
  int duty;                /* square pulse width, percent 5..95, default 50
                              (ignored for non-square waves) */
  int level;               /* percent 0..100 into the chain's OSC SUM */
} subosc_params_t;

typedef struct {
  float freq_hz;            /* oscillator frequency parameter (Hz) */
  wave_t wave;
  int duty;                /* square pulse width, percent 5..95, default 50 */
  subosc_params_t sub[2];
  adsr_params_t adsr;
  filt_mode_t fmode; float fc;
  int res; int envamt; int poles; /* res: percent 0..100 -> Q 0.5..20 log;
                              envamt: percent -100..+100 (chain ADSR -> cutoff);
                              poles: 12 or 24 (dB/oct, biquad cascade) */
  int level; int vel;      /* percent 0..100; vel = retrigger level for
                              TRIGGER and direct mode (PLAN §7) */
  int mixvol;              /* percent 0..100, default 100 (owner order
                              2026-11-06, PLAN_MOD_ROUTING.md M3): the
                              chain's MASTER send — scales the post-
                              ADSR/FILTER/LEVEL signal into the master
                              sum; NEVER touches modulation (a muted
                              chain still drives mod slots as a source) */
} chain_params_t;

/* cross-mod source ids: 0=OFF, 1=B.osc 2=B.sub1 3=B.sub2
                          4=C.osc 5=C.sub1 6=C.sub2,
                          7=A.osc 8=A.sub1 9=A.sub2 (PLAN_MOD_ROUTING.md
                          M2 — ids 1..6 never renumbered; A appended) */
typedef struct {
  int src; int dest; float amount;
  int mix;                /* percent 0..100 (owner order 2026-11-06):
                             the source's AUDIO blended into A —
                             independent of dest, so a slot can add,
                             modulate, or both; 0 = off */
  int tgt;                /* target chain 0..2 (owner order 2026-11-06,
                             PLAN_MOD_ROUTING.md M1): which chain the
                             slot modulates and adds into; 0 = A
                             (compat default) */
} xmod_params_t;

typedef struct { float pitch_hz; int vel; } seq_step_t;
/* vel: percent 0..100; vel 0 = silent step (there is no active flag) */

typedef struct {
  int on; int div; int mode; int once; seq_step_t steps[8];
  /* div: clock divider integer 1..16, default 1 (step period =
     60/bpm/4 * div with bpm the GLOBAL tempo, PLAN §6; UI-clamped, so
     only 1..16 are valid inputs and only those are specified)
     mode: step order (PLAN §6): 0=fwd (default), 1=pingpong, 2=oddeven,
     3=random, 4=markov. once: 0=loop (default), 1=once (one pass through
     the order, then silent until restarted — PLAN §6).
     BPM is NOT per-seq (AMENDED, owner order, PLAN §6): the GLOBAL tempo
     is synth_params_t.bpm, passed to seq_tick by the caller. */
} seq_params_t;

typedef struct { float time_l, time_r; int feedback, tone, mix; } delay_params_t;
/* time_l/time_r: seconds 0..2; feedback/mix: percent 0..100;
 * tone: Hz 20..20000 (LP in the delay feedback loop). Real DSP since the
 * owner-ordered FX amendment (PLAN §2/§5). */

typedef struct { int size, damp, mix, width; } reverb_params_t;
/* all percent 0..100. Real DSP since the owner-ordered FX amendment. */

/* ---- shared clamps (Phase 5 item 3, PLAN_CODE_REVIEW_FIXES.md): the
 * one int/double clamp for dsp + ui (previously duplicated in chain.c,
 * fx.c and ui.c). ---- */
static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline double clampd(double v, double lo, double hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

typedef struct {
  chain_params_t chain[3];
  seq_params_t   seq[3];
  xmod_params_t  xmod[3];
  delay_params_t delay;        /* master-path delay (PLAN §5 amended) */
  reverb_params_t reverb;      /* master-path reverb (PLAN §5 amended) */
  int            bpm;         /* GLOBAL tempo 1..500 (AMENDED, owner
                                 order: one BPM for the whole synth,
                                 shared by all sequencers — PLAN §6;
                                 NOT in seq_params_t) */
  int            master;      /* percent 0..100 */
  int            safety;      /* master safety filter: 1 = ON (default,
                                 HP 60 Hz + LP 18 kHz cascaded biquads),
                                 0 = bypass (owner-ordered FX amendment) */
  int            run;         /* transport */
} synth_params_t;

#endif /* OPHIS_TYPES_H */
