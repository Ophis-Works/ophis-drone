/* src/ui/seq_items.c — the whole tab-reorg item model (PLAN.md
 * §8.2/§8.6 AMENDED, owner order). Pure data, no SDL.
 * Pages (flat item lists + section splits):
 *   OSC (sub-tab) : MAIN (FREQ · WAVE · DUTY · LEVEL · VEL) then
 *                   SUB 1 and SUB 2 (each FMODE · FREQ · WAVE · DUTY ·
 *                   DEST · AMT) = 17 — the ADSR items moved OFF this
 *                   page; NO dsp/param/model changes, same ids.
 *   ENV+FLT        : ADSR (ATTACK · DECAY · SUSTAIN · RELEASE) then
 *                   FILTER (MODE · CUTOFF · RES · ENV · POLES) = 9.
 *   SEQ            : SEQ ON · DIV · MODE · ONCE · STEP · PITCH · VEL
 *                   (7 items — NO BPM: the GLOBAL tempo lives on the
 *                   MASTER tab); the MODE and ONCE selectors are table
 *                   rows and MUST stay visible (regression-tested by
 *                   tests/test_seq_items.c).
 *   MOD            : SLOT · SOURCE · TARGET · DEST · DEPTH · LEVEL = 6
 *                   (PLAN_MOD_MATRIX_16.md R4 — SLOT selects the matrix
 *                   slot the five rows below edit).
 *   MASTER         : MASTER · SAFETY · BPM + DELAY (TIME L · FEEDBACK ·
 *                   TONE · MIX) + REVERB (SIZE · DAMP · MIX · WIDTH)
 *                   = 11 (the former FX tab merged in, owner order).
 * Section tables give each label's first flat index + item count; the
 * renderer draws one gray label row per section, its items indented,
 * and ONE empty 16 px row before the next section label. */
#include <stddef.h>
#include <stdio.h>
#include <math.h>

#include "seq_items.h"

/* ---- OSC sub-tab, OSC page: MAIN + SUB 1 + SUB 2 = 17 ------------- */

const item_t MAIN_ITEMS[MAIN_ITEMS_N] = {
  { "FREQ",  IT_OSC_FREQ, 0 }, { "WAVE",  IT_OSC_WAVE, 0 },
  { "DUTY",  IT_OSC_DUTY, 0 },
  { "LEVEL", IT_LEVEL,    0 }, { "VEL",   IT_VEL,      0 },
};

/* SUB 1 then SUB 2 (si 0 then 1) — names WITHOUT the old "SUB1 "/"SUB2 "
 * prefixes; the SUB 1/SUB 2 section labels carry the context */
/* subs are Spice/Subharmonicon voices (O2): DIV · WAVE · DUTY · LEVEL */
const item_t SUB_ITEMS[SUB_ITEMS_N] = {
  { "DIV",   IT_SUB_DIV,   0 }, { "WAVE",  IT_SUB_WAVE,  0 },
  { "DUTY",  IT_SUB_DUTY,  0 }, { "LEVEL", IT_SUB_LEVEL, 0 },
  { "DIV",   IT_SUB_DIV,   1 }, { "WAVE",  IT_SUB_WAVE,  1 },
  { "DUTY",  IT_SUB_DUTY,  1 }, { "LEVEL", IT_SUB_LEVEL, 1 },
};

const section_t OSC_SECTIONS[OSC_SECTIONS_N] = {
  { "MAIN",  0,  5 },
  { "SUB 1", 5,  4 },
  { "SUB 2", 9,  4 },
};

/* ---- ENV+FLT sub-tab: ADSR + FILTER = 9 --------------------------- */

/* the ADSR items that used to sit on the OSC page (owner order: they
 * live in the ENV+FLT ADSR section now) */
const item_t ADSR_ITEMS[ADSR_ITEMS_N] = {
  { "ATTACK",   IT_A, 0 }, { "DECAY",    IT_D, 0 },
  { "SUSTAIN",  IT_S, 0 }, { "RELEASE",  IT_R, 0 },
};

