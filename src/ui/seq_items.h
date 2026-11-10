/* src/ui/seq_items.h — the whole tab-reorg item model (PLAN.md §8.2/§8.6
 * AMENDED, owner order): pure data, no SDL — testable by
 * tests/test_seq_items.c. 5 top tabs (OSC 1|2|3, MOD, MASTER); OSC tabs
 * have sub-tabs OSC | ENV+FLT | SEQ. Every page is a FLAT item list
 * (cursor index == flat item index, wrap 0..N-1) split into labeled
 * sections for rendering: a gray non-selectable label row, its items
 * indented, ONE empty 16 px row before the next section label (none
 * after the last section). Labels/gaps are visual only.
 * Pages (item tables + section tables, both below):
 *   OSC sub-tab   : MAIN (5) + SUB 1 + SUB 2 (4+4) = 13 (subs are
 *                   Spice/Subharmonicon voices: DIV · WAVE · DUTY ·
 *                   LEVEL, PLAN_OPTIMIZATION.md O2).
 *   ENV+FLT       : ADSR (4) + FILTER (5) = 9.
 *   SEQ           : SEQ_ITEMS, 7 rows, single column — cursor, item and
 *                   drawn row are 1:1 (BPM is NOT here: the GLOBAL
 *                   tempo lives on the MASTER tab).
 *   MOD           : SLOT · SOURCE · TARGET · DEST · DEPTH · LEVEL = 6
 *                   (PLAN_MOD_MATRIX_16.md R4: one SLOT selector row
 *                   picks the matrix slot 1..16 the five rows below
 *                   edit — the slot rides the ui state at use time,
 *                   exactly like SEQ PITCH/VEL).
 *   MASTER        : MASTER · SAFETY · BPM + the old FX tab's DELAY and
 *                   REVERB items = 11.
 * Same underlying params/ids as before the reorg — pure UI reorg, NO
 * dsp/param/model changes. The IT_* item-id enum and item_t live here
 * too; all tables + the MODE/ONCE option-name arrays are defined in
 * seq_items.c. */
#ifndef OPHIS_SEQ_ITEMS_H
#define OPHIS_SEQ_ITEMS_H

/* item ids (shared by every table below); IT_COUNT is the sentinel the
 * item-spec table completeness test iterates (Phase 5 item 2) */
enum {
  IT_OSC_FREQ, IT_OSC_WAVE, IT_OSC_DUTY, IT_LEVEL, IT_VEL,
  IT_A, IT_D, IT_S, IT_R,
  IT_SUB_DIV, IT_SUB_WAVE, IT_SUB_DUTY, IT_SUB_LEVEL,
  IT_FILT_MODE, IT_FILT_CUT, IT_FILT_RES, IT_FILT_ENV, IT_FILT_POLES,
  IT_XMOD_SRC, IT_XMOD_DEST, IT_XMOD_AMT, IT_XMOD_MIX, IT_XMOD_TGT,
  IT_MOD_SLOT,                   /* MOD page SLOT row (PR-C): ITT_UI —
                                    * the selected matrix slot 0..15 */
  IT_SEQ_ON, IT_BPM, IT_SEQ_DIV, IT_SEQ_MODE, IT_SEQ_ONCE,
  IT_SEQ_STEPSEL,
  IT_STEP_PITCH, IT_STEP_VEL, IT_MASTER, IT_MIXVOL,
  /* delay/reverb (former FX tab; MASTER sections since the reorg) */
  IT_DLY_TIMEL, IT_DLY_FB, IT_DLY_TONE, IT_DLY_MIX,
  IT_RVB_SIZE, IT_RVB_DAMP, IT_RVB_MIX, IT_RVB_WIDTH,
  /* MASTER tab safety boolean (owner-ordered amendment) */
  IT_SAFETY,
  IT_COUNT
};

typedef struct { const char *name; int id; int si; } item_t;

/* one labeled section of a page: gray label row + `count` items starting
 * at flat-item index `first` (labels/gaps are visual only — the cursor
 * walks the page's flat item list) */
