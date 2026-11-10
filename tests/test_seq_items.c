/* tests/test_seq_items.c — the
 * SEQ page item table: exactly 7 items; exact order SEQ ON · DIV · MODE ·
 * ONCE · STEP · PITCH · VEL — NO BPM (the GLOBAL tempo lives on the
 * MASTER tab); the MODE selector (fwd · pingpong · oddeven · random ·
 * markov) and the ONCE selector (loop · once) are PRESENT — the demanded
 * presence regression. ALSO asserts the tab-reorg pure model:
 * 5 top tabs (OSC 1|2|3, MOD, MASTER),
 * OSC sub-tabs OSC|ENV+FLT|SEQ, every page a flat item list split into
 * labeled sections — item tables MAIN/SUB/ADSR/FILT/MOD/MASTER plus the
 * section tables (label + first + count) that drive rendering. SDL-free
 * pure data: reads only src/ui/seq_items.h. */
#include <string.h>
#include "harness.h"
#include "ui/seq_items.h"

int main(void)
{
    /* ---- exactly 7 items (BPM moved OFF the SEQ pages) ---- */
    CHECK(SEQ_ITEMS_N == 7);

    /* ---- exact order: cursor index == item == drawn row.
     * si is 0 for every entry — PITCH/VEL si is bound to the selected
     * rectangle at use time by ui.c, not by the table. ---- */
    static const char *const NAMES[7] =
      { "SEQ ON", "DIV", "MODE", "ONCE", "STEP", "PITCH", "VEL" };
    static const int IDS[7] =
      { IT_SEQ_ON, IT_SEQ_DIV, IT_SEQ_MODE, IT_SEQ_ONCE,
        IT_SEQ_STEPSEL, IT_STEP_PITCH, IT_STEP_VEL };
    for (int i = 0; i < SEQ_ITEMS_N; i++) {
        CHECK(SEQ_ITEMS[i].name != NULL &&
              strcmp(SEQ_ITEMS[i].name, NAMES[i]) == 0);
        CHECK(SEQ_ITEMS[i].id == IDS[i]);
        CHECK(SEQ_ITEMS[i].si == 0);
    }

    /* ---- NO BPM anywhere on the SEQ page (neither name nor id) ---- */
    for (int i = 0; i < SEQ_ITEMS_N; i++) {
        CHECK(SEQ_ITEMS[i].name == NULL ||
              strcmp(SEQ_ITEMS[i].name, "BPM") != 0);
        CHECK(SEQ_ITEMS[i].id != IT_BPM);
    }

    /* ---- MODE selector PRESENT: 5 entries, exact content in order
     * fwd · pingpong · oddeven · random · markov ---- */
    CHECK(SEQMODE_COUNT == 5);
    static const char *const MODE_WANT[5] =
      { "FWD", "PINGPONG", "ODDEVEN", "RANDOM", "MARKOV" };
    for (int i = 0; i < SEQMODE_COUNT; i++)
        CHECK(SEQMODE_NAMES[i] != NULL &&
              strcmp(SEQMODE_NAMES[i], MODE_WANT[i]) == 0);

    /* ---- ONCE selector PRESENT: loop · once ---- */
    CHECK(SEQONCE_COUNT == 2);
    CHECK(SEQONCE_NAMES[0] != NULL && strcmp(SEQONCE_NAMES[0], "LOOP") == 0);
    CHECK(SEQONCE_NAMES[1] != NULL && strcmp(SEQONCE_NAMES[1], "ONCE") == 0);

    /* ==================================================================
     * TAB REORG: every page is a
     * flat item list split into labeled sections; the tables below are
     * the single source of truth for names, orders, item ids, si and
     * the section layout. The ADSR items moved OFF the OSC page into
     * ENV+FLT; SUB/MOD names lost their "SL1 "/"SUB1 " prefixes (the
     * section labels carry the context); the old FX tab's delay/reverb
     * items merged into MASTER. Same underlying params/ids — pure UI
     * reorg, NO dsp/param/model changes.
     * ================================================================== */

    /* ---- OSC sub-tab, OSC page: MAIN (5 — CHFM REMOVED) +
     * SUB 1/SUB 2 (4+4) = 13 ---- */
    CHECK(MAIN_ITEMS_N == 5);
    static const char *const MN5[5] =
      { "FREQ", "WAVE", "DUTY", "LEVEL", "VEL" };
    static const int    MI5[5] =
      { IT_OSC_FREQ, IT_OSC_WAVE, IT_OSC_DUTY, IT_LEVEL, IT_VEL };
    for (int i = 0; i < MAIN_ITEMS_N; i++) {
        CHECK(MAIN_ITEMS[i].name != NULL &&
              strcmp(MAIN_ITEMS[i].name, MN5[i]) == 0);
        CHECK(MAIN_ITEMS[i].id == MI5[i]);
        CHECK(MAIN_ITEMS[i].si == 0);
    }
    CHECK(MAIN_ITEMS[0].id == IT_OSC_FREQ);       /* demanded spot check */

    /* MAIN has NO ADSR items — they live on ENV+FLT */
    for (int i = 0; i < MAIN_ITEMS_N; i++) {
        CHECK(MAIN_ITEMS[i].id != IT_A && MAIN_ITEMS[i].id != IT_D &&
              MAIN_ITEMS[i].id != IT_S && MAIN_ITEMS[i].id != IT_R);
    }

    /* subs are Spice/Subharmonicon voices: DIV · WAVE · DUTY ·
     * LEVEL — no FMODE/FREQ/DEST/AMT */
    CHECK(SUB_ITEMS_N == 8);
    static const char *const SN8[8] =
      { "DIV", "WAVE", "DUTY", "LEVEL", "DIV", "WAVE", "DUTY", "LEVEL" };
    static const int SI8[8] =
      { IT_SUB_DIV, IT_SUB_WAVE, IT_SUB_DUTY, IT_SUB_LEVEL,
        IT_SUB_DIV, IT_SUB_WAVE, IT_SUB_DUTY, IT_SUB_LEVEL };
    for (int i = 0; i < SUB_ITEMS_N; i++) {
        CHECK(SUB_ITEMS[i].name != NULL &&
              strcmp(SUB_ITEMS[i].name, SN8[i]) == 0);
        CHECK(SUB_ITEMS[i].id == SI8[i]);
        CHECK(SUB_ITEMS[i].si == i / 4);          /* sub 1 (0) then sub 2 (1) */
    }

    /* ---- ENV+FLT page: ADSR (4) + FILTER (5) = 9 ---- */
    CHECK(ADSR_ITEMS_N == 4);
    static const char *const AN4[4] =
      { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
    static const int AI4[4] = { IT_A, IT_D, IT_S, IT_R };
    for (int i = 0; i < ADSR_ITEMS_N; i++) {
        CHECK(ADSR_ITEMS[i].name != NULL &&
              strcmp(ADSR_ITEMS[i].name, AN4[i]) == 0);
        CHECK(ADSR_ITEMS[i].id == AI4[i]);        /* demanded spot checks */
        CHECK(ADSR_ITEMS[i].si == 0);
    }

    CHECK(FILT_ITEMS_N == 5);
    static const char *const FN5[5] =
      { "MODE", "CUTOFF", "RES", "ENV", "POLES" };
    static const int FI5[5] =
      { IT_FILT_MODE, IT_FILT_CUT, IT_FILT_RES, IT_FILT_ENV, IT_FILT_POLES };
    for (int i = 0; i < FILT_ITEMS_N; i++) {
        CHECK(FILT_ITEMS[i].name != NULL &&
              strcmp(FILT_ITEMS[i].name, FN5[i]) == 0);
        CHECK(FILT_ITEMS[i].id == FI5[i]);
        CHECK(FILT_ITEMS[i].si == 0);
    }

    /* ---- MOD tab: SLOT · SOURCE · TARGET · DEST · DEPTH · LEVEL = 6.
     * the SLOT row selects the matrix slot 1..16 the five rows below
     * edit; TARGET = the slot's routing target; LEVEL = the slot's own
     * ADD knob; every si is 0 — resolved at use time ---- */
    CHECK(MOD_ITEMS_N == 6);
    static const char *const XN6[6] =
      { "SLOT", "SOURCE", "TARGET", "DEST", "DEPTH", "LEVEL" };
    static const int XI6[6] =
      { IT_MOD_SLOT, IT_XMOD_SRC, IT_XMOD_TGT, IT_XMOD_DEST,
        IT_XMOD_AMT, IT_XMOD_MIX };
    for (int i = 0; i < MOD_ITEMS_N; i++) {
        CHECK(MOD_ITEMS[i].name != NULL &&
              strcmp(MOD_ITEMS[i].name, XN6[i]) == 0);
        CHECK(MOD_ITEMS[i].id == XI6[i]);
        CHECK(MOD_ITEMS[i].si == 0);   /* the slot rides the ui state */
    }
    CHECK(MOD_ITEMS[1].id == IT_XMOD_SRC);        /* demanded spot check */

    /* ---- MASTER tab: MASTER (3) + MIX (3) + DELAY (4) + REVERB (4)
     * = 14 (MIX A/B/C: the per-chain master send — si IS the chain
     * index; the old FX tab's delay/reverb items live HERE now; the
     * GLOBAL tempo BPM is visible and edited on MASTER) ---- */
    CHECK(MASTER_ITEMS_N == 14);
    static const char *const TN14[14] =
      { "MASTER", "SAFETY", "BPM", "OSC A", "OSC B", "OSC C",
        "TIME L", "FEEDBACK", "TONE", "MIX",
        "SIZE", "DAMP", "MIX", "WIDTH" };
    static const int TI14[14] =
      { IT_MASTER,  IT_SAFETY,  IT_BPM,
        IT_MIXVOL,  IT_MIXVOL,  IT_MIXVOL,
        IT_DLY_TIMEL, IT_DLY_FB, IT_DLY_TONE, IT_DLY_MIX,
        IT_RVB_SIZE,  IT_RVB_DAMP, IT_RVB_MIX, IT_RVB_WIDTH };
    for (int i = 0; i < MASTER_ITEMS_N; i++) {
        CHECK(MASTER_ITEMS[i].name != NULL &&
              strcmp(MASTER_ITEMS[i].name, TN14[i]) == 0);
        CHECK(MASTER_ITEMS[i].id == TI14[i]);
        if (MASTER_ITEMS[i].id == IT_MIXVOL)
            CHECK(MASTER_ITEMS[i].si == i - 3);   /* si IS the chain */
        else
            CHECK(MASTER_ITEMS[i].si == 0);
    }
    CHECK(MASTER_ITEMS[2].id  == IT_BPM);         /* demanded spot checks */
    CHECK(MASTER_ITEMS[3].id  == IT_MIXVOL);
    CHECK(MASTER_ITEMS[6].id  == IT_DLY_TIMEL);
    CHECK(MASTER_ITEMS[10].id == IT_RVB_SIZE);

    /* ---- section tables: gray label rows, items indented, ONE 16 px
     * blank row before every next section label. first/count are
     * flat-item indices into the
     * page's item list; the cursor walks that flat list 0..N-1 (wrap),
     * skipping over labels/gaps visually. ---- */
    CHECK(OSC_SECTIONS_N == 3);
    static const char *const OSL[3] = { "MAIN", "SUB 1", "SUB 2" };
    /* subs are 4-item voices: 0, 5, 9, counts 5/4/4 */
    static const int OSF[3] = { 0, 5, 9 };
    static const int OSC[3] = { 5, 4, 4 };
    for (int s = 0; s < OSC_SECTIONS_N; s++) {
        CHECK(OSC_SECTIONS[s].label != NULL &&
              strcmp(OSC_SECTIONS[s].label, OSL[s]) == 0);
        CHECK(OSC_SECTIONS[s].first == OSF[s]);
        CHECK(OSC_SECTIONS[s].count == OSC[s]);
    }
    CHECK(OSC_SECTIONS[0].first + OSC_SECTIONS[0].count == OSC_SECTIONS[1].first);
    CHECK(OSC_SECTIONS[1].first + OSC_SECTIONS[1].count == OSC_SECTIONS[2].first);
    CHECK(OSC_SECTIONS[2].first + OSC_SECTIONS[2].count == MAIN_ITEMS_N + SUB_ITEMS_N);

    CHECK(ENVFLT_SECTIONS_N == 2);
    CHECK(ENVFLT_SECTIONS[0].label != NULL &&
          strcmp(ENVFLT_SECTIONS[0].label, "ADSR") == 0);
    CHECK(ENVFLT_SECTIONS[0].first == 0 && ENVFLT_SECTIONS[0].count == 4);
    CHECK(ENVFLT_SECTIONS[1].label != NULL &&
          strcmp(ENVFLT_SECTIONS[1].label, "FILTER") == 0);
    CHECK(ENVFLT_SECTIONS[1].first == 4 && ENVFLT_SECTIONS[1].count == 5);
    CHECK(ENVFLT_SECTIONS[1].first + ENVFLT_SECTIONS[1].count ==
          ADSR_ITEMS_N + FILT_ITEMS_N);

    CHECK(MOD_SECTIONS_N == 1);
    CHECK(MOD_SECTIONS[0].label != NULL &&
          strcmp(MOD_SECTIONS[0].label, "MOD") == 0);
    CHECK(MOD_SECTIONS[0].first == 0 && MOD_SECTIONS[0].count == 6);
    CHECK(MOD_SECTIONS[0].first + MOD_SECTIONS[0].count == MOD_ITEMS_N);

    CHECK(MASTER_SECTIONS_N == 4);
    static const char *const TSL[4] = { "MASTER", "MIX", "DELAY", "REVERB" };
    static const int TSF[4] = { 0, 3, 6, 10 };
    static const int TSC[4] = { 3, 3, 4, 4 };
    for (int s = 0; s < MASTER_SECTIONS_N; s++) {
        CHECK(MASTER_SECTIONS[s].label != NULL &&
              strcmp(MASTER_SECTIONS[s].label, TSL[s]) == 0);
        CHECK(MASTER_SECTIONS[s].first == TSF[s]);
        CHECK(MASTER_SECTIONS[s].count == TSC[s]);
    }
    CHECK(MASTER_SECTIONS[MASTER_SECTIONS_N - 1].first +
          MASTER_SECTIONS[MASTER_SECTIONS_N - 1].count == MASTER_ITEMS_N);

    /* ==================================================================
     * ITEM SPEC TABLE: ONE table keyed by IT_* id — {fmt, min, max,
     * wrap-flag, si-target} — the single source of every item's edit
     * range and value format. SDL-free pure data, like the rest of this
     * suite. ================================================================== */

    /* ---- completeness: every IT_* id has EXACTLY one entry, no
     * extras, and the accessor agrees ---- */
    CHECK(ITEM_SPECS_N == IT_COUNT);
    for (int id = 0; id < IT_COUNT; id++) {
        int n = 0;
        for (int i = 0; i < ITEM_SPECS_N; i++)
            if (ITEM_SPECS[i].id == id) n++;
        CHECK(n == 1);                        /* exactly one entry per id */
        const item_spec_t *sp = item_spec(id);
        CHECK(sp != NULL && sp->id == id);    /* accessor returns it */
        CHECK(sp >= ITEM_SPECS && sp < ITEM_SPECS + ITEM_SPECS_N);
    }
    for (int i = 0; i < ITEM_SPECS_N; i++)
        CHECK(ITEM_SPECS[i].id >= 0 && ITEM_SPECS[i].id < IT_COUNT);
    CHECK(item_spec(-1) == NULL);             /* out-of-range -> NULL */
    CHECK(item_spec(IT_COUNT) == NULL);

    /* ---- the frequency rows are
     * CENTI-HZ odometer boxes — the whole spec edit space moves to
     * centi-Hz ints (22000 = 220.00 Hz), so the bounds are expressed
     * there: OSC FREQ floor 0.01 Hz (LFO range — the fmt
     * documents the floor; implementer's choice pinned as min <= 100
     * centi-Hz) .. 20000.00; STEP PITCH shares the freq space and
     * floor. FILTER CUT and DELAY TONE keep their 20.00..20000.00
     * ranges = 2000..2000000 centi-Hz. ---- */
    CHECK(item_spec(IT_OSC_FREQ)->fmt == ITF_HZ &&
          item_spec(IT_OSC_FREQ)->min >= 1 &&
          item_spec(IT_OSC_FREQ)->min <= 100 &&
          item_spec(IT_OSC_FREQ)->max == 2000000 &&
          item_spec(IT_OSC_FREQ)->wrap == 0);
    CHECK(item_spec(IT_FILT_CUT)->fmt == ITF_HZ &&
          item_spec(IT_FILT_CUT)->min == 2000 &&
          item_spec(IT_FILT_CUT)->max == 2000000 &&
          item_spec(IT_FILT_CUT)->wrap == 0);
    CHECK(item_spec(IT_STEP_PITCH)->fmt == ITF_HZ &&
          item_spec(IT_STEP_PITCH)->min >= 1 &&
          item_spec(IT_STEP_PITCH)->min <= 100 &&
          item_spec(IT_STEP_PITCH)->max == 2000000 &&
          item_spec(IT_STEP_PITCH)->wrap == 0);
    /* sub DIV: subharmonic divider 1..16, wraps — the ratio/HZ
     * ladders are GONE from subs */
    const item_spec_t *sp_div = item_spec(IT_SUB_DIV);
    CHECK(sp_div->fmt == ITF_DIV && sp_div->min == 1 &&
          sp_div->max == 16 && sp_div->wrap == 1);
    CHECK(item_spec(IT_SUB_DIV)->tgt == ITT_SUB);
    CHECK(item_spec(IT_SUB_LEVEL)->fmt == ITF_PCT &&
          item_spec(IT_SUB_LEVEL)->min == 0 &&
          item_spec(IT_SUB_LEVEL)->max == 100 &&
          item_spec(IT_SUB_LEVEL)->wrap == 0);

    /* ---- ADSR: A/D/R pos 0..50 (0 s .. 60 s log); S is a
     * percent 0..100 ---- */
    static const int SEC_IDS[3] = { IT_A, IT_D, IT_R };
    for (int i = 0; i < 3; i++) {
        const item_spec_t *sp = item_spec(SEC_IDS[i]);
        CHECK(sp->fmt == ITF_SEC && sp->min == 0 && sp->max == 50 &&
              sp->wrap == 0);
    }
    CHECK(item_spec(IT_S)->fmt == ITF_PCT && item_spec(IT_S)->min == 0 &&
          item_spec(IT_S)->max == 100);

    /* ---- percents: integer 0..100, 1 pp steps, clamped ---- */
    static const int PCT_IDS[11] =
      { IT_LEVEL, IT_VEL, IT_FILT_RES, IT_STEP_VEL, IT_MASTER,
        IT_DLY_FB, IT_DLY_MIX, IT_RVB_SIZE, IT_RVB_DAMP, IT_RVB_MIX,
        IT_RVB_WIDTH };
    for (int i = 0; i < 11; i++) {
        const item_spec_t *sp = item_spec(PCT_IDS[i]);
        CHECK(sp->fmt == ITF_PCT && sp->min == 0 && sp->max == 100 &&
              sp->wrap == 0);
    }

    /* ---- ENV AMT: percent -100..+100 ---- */
    CHECK(item_spec(IT_FILT_ENV)->fmt == ITF_SPCT &&
          item_spec(IT_FILT_ENV)->min == -100 &&
          item_spec(IT_FILT_ENV)->max == 100 &&
          item_spec(IT_FILT_ENV)->wrap == 0);

    /* ---- AMT: widest table bound is ±100; the PITCH dest
     * narrows to ±50 at edit time (dest-dependent, amt helper) ---- */
    CHECK(item_spec(IT_XMOD_AMT)->fmt == ITF_AMT &&
          item_spec(IT_XMOD_AMT)->min == -100 &&
          item_spec(IT_XMOD_AMT)->max == 100);

    /* ---- DUTY: percent 5..95 ---- */
    CHECK(item_spec(IT_OSC_DUTY)->fmt == ITF_DUTY &&
          item_spec(IT_OSC_DUTY)->min == 5 &&
          item_spec(IT_OSC_DUTY)->max == 95);
    CHECK(item_spec(IT_SUB_DUTY)->fmt == ITF_DUTY &&
          item_spec(IT_SUB_DUTY)->min == 5 &&
          item_spec(IT_SUB_DUTY)->max == 95);

    /* ---- BPM: integer 1..500; SEQ DIV: integer 1..16 ---- */
    /* MIX VOLUME: percent 0..100 per chain */
    CHECK(item_spec(IT_MIXVOL)->fmt == ITF_PCT &&
          item_spec(IT_MIXVOL)->min == 0 &&
          item_spec(IT_MIXVOL)->max == 100 &&
          item_spec(IT_MIXVOL)->wrap == 0 &&
          item_spec(IT_MIXVOL)->tgt == ITT_MIXVOL);
    CHECK(item_spec(IT_BPM)->fmt == ITF_INT && item_spec(IT_BPM)->min == 1 &&
          item_spec(IT_BPM)->max == 500 && item_spec(IT_BPM)->wrap == 0);
    CHECK(item_spec(IT_SEQ_DIV)->fmt == ITF_XDIV &&
          item_spec(IT_SEQ_DIV)->min == 1 && item_spec(IT_SEQ_DIV)->max == 16 &&
          item_spec(IT_SEQ_DIV)->wrap == 0);

    /* ---- delay time: seconds 0..2 in 0.01 s steps
     * -> cents 0..200; TONE: integer-Hz
     * odometer box 20..20000 — the freq ladder pos 0..212 is gone ---- */
    CHECK(item_spec(IT_DLY_TIMEL)->fmt == ITF_CENTS &&
          item_spec(IT_DLY_TIMEL)->min == 0 &&
          item_spec(IT_DLY_TIMEL)->max == 200);
    CHECK(item_spec(IT_DLY_TONE)->fmt == ITF_TONE &&
          item_spec(IT_DLY_TONE)->min == 2000 &&
          item_spec(IT_DLY_TONE)->max == 2000000);
    /* the tone floor stays single-sourced beside the table (its unit
     * moves with the spec's centi-Hz space) */
    CHECK(TONE_MIN_HZ == 2000 &&
          item_spec(IT_DLY_TONE)->min == TONE_MIN_HZ);

    /* ---- wrap-flag selectors (labels cycle, they do not clamp);
     * IT_FILT_POLES is NOT here — 12/24 dB/oct is a direction-independent
     * toggle (ITF_POLES), not a wrap across min..max ---- */
    static const int WRAP_IDS[9] =
      { IT_OSC_WAVE, IT_SUB_WAVE, IT_FILT_MODE,
        IT_XMOD_SRC, IT_XMOD_TGT, IT_XMOD_DEST,
        IT_SEQ_MODE, IT_SEQ_ONCE, IT_SEQ_ON };
    for (int i = 0; i < 9; i++)
        CHECK(item_spec(WRAP_IDS[i])->wrap == 1);
    /* selector counts: 6 waves, 4 filt,
     * 10 xmod srcs, 13 dests, 5 modes, 2 once, 2 on; subs are voices */
    CHECK(item_spec(IT_OSC_WAVE)->min == 0 && item_spec(IT_OSC_WAVE)->max == 5);
    CHECK(item_spec(IT_SUB_WAVE)->min == 0 && item_spec(IT_SUB_WAVE)->max == 5);
    CHECK(item_spec(IT_FILT_MODE)->min == 0 && item_spec(IT_FILT_MODE)->max == 3);
    /* xmod src ladder extended: 10 sources —
     * off, B/C osc+subs (ids 1..6, unchanged), A osc+subs (7..9) */
    CHECK(item_spec(IT_XMOD_SRC)->min == 0 && item_spec(IT_XMOD_SRC)->max == 9);
    /* TGT: chain selector 0..2, wraps; fmt is
     * ITF_TGT (the value displays "OSC 1/2/3", not the raw 0/1/2) */
    CHECK(item_spec(IT_XMOD_TGT)->fmt == ITF_TGT &&
          item_spec(IT_XMOD_TGT)->min == 0 &&
          item_spec(IT_XMOD_TGT)->max == 2 &&
          item_spec(IT_XMOD_TGT)->wrap == 1 &&
          item_spec(IT_XMOD_TGT)->tgt == ITT_XMOD);
    /* dest ladder: "filter" renamed "flt_cut",
     * "flt_res" appended, then "flt_env" + "adsr_a/d/s/r" appended —
     * off/pitch/phase/amp/flt_cut/flt_res/ring/fm/flt_env/adsr_a/
     * adsr_d/adsr_s/adsr_r = 0..12 (ids 0..7 frozen, new ids appended) */
    CHECK(item_spec(IT_XMOD_DEST)->min == 0 && item_spec(IT_XMOD_DEST)->max == 12);
    CHECK(item_spec(IT_SEQ_MODE)->min == 0 && item_spec(IT_SEQ_MODE)->max == 4);
    CHECK(item_spec(IT_SEQ_ONCE)->min == 0 && item_spec(IT_SEQ_ONCE)->max == 1);
    CHECK(item_spec(IT_SEQ_ON)->min == 0 && item_spec(IT_SEQ_ON)->max == 1);
    CHECK(item_spec(IT_SAFETY)->wrap == 1 && item_spec(IT_SAFETY)->min == 0 &&
          item_spec(IT_SAFETY)->max == 1);
    /* poles toggle: 12 <-> 24 dB/oct */
    CHECK(item_spec(IT_FILT_POLES)->fmt == ITF_POLES &&
          item_spec(IT_FILT_POLES)->min == 12 &&
          item_spec(IT_FILT_POLES)->max == 24);
    /* STEP rectangle selection: UI state (no commit), wraps 0..7 */
    CHECK(item_spec(IT_SEQ_STEPSEL)->fmt == ITF_STEPSEL &&
          item_spec(IT_SEQ_STEPSEL)->tgt == ITT_UI &&
          item_spec(IT_SEQ_STEPSEL)->min == 0 &&
          item_spec(IT_SEQ_STEPSEL)->max == 7 &&
          item_spec(IT_SEQ_STEPSEL)->wrap == 1);

    /* ---- si-target spot checks: which slot the item's si indexes ---- */
    CHECK(item_spec(IT_SUB_DIV)->tgt == ITT_SUB);
    CHECK(item_spec(IT_XMOD_SRC)->tgt == ITT_XMOD);
    CHECK(item_spec(IT_STEP_PITCH)->tgt == ITT_STEP);
    CHECK(item_spec(IT_BPM)->tgt == ITT_TOP);
    CHECK(item_spec(IT_MASTER)->tgt == ITT_TOP);
    CHECK(item_spec(IT_SAFETY)->tgt == ITT_TOP);
    CHECK(item_spec(IT_DLY_TIMEL)->tgt == ITT_DELAY);
    CHECK(item_spec(IT_RVB_SIZE)->tgt == ITT_REVERB);
    CHECK(item_spec(IT_SEQ_ON)->tgt == ITT_SEQ);
    CHECK(item_spec(IT_OSC_FREQ)->tgt == ITT_CHAIN);
    CHECK(item_spec(IT_FILT_CUT)->tgt == ITT_CHAIN);

    /* ---- the page model — counts and
     * sections come from ONE place; coverage over every tab+sub ---- */
    for (int tab = 0; tab < 3; tab++) {
        const page_t *p0 = page_get(tab, 0);
        const page_t *p1 = page_get(tab, 1);
        const page_t *p2 = page_get(tab, 2);
        CHECK(p0 && p1 && p2);
        int n0 = 0, n1 = 0, n2 = 0;
        for (int k = 0; k < p0->nspans; k++) n0 += p0->spans[k].n;
        for (int k = 0; k < p1->nspans; k++) n1 += p1->spans[k].n;
        for (int k = 0; k < p2->nspans; k++) n2 += p2->spans[k].n;
        CHECK(n0 == MAIN_ITEMS_N + SUB_ITEMS_N);
        CHECK(n1 == ADSR_ITEMS_N + FILT_ITEMS_N);
        CHECK(n2 == SEQ_ITEMS_N);
        CHECK(p0->nsections == OSC_SECTIONS_N);
        CHECK(p1->nsections == ENVFLT_SECTIONS_N);
    }
    CHECK(page_get(3, 0)->spans[0].n == MOD_ITEMS_N &&
          page_get(3, 0)->nsections == MOD_SECTIONS_N);
    CHECK(page_get(4, 0)->spans[0].n == MASTER_ITEMS_N &&
          page_get(4, 0)->nsections == MASTER_SECTIONS_N);
    CHECK(page_get(5, 0) == NULL && page_get(0, 3) == NULL &&
          page_get(-1, 0) == NULL);

    /* ==================================================================
     * PINS
     * ================================================================== */

    /* ---- the MOD-page row labels stay UI-only
     * words — SLOT · SOURCE · TARGET · DEST · DEPTH · LEVEL (TARGET
     * was OSC before the 6-row SLOT model); the preset keys stay raw.
     * ---- */
    static const char *const XNL6[6] =
      { "SLOT", "SOURCE", "TARGET", "DEST", "DEPTH", "LEVEL" };
    for (int i = 0; i < MOD_ITEMS_N; i++) {
        CHECK(MOD_ITEMS[i].name != NULL &&
              strcmp(MOD_ITEMS[i].name, XNL6[i]) == 0);
        CHECK(MOD_ITEMS[i].si == 0);   /* the slot rides the ui state */
    }

    /* ---- the MASTER MIX rows read
     * "OSC A" / "OSC B" / "OSC C" at si 0/1/2 (chain A/B/C); the
     * section title "MIX" is UNCHANGED. ---- */
    CHECK(MASTER_ITEMS[3].name != NULL &&
          strcmp(MASTER_ITEMS[3].name, "OSC A") == 0);
    CHECK(MASTER_ITEMS[4].name != NULL &&
          strcmp(MASTER_ITEMS[4].name, "OSC B") == 0);
    CHECK(MASTER_ITEMS[5].name != NULL &&
          strcmp(MASTER_ITEMS[5].name, "OSC C") == 0);
    CHECK(MASTER_ITEMS[3].si == 0 && MASTER_ITEMS[4].si == 1 &&
          MASTER_ITEMS[5].si == 2);
    CHECK(strcmp(MASTER_SECTIONS[1].label, "MIX") == 0);  /* title unchanged */

    /* ---- the DEST ladder max is 12
     * (flt_env + adsr_a/d/s/r appended after the frozen 0..7); still a
     * 0-based wrapping selector. ---- */
    CHECK(item_spec(IT_XMOD_DEST)->min == 0);
    CHECK(item_spec(IT_XMOD_DEST)->max == 12);
    CHECK(item_spec(IT_XMOD_DEST)->wrap == 1);

    /* ---- the TGT item's fmt is
     * the NEW ITF_TGT (ui_draw.c formats "OSC %d", v+1); min 0 / max 2
     * / wrap 1 / tgt ITT_XMOD unchanged. ---- */
    CHECK(item_spec(IT_XMOD_TGT)->fmt == ITF_TGT);
    CHECK(item_spec(IT_XMOD_TGT)->min == 0 &&
          item_spec(IT_XMOD_TGT)->max == 2 &&
          item_spec(IT_XMOD_TGT)->wrap == 1);
    CHECK(item_spec(IT_XMOD_TGT)->tgt == ITT_XMOD);

    /* ---- ui_xsrc_display — the
     * frozen 10-entry source-name table: ids 1..3 = chain B =
     * OSC 2, 4..6 = C = OSC 3, 7..9 = A = OSC 1 (A ids APPENDED);
     * id 0 = OFF. ---- */
    {
        char nb[32];
        ui_xsrc_display(0, nb, (int)sizeof nb);
        CHECK(strcmp(nb, "OFF") == 0);
        ui_xsrc_display(1, nb, (int)sizeof nb);
        CHECK(strcmp(nb, "OSC 2") == 0);
        ui_xsrc_display(2, nb, (int)sizeof nb);
        CHECK(strcmp(nb, "OSC 2 SUB 1") == 0);
        ui_xsrc_display(6, nb, (int)sizeof nb);
        CHECK(strcmp(nb, "OSC 3 SUB 2") == 0);
        ui_xsrc_display(7, nb, (int)sizeof nb);
        CHECK(strcmp(nb, "OSC 1") == 0);
        ui_xsrc_display(9, nb, (int)sizeof nb);
        CHECK(strcmp(nb, "OSC 1 SUB 2") == 0);
    }

    /* ---- the pure odometer helpers
     * move to CENTI-HZ integers (22000 = 220.00 Hz). hz_digits splits
     * into SEVEN digits — digits[0] = 10000s place .. digits[4] = 1s,
     * digits[5] = 0.1s, digits[6] = 0.01s ("00055.50" for 5550);
     * hz_digit_step(centi_hz, place_centi, dir, floor, ceil) — value ±
     * place with TRUE odometer carry and roll-over at [floor, ceil]
     * (the step pins hold in both spaces — pure odometer arithmetic;
     * the splitter is the new contract). ---- */
    {
        int d[7];
        hz_digits(5550, d);                    /* 00055.50 (55.50 Hz) */
        CHECK(d[0] == 0 && d[1] == 0 && d[2] == 0 &&
              d[3] == 5 && d[4] == 5 && d[5] == 5 && d[6] == 0);

        CHECK(hz_digit_step(22000, 100, +1, 1, 2000000) == 22100);
        /* carry through the 10s digit lands exactly on the ceiling */
        CHECK(hz_digit_step(1999000, 1000, +1, 1, 2000000) == 2000000);
        /* FREQ floor 0.01 Hz (1 centi-Hz): rolling below -> 20000.00 */
        CHECK(hz_digit_step(1, 1, -1, 1, 2000000) == 2000000);
        /* ceiling 20000.00 +0.01 -> the FREQ floor 0.01 */
        CHECK(hz_digit_step(2000000, 1, +1, 1, 2000000) == 1);
        /* CUT/TONE floor 20.00 Hz (2000 centi-Hz): rolling below ->
         * 20000.00 */
        CHECK(hz_digit_step(2000, 1, -1, 2000, 2000000) == 2000000);
    }

    /* ==================================================================
     * PIN — the MOD page is SIX items —
     * SLOT · SOURCE · TARGET · DEST · DEPTH · LEVEL in that order.
     * The five rows below SLOT edit the slot the SLOT row selects
     * (the slot rides the ui state at
     * use time, exactly like SEQ PITCH/VEL — si is 0 in the table).
     * The SLOT spec rolls 0..15 (NAV_STEPSEL bounds, ui_t-
     * owned -> tgt ITT_UI; the display shows SLOT n = value + 1, like
     * ITF_TGT).
     * ================================================================== */
    CHECK(MOD_ITEMS_N == 6);
    {
        static const char *const M6N[6] =
          { "SLOT", "SOURCE", "TARGET", "DEST", "DEPTH", "LEVEL" };
        for (int i = 0; i < 6; i++) {
            CHECK(MOD_ITEMS[i].name != NULL &&
                  strcmp(MOD_ITEMS[i].name, M6N[i]) == 0);
            CHECK(MOD_ITEMS[i].si == 0);   /* slot resolved at use time */
        }
        CHECK(MOD_ITEMS[1].id == IT_XMOD_SRC);
        CHECK(MOD_ITEMS[2].id == IT_XMOD_TGT);
        CHECK(MOD_ITEMS[3].id == IT_XMOD_DEST);
        CHECK(MOD_ITEMS[4].id == IT_XMOD_AMT);
        CHECK(MOD_ITEMS[5].id == IT_XMOD_MIX);
        /* the SLOT spec: the ONE NAV_STEPSEL entry bounded 0..15 that
         * wraps (the SEQ STEPSEL stays 0..7); the SLOT row drives it.
         * No new id is named here — the scan keys on the contract
         * (nav + bounds). */
        int slot_id = -1;
        for (int i = 0; i < ITEM_SPECS_N; i++)
            if (ITEM_SPECS[i].nav == NAV_STEPSEL &&
                ITEM_SPECS[i].min == 0 && ITEM_SPECS[i].max == 15 &&
                ITEM_SPECS[i].wrap == 1) {
                CHECK(slot_id == -1);      /* exactly one such entry */
                slot_id = ITEM_SPECS[i].id;
            }
        CHECK(slot_id != -1);              /* the SLOT spec exists */
        CHECK(MOD_ITEMS[0].id == slot_id); /* the SLOT row drives it */
        CHECK(slot_id != -1 &&
              item_spec(slot_id)->tgt == ITT_UI);   /* ui_t-owned */
    }

    HARNESS_REPORT();
}