const item_t FILT_ITEMS[FILT_ITEMS_N] = {           /* order PLAN §8.2/§8.3 */
  { "MODE",   IT_FILT_MODE,  0 }, { "CUTOFF", IT_FILT_CUT,   0 },
  { "RES",    IT_FILT_RES,   0 }, { "ENV",    IT_FILT_ENV,   0 },
  { "POLES",  IT_FILT_POLES, 0 },
};

const section_t ENVFLT_SECTIONS[ENVFLT_SECTIONS_N] = {
  { "ADSR",   0, 4 },
  { "FILTER", 4, 5 },
};

/* ---- SEQ page: 7 rows, single column (AMENDED, owner order) ------- */

const item_t SEQ_ITEMS[SEQ_ITEMS_N] = {
  { "SEQ ON", IT_SEQ_ON,      0 },
  { "DIV",    IT_SEQ_DIV,     0 },
  { "MODE",   IT_SEQ_MODE,    0 },   /* fwd/pingpong/oddeven/random/
                                          markov (owner-ordered, PLAN §6) */
  { "ONCE",   IT_SEQ_ONCE,    0 },   /* loop/once (owner-ordered) */
  { "STEP",   IT_SEQ_STEPSEL, 0 },  /* LEFT/RIGHT selects the rectangle */
  { "PITCH",  IT_STEP_PITCH,  0 },   /* si resolved to seq_sel at use */
  { "VEL",    IT_STEP_VEL,    0 },
};

/* ---- MOD tab: SLOT · SOURCE · TARGET · DEST · DEPTH · LEVEL (R4) --- */

/* the 16-slot matrix (PLAN_MOD_MATRIX_16.md R4, owner order
 * 2026-11-09): ONE SLOT selector row (ITT_UI — ui.c binds the five
 * rows below to the selected slot at use time, PR-D; every si is 0
 * here). Labels are UI-only words (PLAN_OSC_NAMING.md R1/R5): SOURCE =
 * the mod source, TARGET = the routing target (values "OSC 1/2/3",
 * never raw 0/1/2), DEST, DEPTH = the amount, LEVEL = the slot's own
 * ADD knob. Preset keys stay raw
 * ("src"/"tgt"/"dest"/"amount"/"mix"). */
const item_t MOD_ITEMS[MOD_ITEMS_N] = {
  { "SLOT",   IT_MOD_SLOT,  0 },
  { "SOURCE", IT_XMOD_SRC,  0 }, { "TARGET", IT_XMOD_TGT,  0 },
  { "DEST",   IT_XMOD_DEST, 0 }, { "DEPTH",  IT_XMOD_AMT,  0 },
  { "LEVEL",  IT_XMOD_MIX,  0 },
};

const section_t MOD_SECTIONS[MOD_SECTIONS_N] = {
  { "MOD", 0, 6 },
};

/* ---- MASTER tab: MASTER · SAFETY · BPM + MIX + DELAY + REVERB ----- */

/* the GLOBAL tempo (PLAN §6) is edited HERE (owner order — never on the
 * SEQ pages); MIX A/B/C are the per-chain master sends (M3, si IS the
 * chain index); DELAY then REVERB are the former FX tab's items (owner
 * order, PLAN §8.2), merged into MASTER sections */
const item_t MASTER_ITEMS[MASTER_ITEMS_N] = {
  { "MASTER",   IT_MASTER,    0 },
  { "SAFETY",   IT_SAFETY,    0 },
  { "BPM",      IT_BPM,       0 },
  { "OSC A",    IT_MIXVOL,    0 }, { "OSC B", IT_MIXVOL, 1 },
  { "OSC C",    IT_MIXVOL,    2 },
  { "TIME L",   IT_DLY_TIMEL, 0 }, { "FEEDBACK", IT_DLY_FB,    0 },
  { "TONE",     IT_DLY_TONE,  0 }, { "MIX",      IT_DLY_MIX,   0 },
  { "SIZE",     IT_RVB_SIZE,  0 }, { "DAMP",     IT_RVB_DAMP,  0 },
  { "MIX",      IT_RVB_MIX,   0 }, { "WIDTH",    IT_RVB_WIDTH, 0 },
};

