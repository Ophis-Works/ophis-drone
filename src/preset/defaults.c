/* src/preset/defaults.c — factory defaults, exactly per IMPLEMENTATION.md
 * §2: A 55.0 saw LP800 lvl100; B 55.5 square LP600 lvl50, sub1 ratio 0.5
 * sine -> PITCH +2.0 Hz; C 110.0 sine LP1200 lvl30 (A.1/D.3/S80/R1.0);
 * seqs off, steps {55.0,100}; bpm 60 GLOBAL (AMENDED, owner order,
 * PLAN §6 — top-level, NOT per-seq); xmod off; master 70; run 0;
 * filter res 10, envamt 0, poles 12 on every chain.
 * Approved plan change (eacb368+fix): duty 50 on every main osc and sub;
 * silent FX defaults — delay {time_l 0.0, time_r 0.0, feedback 0,
 * tone 20000, mix 0}; reverb {size 0, damp 0, mix 0, width 0}
 * (engine-ignored until the FX milestone). */
#include "defaults.h"

/* all six subs default to div 1, sine, level 0 = inaudible (O2) */
static const subosc_params_t SUB_OFF = {
    .div = 1, .wave = WAVE_SINE, .duty = 50, .level = 0
};

/* all 16 matrix slots default OFF (MOD_SLOTS, owner order 2026-11-09) */
static const xmod_params_t XMOD_OFF = {
    .src = 0, .dest = MOD_OFF, .amount = 0.0f, .mix = 0, .tgt = 0
};

const synth_params_t OPHIS_DEFAULTS = {
    .chain = {
        { .freq_hz = 55.0f, .wave = WAVE_SAW, .duty = 50,
          .sub = { SUB_OFF, SUB_OFF },
          .adsr = { .a = 0.05f, .d = 0.2f, .s = 100, .r = 0.5f },
          .fmode = FILT_LP, .fc = 800.0f, .res = 10, .envamt = 0, .poles = 12, .level = 100, .vel = 100,
          .mixvol = 100 },
        { .freq_hz = 55.5f, .wave = WAVE_SQUARE, .duty = 50,
          .sub = { SUB_OFF, SUB_OFF },
          .adsr = { .a = 0.05f, .d = 0.2f, .s = 100, .r = 0.5f },
          .fmode = FILT_LP, .fc = 600.0f, .res = 10, .envamt = 0, .poles = 12, .level = 50, .vel = 100,
          .mixvol = 100 },
        { .freq_hz = 110.0f, .wave = WAVE_SINE, .duty = 50,
          .sub = { SUB_OFF, SUB_OFF },
          .adsr = { .a = 0.1f, .d = 0.3f, .s = 80, .r = 1.0f },
          .fmode = FILT_LP, .fc = 1200.0f, .res = 10, .envamt = 0, .poles = 12, .level = 30, .vel = 100,
          .mixvol = 100 }
    },
    .seq = {
        { .on = 0, .div = 1, .mode = 0, .once = 0,
          .steps = { { 55.0f, 100 }, { 55.0f, 100 }, { 55.0f, 100 },
                     { 55.0f, 100 }, { 55.0f, 100 }, { 55.0f, 100 },
                     { 55.0f, 100 }, { 55.0f, 100 } } },
        { .on = 0, .div = 1, .mode = 0, .once = 0,
          .steps = { { 55.0f, 100 }, { 55.0f, 100 }, { 55.0f, 100 },
                     { 55.0f, 100 }, { 55.0f, 100 }, { 55.0f, 100 },
                     { 55.0f, 100 }, { 55.0f, 100 } } },
        { .on = 0, .div = 1, .mode = 0, .once = 0,
          .steps = { { 55.0f, 100 }, { 55.0f, 100 }, { 55.0f, 100 },
                     { 55.0f, 100 }, { 55.0f, 100 }, { 55.0f, 100 },
                     { 55.0f, 100 }, { 55.0f, 100 } } }
    },
    .xmod = { XMOD_OFF, XMOD_OFF, XMOD_OFF, XMOD_OFF,
              XMOD_OFF, XMOD_OFF, XMOD_OFF, XMOD_OFF,
              XMOD_OFF, XMOD_OFF, XMOD_OFF, XMOD_OFF,
              XMOD_OFF, XMOD_OFF, XMOD_OFF, XMOD_OFF },
    .delay = { .time_l = 0.0f, .time_r = 0.0f,
               .feedback = 0, .tone = 20000, .mix = 0 },
    .reverb = { .size = 0, .damp = 0, .mix = 0, .width = 0 },
    .bpm = 60,   /* GLOBAL tempo (AMENDED, owner order, PLAN §6) */
    .master = 70,
    .safety = 1,   /* master safety filter ON (owner-ordered FX amendment) */
    .run = 0
};