typedef struct { const char *label; int first; int count; } section_t;

/* ---- OSC sub-tab, OSC page: MAIN + SUB 1 + SUB 2 = 13 items ------- */
#define MAIN_ITEMS_N 5
extern const item_t MAIN_ITEMS[MAIN_ITEMS_N];
#define SUB_ITEMS_N 8
extern const item_t SUB_ITEMS[SUB_ITEMS_N];
#define OSC_SECTIONS_N 3
extern const section_t OSC_SECTIONS[OSC_SECTIONS_N];

/* ---- ENV+FLT sub-tab: ADSR + FILTER = 9 items -------------------- */
#define ADSR_ITEMS_N 4
extern const item_t ADSR_ITEMS[ADSR_ITEMS_N];
#define FILT_ITEMS_N 5
extern const item_t FILT_ITEMS[FILT_ITEMS_N];
#define ENVFLT_SECTIONS_N 2
extern const section_t ENVFLT_SECTIONS[ENVFLT_SECTIONS_N];

/* ---- SEQ page: 7 rows in the PLAN §8.2 order; si is 0 for every
 * entry (the PITCH/VEL si is bound to the selected rectangle at use
 * time by ui.c, not by the table) ----------------------------------- */
#define SEQ_ITEMS_N 7
extern const item_t SEQ_ITEMS[SEQ_ITEMS_N];

/* ---- MOD tab: SLOT · SOURCE · TARGET · DEST · DEPTH · LEVEL = 6
 * items (PLAN_MOD_MATRIX_16.md R4, owner order 2026-11-09): the SLOT
 * row picks the matrix slot (1..16 shown, ui_t-owned ITT_UI, PR-C) the
 * five rows below edit — every si is 0 in the TABLE, resolved to the
 * selected slot at use time by ui.c (PR-D, exactly like SEQ
 * PITCH/VEL); TARGET = the slot's routing target (PLAN_MOD_ROUTING.md
 * M1), LEVEL = the slot's own ADD knob ------------------------------- */
#define MOD_ITEMS_N 6
extern const item_t MOD_ITEMS[MOD_ITEMS_N];
#define MOD_SECTIONS_N 1
extern const section_t MOD_SECTIONS[MOD_SECTIONS_N];

/* ---- MASTER tab: MASTER · SAFETY · BPM + MIX · DELAY + REVERB
 * = 14 items (MIX A/B/C: the per-chain master send, M3 — si IS the
 * chain index; PLAN_MOD_ROUTING.md M3)
 * (the GLOBAL tempo — integer 1..500 — lives HERE, never on the SEQ
 * pages; the former FX tab's delay/reverb params are MASTER sections
 * since the reorg); regression-tested by tests/test_seq_items.c ------ */
#define MASTER_ITEMS_N 14
extern const item_t MASTER_ITEMS[MASTER_ITEMS_N];
#define MASTER_SECTIONS_N 4
extern const section_t MASTER_SECTIONS[MASTER_SECTIONS_N];

/* MODE selector (PLAN §6/§8.3): label cycling fwd · pingpong · oddeven ·
 * random · markov — a visible row of the SEQ page (presence regression,
 * tests/test_seq_items.c) */
#define SEQMODE_COUNT 5
extern const char *SEQMODE_NAMES[SEQMODE_COUNT];

/* ONCE selector (PLAN §6/§8.3): label cycling loop · once */
#define SEQONCE_COUNT 2
extern const char *SEQONCE_NAMES[SEQONCE_COUNT];

/* ---- item spec table (Phase 5 item 2, PLAN_CODE_REVIEW_FIXES.md;
 * ranges per PLAN §8.3): ONE table keyed by IT_* id that replaces the
 * parallel per-item switches of ui.c's item_value()/edit_item(). Every
 * item's edit range and value format lives HERE and only here; ui.c
 * drives both display and editing off it. SDL-free pure data,
 * regression-tested by tests/test_seq_items.c (completeness: every
 * IT_* id exactly one entry; range spot checks per §8.3). ---- */