const section_t MASTER_SECTIONS[MASTER_SECTIONS_N] = {
  { "MASTER", 0, 3 },
  { "MIX",    3, 3 },
  { "DELAY",  6, 4 },
  { "REVERB", 10, 4 },
};

/* ---- S1 (PLAN_OPTIMIZATION.md O6): the display transform for the RAW
 * preset enum strings — UPPERCASE, '_' -> '.' — a UI concern, ONCE
 * here. buf may hold up to cap bytes; truncation is fine for labels. -- */
const char *ui_enum_display(const char *raw, char *buf, size_t cap)
{
    size_t i = 0;
    for (; raw && raw[i] != '\0' && i < cap - 1; i++)
        buf[i] = (raw[i] == '_') ? '.'
               : (raw[i] >= 'a' && raw[i] <= 'z')
                 ? (char)(raw[i] - 'a' + 'A') : raw[i];
    buf[i] = '\0';
    return buf;
}

/* ---- R3 (PLAN_OSC_NAMING.md): the MOD-page SOURCE display — the
 * FROZEN 10-entry source-name table (id order 0..9). The internal
 * A/B/C-letter forms ("A.OSC") are gone: ids 1..3 = chain B = OSC 2,
 * 4..6 = C = OSC 3, 7..9 = A = OSC 1 (A ids APPENDED, M2 — never
 * renumbered). Pure, SDL-free, testable. -- */
const char *ui_xsrc_display(int v, char *buf, size_t cap)
{
    static const char *const XSRC_NAMES[10] = {
        "OFF",
        "OSC 2", "OSC 2 SUB 1", "OSC 2 SUB 2",
        "OSC 3", "OSC 3 SUB 1", "OSC 3 SUB 2",
        "OSC 1", "OSC 1 SUB 1", "OSC 1 SUB 2",
    };
    const char *s = (v >= 0 && v <= 9) ? XSRC_NAMES[v] : "";
    snprintf(buf, cap, "%s", s);
    return buf;
}

/* ---- R0f (PLAN_OSC_NAMING.md): the MOD-page DEST display — the RAW
 * preset string uppercased with '_' -> ' ' ("flt_cut" -> "FLT CUT",
 * "flt_res" -> "FLT RES"). Same contract as ui_enum_display but with a
 * space separator; buf may hold up to cap bytes, truncation is fine
 * for labels. -- */
const char *ui_dest_display(const char *raw, char *buf, size_t cap)
{
    size_t i = 0;
    for (; raw && raw[i] != '\0' && i < cap - 1; i++)
        buf[i] = (raw[i] == '_') ? ' '
               : (raw[i] >= 'a' && raw[i] <= 'z')
                 ? (char)(raw[i] - 'a' + 'A') : raw[i];
    buf[i] = '\0';
    return buf;
}

/* seq mode / once option labels (owner-ordered modes amendment, PLAN §6) */
const char *SEQMODE_NAMES[SEQMODE_COUNT] =
  { "FWD", "PINGPONG", "ODDEVEN", "RANDOM", "MARKOV" };
const char *SEQONCE_NAMES[SEQONCE_COUNT] = { "LOOP", "ONCE" };

/* ---- PLAN_HZ_EDIT.md (R4/R5/R6, PR-G): the pure odometer helpers —
 * contracts documented in seq_items.h ---- */

int hz_digit_step(int centi_hz, int place_centi, int dir, int floor,
                  int ceil)
{
    int v = centi_hz + dir * place_centi;  /* already centi-Hz ints (PR-B) */
    if (v < floor) return ceil;            /* roll-over at the edges (R2) */
    if (v > ceil)  return floor;
    return v;
}

int hz_digits(int centi_hz, int digits[7])
{
    int v = centi_hz;                      /* exact centi-Hz in (R5) */
    if (v < 0)       v = 0;
    if (v > 2000000) v = 2000000;          /* display ceiling 20000.00 */
    for (int i = 6; i >= 0; i--) {         /* 7 digits: 10000s..0.01s */
        digits[i] = v % 10;
        v /= 10;
    }
    return digits[0] * 1000000 + digits[1] * 100000 + digits[2] * 10000 +
           digits[3] * 1000 + digits[4] * 100 + digits[5] * 10 + digits[6];
}

/* ---- item spec table (Phase 5 item 2): ONE entry per IT_* id, in id
 * order — {fmt, min, max, wrap, si-target, nav}. The single source of
 * every item's edit range, value format and NAV mode (PLAN §8.3,
 * PLAN_HZ_EDIT.md R12a); ui.c's item_value()/edit_item()/ACT_NAV drive
 * off this. Entries without an explicit nav zero-init to NAV_NONE (the
 * enum's first member). See seq_items.h for the field semantics. ---- */

const item_spec_t ITEM_SPECS[ITEM_SPECS_N] = {
  /* OSC MAIN section — freq bounds in the centi-Hz edit space
   * (PLAN_FREQ_DECIMALS.md R2/R4: 1..2000000 = 0.01..20000.00 Hz) */
  [IT_OSC_FREQ]   = { IT_OSC_FREQ,   ITF_HZ,      1, 2000000, 0, ITT_CHAIN , PK_CHAIN_FREQ , NAV_DIGITS },
  [IT_OSC_WAVE]   = { IT_OSC_WAVE,   ITF_WAVE,    0,    5, 1, ITT_CHAIN , PK_CHAIN_WAVE },
  [IT_OSC_DUTY]   = { IT_OSC_DUTY,   ITF_DUTY,    5,   95, 0, ITT_CHAIN , PK_CHAIN_DUTY },
  [IT_LEVEL]      = { IT_LEVEL,      ITF_PCT,     0,  100, 0, ITT_CHAIN , PK_CHAIN_LEVEL },
  [IT_VEL]        = { IT_VEL,        ITF_PCT,     0,  100, 0, ITT_CHAIN , PK_CHAIN_VEL },
  /* serial chain FM amount (owner order 2026-11-06): this chain's osc
   * output feeds the NEXT chain's carrier phase */
  /* ADSR section (ENV+FLT): A/D/R in the 0..50 pos ladder, S percent */
  [IT_A]          = { IT_A,          ITF_SEC,     0,   50, 0, ITT_CHAIN , PK_ADSR_A },
  [IT_D]          = { IT_D,          ITF_SEC,     0,   50, 0, ITT_CHAIN , PK_ADSR_D },
  [IT_S]          = { IT_S,          ITF_PCT,     0,  100, 0, ITT_CHAIN , PK_ADSR_S },
  [IT_R]          = { IT_R,          ITF_SEC,     0,   50, 0, ITT_CHAIN , PK_ADSR_R },
  /* SUB 1 / SUB 2 sections */
  [IT_SUB_DIV]    = { IT_SUB_DIV,    ITF_DIV,     1,   16, 1, ITT_SUB , PK_SUB_DIV },
  [IT_SUB_WAVE]   = { IT_SUB_WAVE,   ITF_WAVE,    0,    5, 1, ITT_SUB , PK_SUB_WAVE },
  [IT_SUB_DUTY]   = { IT_SUB_DUTY,   ITF_DUTY,    5,   95, 0, ITT_SUB , PK_SUB_DUTY },
  [IT_SUB_LEVEL]  = { IT_SUB_LEVEL,  ITF_PCT,     0,  100, 0, ITT_SUB , PK_SUB_LEVEL },
  /* FILTER section (ENV+FLT) */
  [IT_FILT_MODE]  = { IT_FILT_MODE,  ITF_FILT,    0,    3, 1, ITT_CHAIN , PK_CHAIN_FMODE },
  [IT_FILT_CUT]   = { IT_FILT_CUT,   ITF_HZ,   2000, 2000000, 0, ITT_CHAIN , PK_CHAIN_FC , NAV_DIGITS },
  [IT_FILT_RES]   = { IT_FILT_RES,   ITF_PCT,     0,  100, 0, ITT_CHAIN , PK_CHAIN_RES },
  [IT_FILT_ENV]   = { IT_FILT_ENV,   ITF_SPCT, -100,  100, 0, ITT_CHAIN , PK_CHAIN_ENV },
  [IT_FILT_POLES] = { IT_FILT_POLES, ITF_POLES,  12,   24, 0, ITT_CHAIN , PK_CHAIN_POLES },
  /* MOD tab */
  [IT_XMOD_SRC]   = { IT_XMOD_SRC,   ITF_XSRC,    0,    9, 1, ITT_XMOD , PK_XMOD_SRC },
  /* dest ladder gains FLT_RES (PLAN_OSC_NAMING.md R0e): max 6 -> 7 */
  [IT_XMOD_DEST]  = { IT_XMOD_DEST,  ITF_DEST,    0,    7, 1, ITT_XMOD , PK_XMOD_DEST },
  [IT_XMOD_AMT]   = { IT_XMOD_AMT,   ITF_AMT,  -100,  100, 0, ITT_XMOD , PK_XMOD_AMT },
  /* the slot's own ADD knob (owner order 2026-11-06): independent of
   * the dest — a slot can add AND modulate, or either alone */
  [IT_XMOD_MIX]   = { IT_XMOD_MIX,   ITF_PCT,     0,  100, 0, ITT_XMOD , PK_XMOD_MIX },
  /* the routing target shows the OSCILLATOR NAME ("OSC 1/2/3", R2):
   * ITF_TGT in ui_draw.c, not the raw 0/1/2 */
  [IT_XMOD_TGT]   = { IT_XMOD_TGT,   ITF_TGT,     0,    2, 1, ITT_XMOD , PK_XMOD_TGT },
  /* the MOD page SLOT row (PLAN_MOD_MATRIX_16.md R4/PR-C): UI state,
   * NAV_STEPSEL roll-over across the 16 slots 0..15 — the ONE
   * NAV_STEPSEL entry with those bounds (the SEQ STEPSEL stays 0..7) */
  [IT_MOD_SLOT]   = { IT_MOD_SLOT,   ITF_SLOT,    0,   15, 1, ITT_UI , PK_NONE , NAV_STEPSEL },
  [IT_MIXVOL]     = { IT_MIXVOL,     ITF_PCT,     0,  100, 0, ITT_MIXVOL , PK_CHAIN_MIXVOL },
  /* SEQ page */
  [IT_SEQ_ON]     = { IT_SEQ_ON,     ITF_ONOFF,   0,    1, 1, ITT_SEQ , PK_SEQ_ON },
  [IT_BPM]        = { IT_BPM,        ITF_INT,     1,  500, 0, ITT_TOP , PK_TOP_BPM },
  [IT_SEQ_DIV]    = { IT_SEQ_DIV,    ITF_XDIV,    1,   16, 0, ITT_SEQ , PK_SEQ_DIV },
  [IT_SEQ_MODE]   = { IT_SEQ_MODE,   ITF_SEQMODE, 0,    4, 1, ITT_SEQ , PK_SEQ_MODE },
  [IT_SEQ_ONCE]   = { IT_SEQ_ONCE,   ITF_SEQONCE, 0,    1, 1, ITT_SEQ , PK_SEQ_ONCE },
  [IT_SEQ_STEPSEL]= { IT_SEQ_STEPSEL,ITF_STEPSEL, 0,    7, 1, ITT_UI , PK_NONE , NAV_STEPSEL },
  [IT_STEP_PITCH] = { IT_STEP_PITCH, ITF_HZ,      1, 2000000, 0, ITT_STEP , PK_STEP_PITCH , NAV_DIGITS },
  [IT_STEP_VEL]   = { IT_STEP_VEL,   ITF_PCT,     0,  100, 0, ITT_STEP , PK_STEP_VEL },
  /* MASTER tab */
  [IT_MASTER]     = { IT_MASTER,     ITF_PCT,     0,  100, 0, ITT_TOP , PK_TOP_MASTER },
  [IT_SAFETY]     = { IT_SAFETY,     ITF_ONOFF,   0,    1, 1, ITT_TOP , PK_TOP_SAFETY },
  [IT_DLY_TIMEL]  = { IT_DLY_TIMEL,  ITF_CENTS,   0,  200, 0, ITT_DELAY , PK_DLY_TIMEL },
  [IT_DLY_FB]     = { IT_DLY_FB,     ITF_PCT,     0,  100, 0, ITT_DELAY , PK_DLY_FB },
  [IT_DLY_TONE]   = { IT_DLY_TONE,   ITF_TONE, 2000, 2000000, 0, ITT_DELAY , PK_DLY_TONE , NAV_DIGITS },
  [IT_DLY_MIX]    = { IT_DLY_MIX,    ITF_PCT,     0,  100, 0, ITT_DELAY , PK_DLY_MIX },
  /* reverb */
  [IT_RVB_SIZE]   = { IT_RVB_SIZE,   ITF_PCT,     0,  100, 0, ITT_REVERB , PK_RVB_SIZE },
  [IT_RVB_DAMP]   = { IT_RVB_DAMP,   ITF_PCT,     0,  100, 0, ITT_REVERB , PK_RVB_DAMP },
  [IT_RVB_MIX]    = { IT_RVB_MIX,    ITF_PCT,     0,  100, 0, ITT_REVERB , PK_RVB_MIX },
  [IT_RVB_WIDTH]  = { IT_RVB_WIDTH,  ITF_PCT,     0,  100, 0, ITT_REVERB , PK_RVB_WIDTH },
};