/* value kind: the edit space min/max are expressed in + the display
 * format (one formatter switch on this in ui.c) */
typedef enum {
  ITF_HZ,        /* centi-Hz odometer box min..max (PLAN_FREQ_DECIMALS.md
                    R2/R4: 7 editable digits 10000..0.01 Hz, min/max in
                    centi-Hz; OSC FREQ / SEQ PITCH floor 0.01 = 1);
                    display zero-padded "%05d.%02d Hz" (R5) — the true
                    value may keep a preset-loaded fraction, which the
                    display shows exactly (R5 fractional rule) */
  ITF_WAVE,      /* wave selector, wraps; WAVE names */
  ITF_DUTY,      /* square pulse width percent (5..95); "%d %%" */
  ITF_PCT,       /* percent min..max; "%d %%" */
  ITF_SPCT,      /* signed percent; "%+d %%" */
  ITF_SEC,       /* ADSR seconds ladder pos 0..max; seconds display */
  ITF_DIV,       /* subharmonic divider 1..16, wraps; "/n" (O2) */
  ITF_DEST,      /* mod dest selector, wraps; dest names (editing a DEST
                    re-clamps the slot's AMT — PLAN §8.3) */
  ITF_TGT,       /* mod routing target selector 0..2, wraps; "OSC %d"
                    display (v+1) — PLAN_OSC_NAMING.md R2 */
  ITF_AMT,       /* mod amount: dest-dependent clamp (PITCH ±50, others
                    ±100 — amt helper in ui.c; min/max hold the widest
                    bound); "%+.0f Hz" / "%+.0f %%" by dest */
  ITF_FILT,      /* filter mode selector, wraps; filt names */
  ITF_POLES,     /* poles toggle min <-> max (12/24 dB/oct) */
  ITF_XSRC,      /* cross-mod source selector, wraps; xmod src names */
  ITF_ONOFF,     /* boolean ON/OFF, wraps */
  ITF_SEQMODE,   /* seq mode selector, wraps; SEQMODE_NAMES */
  ITF_SEQONCE,   /* seq once selector, wraps; SEQONCE_NAMES */
  ITF_STEPSEL,   /* SEQ rectangle selection: UI state (si-target ITT_UI,
                    no commit), wraps */
  ITF_SLOT,      /* MOD matrix slot selection: UI state (si-target
                    ITT_UI, no commit), wraps; "SLOT %d" display (v+1) —
                    PLAN_MOD_MATRIX_16.md R4 */
  ITF_INT,       /* plain integer min..max; "%d" (BPM) */
  ITF_XDIV,      /* clock divider integer; "/%d" */
  ITF_CENTS,     /* delay time in centiseconds (0.01 s steps); "%.2f s" */
  ITF_TONE       /* delay tone: centi-Hz odometer box min..max
                    (2000..2000000 = 20.00..20000.00 Hz, R4 keeps the
                    CUT/TONE 20 Hz floor); display "%05d.%02d Hz" */
} item_fmt_t;

/* si-target: which parameter slot the item's value lives in (the item's
 * si is the index within the slot where the slot is indexed) */
typedef enum {
  ITT_CHAIN,     /* draft.chain[ci] (si unused) */
  ITT_SUB,       /* draft.chain[ci].sub[si] */
  ITT_XMOD,      /* draft.xmod[si] (si IS the xmod slot) */
  ITT_MIXVOL,    /* draft.chain[si] mixvol (si IS the chain index) */
  ITT_SEQ,       /* draft.seq[ci] (si unused) */
  ITT_STEP,      /* draft.seq[ci].steps[si] (si = selected rectangle) */
  ITT_TOP,       /* synth-level: bpm / master / safety */
  ITT_DELAY,     /* draft.delay */
  ITT_REVERB,    /* draft.reverb */
  ITT_UI         /* UI-only state (seq_sel), not committed */
} item_tgt_t;