const item_spec_t *item_spec(int id)
{
    if (id < 0 || id >= IT_COUNT)
        return NULL;
    return &ITEM_SPECS[id];
}

/* ---- A1 (PLAN_OPTIMIZATION.md O4): the page model -------------------
 * (sub_tab_t lives in ui.h; the OSC rows index it — duplicate the
 * 0/1/2 order here as plain indices to keep this TU SDL-free) ------- */
enum { PG_OSC = 0, PG_ENVFLT = 1, PG_SEQ = 2 };
static const item_span_t SP_OSC[] = {
    { MAIN_ITEMS, MAIN_ITEMS_N }, { SUB_ITEMS, SUB_ITEMS_N }
};
static const item_span_t SP_ENVFLT[] = {
    { ADSR_ITEMS, ADSR_ITEMS_N }, { FILT_ITEMS, FILT_ITEMS_N }
};
static const item_span_t SP_SEQ[]  = { { SEQ_ITEMS, SEQ_ITEMS_N } };
static const item_span_t SP_MOD[]  = { { MOD_ITEMS, MOD_ITEMS_N } };
static const item_span_t SP_MASTER[] = { { MASTER_ITEMS, MASTER_ITEMS_N } };

static const page_t PAGES[][3] = {
    /* OSC tabs: sub-tab OSC | ENV+FLT | SEQ */
    [0] = { [PG_OSC]    = { SP_OSC, 2, OSC_SECTIONS, OSC_SECTIONS_N },
            [PG_ENVFLT] = { SP_ENVFLT, 2, ENVFLT_SECTIONS,
                             ENVFLT_SECTIONS_N },
            [PG_SEQ]    = { SP_SEQ, 1, NULL, 0 } },
    [1] = { [PG_OSC]    = { SP_OSC, 2, OSC_SECTIONS, OSC_SECTIONS_N },
            [PG_ENVFLT] = { SP_ENVFLT, 2, ENVFLT_SECTIONS,
                             ENVFLT_SECTIONS_N },
            [PG_SEQ]    = { SP_SEQ, 1, NULL, 0 } },
    [2] = { [PG_OSC]    = { SP_OSC, 2, OSC_SECTIONS, OSC_SECTIONS_N },
            [PG_ENVFLT] = { SP_ENVFLT, 2, ENVFLT_SECTIONS,
                             ENVFLT_SECTIONS_N },
            [PG_SEQ]    = { SP_SEQ, 1, NULL, 0 } },
    [3] = { [0] = { SP_MOD, 1, MOD_SECTIONS, MOD_SECTIONS_N } },
    [4] = { [0] = { SP_MASTER, 1, MASTER_SECTIONS, MASTER_SECTIONS_N } },
};

const page_t *page_get(int tab, int sub)
{
    if (tab < 0 || tab > 4) return NULL;
    if (tab < 3)
        return (sub >= 0 && sub < 3) ? &PAGES[tab][sub] : NULL;
    return &PAGES[tab][0];
}