/* D3 (PLAN_OPTIMIZATION.md O4): WHICH member of the slot the item's
 * value lives in — param_get/param_set dispatch on this, replacing the
 * mirrored get/set switch ladders. */
typedef enum {
  PK_NONE,
  /* chain[ci] */
  PK_CHAIN_FREQ, PK_CHAIN_WAVE, PK_CHAIN_DUTY, PK_CHAIN_LEVEL,
  PK_CHAIN_VEL, PK_ADSR_A, PK_ADSR_D, PK_ADSR_S, PK_ADSR_R,
  PK_CHAIN_FMODE, PK_CHAIN_FC, PK_CHAIN_RES, PK_CHAIN_ENV, PK_CHAIN_POLES,
  PK_CHAIN_MIXVOL,               /* chain[si] — MIX A/B/C (si = chain) */
  /* chain[ci].sub[si] */
  PK_SUB_DIV, PK_SUB_WAVE, PK_SUB_DUTY, PK_SUB_LEVEL,
  /* xmod[si] */
  PK_XMOD_SRC, PK_XMOD_DEST, PK_XMOD_AMT, PK_XMOD_MIX, PK_XMOD_TGT,
  /* seq[ci] */
  PK_SEQ_ON, PK_SEQ_DIV, PK_SEQ_MODE, PK_SEQ_ONCE,
  /* seq[ci].steps[si] */
  PK_STEP_PITCH, PK_STEP_VEL,
  /* top level */
  PK_TOP_BPM, PK_TOP_MASTER, PK_TOP_SAFETY,
  /* delay / reverb */
  PK_DLY_TIMEL, PK_DLY_FB, PK_DLY_TONE, PK_DLY_MIX,
  PK_RVB_SIZE, PK_RVB_DAMP, PK_RVB_MIX, PK_RVB_WIDTH
} param_kind_t;

/* R12a (PLAN_HZ_EDIT.md): what LEFT/RIGHT do on a row — TABLE-DRIVEN
 * per-row navigation mode carried by the item spec; ui.c's ACT_NAV
 * branch and ui_draw's digit marker dispatch on this, never on item
 * ids, row positions or strings */
typedef enum {
  NAV_NONE,            /* LEFT/RIGHT inert (default — every spec entry
                          without an explicit nav is this) */
  NAV_STEPSEL,         /* move the SEQ step rectangle (wrap min..max) */
  NAV_DIGITS           /* move the digit cursor (ui_t.freq_digit, 0..6
                          from the left, clamped; reset to the 1s digit
                          — index 4, the first non-decimal digit — on
                          row entry); X/Z step the selected digit's
                          place */
} nav_mode_t;

typedef struct {
  int id;              /* IT_* id — exactly one entry per id */
  item_fmt_t fmt;      /* value kind: edit space + display format */
  int min, max;        /* inclusive edit range in the fmt's space (pos
                          space for ladder fmts; selectors wrap across
                          min..max) */
  int wrap;            /* 1 = selector wraps across min..max, 0 = clamps */
  item_tgt_t tgt;      /* si-target: which param slot the value lives in */
  param_kind_t kind;   /* D3: which member of the slot (param_get/set) */
  nav_mode_t nav;      /* R12a: LEFT/RIGHT behavior of the row (zero =
                          NAV_NONE, so non-nav entries may omit it) */
} item_spec_t;

/* ladder bounds that are properties of the ladders themselves (kept
 * beside the table so every range is single-sourced here; the unit
 * follows the spec's centi-Hz edit space — PLAN_FREQ_DECIMALS.md R4:
 * 20.00 Hz = 2000) */
#define TONE_MIN_HZ 2000       /* delay tone floor (§8.3: 20 Hz..20 kHz;
                                * also IT_DLY_TONE's spec min below) */

/* ---- PLAN_HZ_EDIT.md (R4/R5/R6, PR-G) amended by PLAN_FREQ_DECIMALS.md
 * (R1/R2/R4/R5): the pure odometer helpers behind the frequency digit
 * boxes (OSC FREQ, FILTER CUT, DELAY TONE, SEQ PITCH). SDL-free,
 * unit-tested by tests/test_seq_items.c. All values are CENTI-HZ
 * integers (1 Hz = 100; 20000.00 Hz = 2000000 — exact up to the
 * ceiling, PR-B).
 *
 * hz_digits splits the value into exactly SEVEN zero-padded digits,
 * digits[0] = 10000s place .. digits[4] = 1s place, digits[5] = 0.1s,
 * digits[6] = 0.01s ("00055.50" for 5550 = 55.50 Hz; the period between
 * digits[4] and digits[5] is a fixed separator, not a digit).
 *
 * hz_digit_step is one odometer step: value + dir*place (TRUE carry —
 * stepping the 10s digit over 9->0 borrows through the arithmetic), and
 * ROLL-OVER at the range edges (R2): a result below floor returns ceil,
 * above ceil returns floor (20000.00 +0.01 on the 0.01s digit -> the
 * row's floor; OSC FREQ / SEQ PITCH floor 0.01 -> 20000.00; CUT/TONE
 * keep their 20.00 floor, R2/R4). Integer centi-Hz steps only.
 *
 * R5 (the fractional rule): a preset-loaded fractional value (e.g.
 * 55.5 Hz — the schema floor is now 0.01) is DISPLAYED exactly
 * ("00055.50") and KEEPS its fractional part in the DSP until the user
 * edits the row; the FIRST digit edit snaps the value through centi-Hz
 * integers and proceeds from there (55.5 +1 on the 1s digit -> 5550 +
 * 100 = 5650 -> 56.50). The snapping happens at the edit site (ui.c
 * edit_item, which owns the double value); these helpers take/return
 * centi-Hz ints. The OSC FREQ / SEQ PITCH edit floor is 0.01 Hz (R2 —
 * the LFO range). ---- */
int hz_digit_step(int centi_hz, int place_centi, int dir, int floor,
                  int ceil);
int hz_digits(int centi_hz, int digits[7]);

#define ITEM_SPECS_N (IT_COUNT)
extern const item_spec_t ITEM_SPECS[ITEM_SPECS_N];

/* the entry for an IT_* id (NULL for an out-of-range id) */
const item_spec_t *item_spec(int id);

/* ---- A1 (PLAN_OPTIMIZATION.md O4): the page model — a page is a flat
 * item list built from one or more item SPANS plus its section table.
 * ui.c's content_count/content_item become lookups; the layout lives
 * HERE only. ---- */
typedef struct { const item_t *items; int n; } item_span_t;
typedef struct {
  const item_span_t *spans;        /* concatenated flat item list */
  int nspans;
  const section_t *sections;       /* labels over the flat indices */
  int nsections;
} page_t;
const page_t *page_get(int tab, int sub);

/* S1: the display form (UPPERCASE, '_' -> '.') of a RAW preset enum
 * string — the transform lives HERE, never in the preset layer */
const char *ui_enum_display(const char *raw, char *buf, size_t cap);   /* NULL for non-OSC sub on
                                               MOD/MASTER (sub ignored) */

/* MOD-page displays (PLAN_OSC_NAMING.md R2/R3, pure, SDL-free):
 * ui_xsrc_display — the FROZEN source-name table for the xmod src id
 * 0..9 (ids 1..3 = chain B = OSC 2, 4..6 = C = OSC 3, 7..9 = A = OSC 1,
 * A appended in M2); ui_dest_display — the UPPERCASE, '_' -> ' ' form
 * of a RAW dest string ("flt_cut" -> "FLT CUT"). Both write buf (cap
 * bytes) and return it; truncation is fine for labels. */
const char *ui_xsrc_display(int v, char *buf, size_t cap);
const char *ui_dest_display(const char *raw, char *buf, size_t cap);

#endif /* OPHIS_SEQ_ITEMS_H */
