/* src/ui/ui.c — SDL2 UI STATE MACHINE (PLAN §8 amended, IMPLEMENTATION.md
 * §6; X3, PLAN_CODE_REVIEW_FIXES.md Phase 8: the file was split — the
 * menu/dialog logic lives in ui/ui_menu.c, the rendering in
 * ui/ui_draw.c, their private interface in ui/ui_internal.h; this TU
 * keeps tabs/focus/cursor navigation, the value-edit pipeline and the
 * event routing, no behavior change).
 * 5 top tabs (OSC 1 | OSC 2 | OSC 3 | MOD | MASTER; Q/E desktop, L/R
 * shoulder R36S cycle, wrap), OSC tabs with sub-tabs OSC|ENV+FLT|SEQ,
 * focus model per the amended PLAN §8.2 binding text:
 *   - arrows START on the sub-tab row (OSC tabs); LEFT/RIGHT there cycles
 *     sub-tabs; DOWN enters content; UP from the top of content returns
 *     to the sub-tab row; MOD/MASTER have no sub-tab row (content only).
 *   - in content: UP/DOWN move the cursor (wrap), Z/X (desktop) / X/Y (R36S)
 *     edit; LEFT/RIGHT = navigation only (sub-tab row, SEQ step rectangles).
 *   - cursor/focus persist per tab+sub-tab.
 * Pages are FLAT item lists (the IT_* ids, item_t, the item tables and
 * the section tables all live in ui/seq_items.{h,c} — pure data,
 * regression-tested by tests/test_seq_items.c). The page CONTENT (item
 * ids and counts per PLAN §8.2/§8.6 amended) is documented there.
 * Ranges (PLAN §8.3 as amended by PLAN_HZ_EDIT.md + PLAN_FREQ_DECIMALS.md):
 *  - freq rows:  centi-Hz 7-digit odometer boxes (OSC FREQ / SEQ PITCH
 *                0.01..20000, FILTER CUT / DELAY TONE 20..20000, R2/R4)
 *                — X/Z step the digit cursor's place (10000..0.01 Hz)
 *                with true carry and roll-over at the range edges; the
 *                1/12-octave ladder is gone (R9)
 *  - ADSR A/D/R: pos 0 = 0 s; pos 1..50 = 0.001*60000^((pos-1)/49)
 *  - percents:   integer 0..100, 1 pp steps
 *  - BPM:        GLOBAL tempo (PLAN §6): integer 1..500, one value for
 *                the whole synth;  SEQ DIV: integer 1..16, default 1
 *  - R11:        every numeric edit field ROLLS OVER at its bounds —
 *                nothing clamps anymore
 *  - AMT:        PITCH -50..+50 Hz (1 Hz/step); AMP/FILTER -100..+100
 *                (1 pp/step), wrapping inside the dest range (R11);
 *                dimmed + inert when dest OFF; re-clamped on dest
 *                switch. FREQ dimmed when the wave is white/pink.
 * DUTY items (approved plan change eacb368+fix) on the OSC and ENV+FLT
 * pages after WAVE: percent 5..95, dimmed/inert unless the wave is square.
 * All UI->engine traffic is via mailbox.h channels only. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"
#include "ui_internal.h"
#include "../config.h"
#include "../preset/defaults.h"
#include "../preset/preset.h"

#include "range.h"
#include "seq_items.h"

/* --- range helpers (PLAN §8.3) — the ladders live in ui/range.c (D6) - */

static int is_noise(wave_t w)
{
    return w == WAVE_WHITE || w == WAVE_PINK;
}
static double amt_min(mod_dest_t dest)
{
    /* RING is a dry/wet mix: 0..100, no negative half (owner order
     * 2026-11-06); PITCH keeps ±50 Hz; the rest are ±100 percent */
    if (dest == MOD_RING) return 0.0;
    return dest == MOD_PITCH ? -50.0 : -100.0;
}
static double amt_max(mod_dest_t dest)
{
    if (dest == MOD_RING) return 100.0;
    return dest == MOD_PITCH ? 50.0 : 100.0;
}
static double amt_clamp(mod_dest_t dest, double v)
{
    double lo = amt_min(dest), hi = amt_max(dest);
    return (double)lround(clampd(v, lo, hi));   /* integer steps only;
                                                   shared clamp (item 3) */
}

/* --- PLAN_HZ_EDIT.md (R1), amended by PLAN_FREQ_DECIMALS.md (R2/R3):
 * the 7-digit centi-Hz odometer boxes (10000..0.01 Hz) ----------------- */

/* R12a: "digit box" is a SPEC property (nav == NAV_DIGITS), not an id
 * list — OSC FREQ / FILTER CUT / DELAY TONE / SEQ PITCH carry it in
 * their ITEM_SPECS entries; no id comparisons anywhere. */
static int is_digit_box(int id)
{
    const item_spec_t *sp = item_spec(id);
    return sp != NULL && sp->nav == NAV_DIGITS;
}

/* the place value of the CURRENT digit for a row, in CENTI-HZ (R2):
 * digit 0 = 10000s place (1000000 centi-Hz) .. digit 4 = 1s (100),
 * digit 5 = 0.1s (10), digit 6 = 0.01s (1); any other Hz row would step
 * the 1s digit (there are none left — all ITF_HZ rows are boxes) */
static int digit_place(const ui_t *ui, int id)
{
    static const int PLACES[7] = { 1000000, 100000, 10000, 1000,
                                   100, 10, 1 };
    int place = 1;
    if (!is_digit_box(id))
        return 1;
    if (ui->freq_digit >= 0 && ui->freq_digit <= 6)
        place = PLACES[ui->freq_digit];
    return place;
}

/* --- tab navigation helpers ----------------------------------------- */

int is_osc_tab(const ui_t *ui) { return ui->tab < TAB_MOD; }
int chain_of(const ui_t *ui)           /* chain index for OSC tabs */
{
    /* TAB_OSC1..3 -> 0..2. U5: the SEQ-item/seq_sel users of ci only run
     * on OSC tabs (the "SEQ ids only on OSC tabs" invariant), so the
     * result is always a valid chain index — clamped explicitly to
     * 0..2 (shared clamp, Phase 5 item 3) to make the bound visible to
     * static analysis (clang-tidy security.ArrayBound on d->seq[ci] /
     * ui->seq_sel[ci]). */
    return clampi((int)ui->tab, 0, 2);
}
sub_tab_t cur_sub(const ui_t *ui)
{
    return ui->sub[chain_of(ui)];
}
static int cur_cursor_idx(const ui_t *ui)
{
    return (int)ui->tab * SUB_COUNT +
           (is_osc_tab(ui) ? (int)cur_sub(ui) : 0);
}
int *cur_cursor(ui_t *ui) { return &ui->cursor[cur_cursor_idx(ui)]; }

/* PR-E (PLAN_HZ_EDIT.md), R3 (PLAN_FREQ_DECIMALS.md): the digit cursor
 * starts over on every row entry — reset to the FIRST NON-DECIMAL digit
 * (the 1s place, index 4 of 0..6 from the left) whenever the cursor
 * lands on a digit-box row (nav == NAV_DIGITS; called after every
 * cursor/tab change). */
static void freq_digit_sync(ui_t *ui)
{
    item_t it = content_item(ui, *cur_cursor(ui));
    if (is_digit_box(it.id))
        ui->freq_digit = 4;
}

/* content item count / item for the current tab+sub-tab: the page
 * model in seq_items.c is the single source (A1) — the flat item list
 * is the concatenation of the page's spans; the SEQ page binds the
 * PITCH/VEL si to the selected rectangle at use time. */
int content_count(const ui_t *ui)
{
    const page_t *pg = page_get((int)ui->tab, is_osc_tab(ui)
                                              ? (int)cur_sub(ui) : 0);
    int n = 0;
    for (int i = 0; i < pg->nspans; i++) n += pg->spans[i].n;
    return n;
}

item_t content_item(const ui_t *ui, int i)
{
    const page_t *pg = page_get((int)ui->tab, is_osc_tab(ui)
                                              ? (int)cur_sub(ui) : 0);
    for (int k = 0; k < pg->nspans; k++) {
        if (i < pg->spans[k].n) {
            item_t it = pg->spans[k].items[i];
            if (it.id == IT_STEP_PITCH || it.id == IT_STEP_VEL)
                it.si = ui->seq_sel[chain_of(ui)];
            return it;
        }
        i -= pg->spans[k].n;
    }
    item_t none = { NULL, 0, 0 };
    return none;                       /* unreachable: cursor is bounded */
}

/* --- dim / value / edit ---------------------------------------------- */

int item_dim(const ui_t *ui, item_t it)
{
    const synth_params_t *d = &ui->draft;
    int ci = chain_of(ui);
    switch (it.id) {
    case IT_OSC_FREQ:  return is_noise(d->chain[ci].wave);
    case IT_OSC_DUTY:  return d->chain[ci].wave != WAVE_SQUARE;
    case IT_SUB_DUTY:  return d->chain[ci].sub[it.si].wave != WAVE_SQUARE;
    case IT_XMOD_AMT:  return d->xmod[it.si].dest == MOD_OFF;
    default:           return 0;
    }
}

/* --- spec-table access (Phase 5 item 2): the item spec table in
 * ui/seq_items.c is the ONE source of every item's range + format;
 * these two accessors map an item to its param slot (the si-target) —
 * everything else (clamping, wrapping, formatting) is generic below. --- */

/* D3 (PLAN_OPTIMIZATION.md O4): the member accessors — ONE switch on
 * the spec's param_kind_t replaces the mirrored item_get/item_set
 * ladders. The item layer only maps (tab, si) -> slot + kind. */
double param_get(const synth_params_t *d, param_kind_t kind, int ci,
                 int si)
{
    const chain_params_t *c = &d->chain[ci];
    switch (kind) {
    case PK_CHAIN_FREQ:   return (double)c->freq_hz;
    case PK_CHAIN_WAVE:   return (double)c->wave;
    case PK_CHAIN_DUTY:   return (double)c->duty;
    case PK_CHAIN_LEVEL:  return (double)c->level;
    case PK_CHAIN_VEL:    return (double)c->vel;
    case PK_ADSR_A:       return (double)c->adsr.a;
    case PK_ADSR_D:       return (double)c->adsr.d;
    case PK_ADSR_S:       return (double)c->adsr.s;
    case PK_ADSR_R:       return (double)c->adsr.r;
    case PK_CHAIN_FMODE:  return (double)c->fmode;
    case PK_CHAIN_FC:     return (double)c->fc;
    case PK_CHAIN_RES:    return (double)c->res;
    case PK_CHAIN_ENV:    return (double)c->envamt;
    case PK_CHAIN_POLES:  return (double)c->poles;
    case PK_CHAIN_MIXVOL: return (double)d->chain[si].mixvol;  /* si = ch */
    case PK_SUB_DIV:      return (double)c->sub[si].div;
    case PK_SUB_WAVE:     return (double)c->sub[si].wave;
    case PK_SUB_DUTY:     return (double)c->sub[si].duty;
    case PK_SUB_LEVEL:    return (double)c->sub[si].level;
    case PK_XMOD_SRC:     return (double)d->xmod[si].src;
    case PK_XMOD_DEST:    return (double)d->xmod[si].dest;
    case PK_XMOD_AMT:     return (double)d->xmod[si].amount;
    case PK_XMOD_MIX:     return (double)d->xmod[si].mix;
    case PK_XMOD_TGT:     return (double)d->xmod[si].tgt;
    case PK_SEQ_ON:       return (double)d->seq[ci].on;
    case PK_SEQ_DIV:      return (double)d->seq[ci].div;
    case PK_SEQ_MODE:     return (double)d->seq[ci].mode;
    case PK_SEQ_ONCE:     return (double)d->seq[ci].once;
    case PK_STEP_PITCH:   return (double)d->seq[ci].steps[si].pitch_hz;
    case PK_STEP_VEL:     return (double)d->seq[ci].steps[si].vel;
    case PK_TOP_BPM:      return (double)d->bpm;
    case PK_TOP_MASTER:   return (double)d->master;
    case PK_TOP_SAFETY:   return (double)d->safety;
    case PK_DLY_TIMEL:    return (double)d->delay.time_l;
    case PK_DLY_FB:       return (double)d->delay.feedback;
    case PK_DLY_TONE:     return (double)d->delay.tone;
    case PK_DLY_MIX:      return (double)d->delay.mix;
    case PK_RVB_SIZE:     return (double)d->reverb.size;
    case PK_RVB_DAMP:     return (double)d->reverb.damp;
    case PK_RVB_MIX:      return (double)d->reverb.mix;
    case PK_RVB_WIDTH:    return (double)d->reverb.width;
    default:              return 0.0;   /* PK_NONE / ITT_UI */
    }
}

void param_set(synth_params_t *d, param_kind_t kind, int ci, int si,
               double v)
{
    chain_params_t *c = &d->chain[ci];
    switch (kind) {
    case PK_CHAIN_FREQ:   c->freq_hz = (float)v; break;
    case PK_CHAIN_WAVE:   c->wave = (wave_t)(int)v; break;
    case PK_CHAIN_DUTY:   c->duty = (int)v; break;
    case PK_CHAIN_LEVEL:  c->level = (int)v; break;
    case PK_CHAIN_VEL:    c->vel = (int)v; break;
    case PK_ADSR_A:       c->adsr.a = (float)v; break;
    case PK_ADSR_D:       c->adsr.d = (float)v; break;
    case PK_ADSR_S:       c->adsr.s = (int)v; break;
    case PK_ADSR_R:       c->adsr.r = (float)v; break;
    case PK_CHAIN_FMODE:  c->fmode = (filt_mode_t)(int)v; break;
    case PK_CHAIN_FC:     c->fc = (float)v; break;
    case PK_CHAIN_RES:    c->res = (int)v; break;
    case PK_CHAIN_ENV:    c->envamt = (int)v; break;
    case PK_CHAIN_POLES:  c->poles = (int)v; break;
    case PK_CHAIN_MIXVOL: d->chain[si].mixvol = (int)v; break;
    case PK_SUB_DIV:      c->sub[si].div = (int)v; break;
    case PK_SUB_WAVE:     c->sub[si].wave = (wave_t)(int)v; break;
    case PK_SUB_DUTY:     c->sub[si].duty = (int)v; break;
    case PK_SUB_LEVEL:    c->sub[si].level = (int)v; break;
    case PK_XMOD_SRC:     d->xmod[si].src = (int)v; break;
    case PK_XMOD_DEST:    d->xmod[si].dest = (mod_dest_t)(int)v; break;
    case PK_XMOD_AMT:     d->xmod[si].amount = (float)v; break;
    case PK_XMOD_MIX:     d->xmod[si].mix = (int)v; break;
    case PK_XMOD_TGT:     d->xmod[si].tgt = (int)v; break;
    case PK_SEQ_ON:       d->seq[ci].on = (int)v; break;
    case PK_SEQ_DIV:      d->seq[ci].div = (int)v; break;
    case PK_SEQ_MODE:     d->seq[ci].mode = (int)v; break;
    case PK_SEQ_ONCE:     d->seq[ci].once = (int)v; break;
    case PK_STEP_PITCH:   d->seq[ci].steps[si].pitch_hz = (float)v; break;
    case PK_STEP_VEL:     d->seq[ci].steps[si].vel = (int)v; break;
    case PK_TOP_BPM:      d->bpm = (int)v; break;
    case PK_TOP_MASTER:   d->master = (int)v; break;
    case PK_TOP_SAFETY:   d->safety = (int)v; break;
    case PK_DLY_TIMEL:    d->delay.time_l = (float)v; break;
    case PK_DLY_FB:       d->delay.feedback = (int)v; break;
    case PK_DLY_TONE:     d->delay.tone = (int)v; break;
    case PK_DLY_MIX:      d->delay.mix = (int)v; break;
    case PK_RVB_SIZE:     d->reverb.size = (int)v; break;
    case PK_RVB_DAMP:     d->reverb.damp = (int)v; break;
    case PK_RVB_MIX:      d->reverb.mix = (int)v; break;
    case PK_RVB_WIDTH:    d->reverb.width = (int)v; break;
    default:              break;        /* PK_NONE / ITT_UI */
    }
}

double item_get(const ui_t *ui, item_t it)
{
    const item_spec_t *sp = item_spec(it.id);
    if (sp->tgt == ITT_UI)
        return (double)ui->seq_sel[chain_of(ui)];   /* IT_SEQ_STEPSEL */
    return param_get(&ui->draft, sp->kind, chain_of(ui), it.si);
}

static void item_set(ui_t *ui, item_t it, double v)
{
    const item_spec_t *sp = item_spec(it.id);
    if (sp->tgt == ITT_UI) {
        ui->seq_sel[chain_of(ui)] = (int)v;         /* IT_SEQ_STEPSEL */
        return;
    }
    param_set(&ui->draft, sp->kind, chain_of(ui), it.si, v);
}

/* parent mod dest of an AMT item (PITCH narrows the AMT range) */
mod_dest_t amt_dest_of(const ui_t *ui, item_t it)
{
    return ui->draft.xmod[it.si].dest;   /* only the matrix has AMT (O2) */
}

/* --- editing (LEFT/RIGHT, PLAN §8.1/§8.3) ---------------------------- */

void commit(ui_t *ui)
{
    mailbox_write(ui->mb, &ui->draft);
}

/* Table-driven edit (Phase 5 item 2): the spec entry (ui/seq_items.c)
 * carries the range (min/max in the fmt's edit space); this function is
 * generic — NO per-item ranges here. Dimmed items never reach this
 * (ui_handle_action breaks on item_dim; val_edit re-gates).
 * PLAN_HZ_EDIT.md: the Hz rows step the digit boxes' selected digit via
 * hz_digit_step (R4/R5); EVERY numeric field ROLLS OVER at its bounds
 * instead of clamping (R11 — the uniform rule, selectors included). */
void edit_item(ui_t *ui, item_t it, int dir)
{
    const item_spec_t *sp = item_spec(it.id);
    if (sp == NULL)
        return;                            /* unknown id: inert */
    double v = item_get(ui, it);
    switch (sp->fmt) {
    case ITF_HZ:                           /* centi-Hz odometer box (R2):
                                            * snap the float param value
                                            * through centi-Hz ints, step
                                            * the selected digit's place,
                                            * roll-over at [min, max]
                                            * (R4/R5); the FIRST edit
                                            * snaps any preset fraction
                                            * (R5 fractional rule) */
    case ITF_TONE:                         /* same box, floor 20.00 (R2) */
        v = hz_digit_step((int)lround(v * 100.0), digit_place(ui, it.id),
                          dir, sp->min, sp->max) / 100.0;
        break;
    case ITF_SEC: {                        /* ADSR ladder pos 0..max —
                                            * wraps top -> 0 (R11) */
        int n = sp->max - sp->min + 1;
        int pos = adsr_s_to_pos(v);
        pos = sp->min + (((pos + dir - sp->min) % n) + n) % n;
        v = adsr_pos_to_s(pos);
        break;
    }
    case ITF_CENTS: {                      /* 0.01 s steps, wraps (R11) */
        int n = sp->max - sp->min + 1;
        int c = (int)lround(v * 100.0);
        c = sp->min + (((c + dir - sp->min) % n) + n) % n;
        v = (float)c / 100.0f;
        break;
    }
    case ITF_AMT: {                        /* dest-dependent range kept
                                            * (PITCH ±50, others ±100,
                                            * §8.3) but WRAPS inside it
                                            * (R11) */
        int lo = (int)amt_min(amt_dest_of(ui, it));
        int hi = (int)amt_max(amt_dest_of(ui, it));
        int n = hi - lo + 1;
        int a = (int)lround(v);
        v = lo + (((a + dir - lo) % n) + n) % n;
        break;
    }
    case ITF_POLES:                        /* 12 <-> 24 dB/oct toggle */
        v = ((int)v == sp->max) ? sp->min : sp->max;
        break;
    default:                               /* R11: uniform roll-over —
                                            * percent, signed percent,
                                            * DUTY, BPM, XDIV and every
                                            * wrapping selector share the
                                            * one wrap formula (min +
                                            * ((v-min+dir) mod n + n) mod
                                            * n); nothing clamps anymore */
    {
        int n = sp->max - sp->min + 1;
        v = sp->min + ((((int)v + dir - sp->min) % n) + n) % n;
        break;
    }
    }
    item_set(ui, it, v);
    if (sp->fmt == ITF_DEST) {             /* re-clamp AMT on dest switch
                                            * (PLAN §8.3) */
        mod_dest_t nd = (mod_dest_t)item_get(ui, it);
        if (nd != MOD_OFF) {
            item_t amt = it;
            amt.id = IT_XMOD_AMT;
            item_set(ui, amt, amt_clamp(nd, item_get(ui, amt)));
        }
    }
    if (sp->tgt != ITT_UI)                 /* STEPSEL: selection only, no
                                            * mailbox commit */
        commit(ui);
}

/* --- init ------------------------------------------------------------ */

void ui_init(ui_t *ui, mailbox_t *mb)
{
    memset(ui, 0, sizeof *ui);
    ui->mb = mb;
    ui->draft = OPHIS_DEFAULTS;
    ui->menu_cursor = 5;                       /* BACK: the safe choice */
    ui->confirm_cursor = 1;                    /* NO: the safe choice   */
    /* focus[] is per OSC tab only (0 = sub-tab row, the PLAN §8.2 start);
     * MOD/MASTER have no sub-tab row — navigation keeps them on content
     * via is_osc_tab(). */
    if (preset_dir(ui->preset_dir, (int)sizeof ui->preset_dir) != 0)
        ui->preset_dir[0] = '\0';
}

/* Value-key edit + retrig (owner order 2026-11-05: holding X/Y — or
 * Z/X on desktop — retrigs): the value-edit + hold-repeat state
 * machine lives in ui/valrepeat.c (M1 split, PLAN_OPTIMIZATION.md O6);
 * ui.c routes the VAL actions and the tick into it. */

void ui_tick(ui_t *ui, Uint32 now)
{
    /* D9 (PLAN_CODE_REVIEW_FIXES.md Phase 6): retire expired flashes
     * here, as ui.h promises — 0 means "no active flash"; ui_draw then
     * tests the flag, not the clock. Call order in main.c is ui_tick
     * before ui_draw, so a flash set during this pass's actions still
     * draws (its until is in the future) and retires next pass. */
    if (ui->trig_flash_until && now >= ui->trig_flash_until)
        ui->trig_flash_until = 0;
    if (ui->panic_flash_until && now >= ui->panic_flash_until)
        ui->panic_flash_until = 0;
    valrepeat_tick(ui, now);   /* value-key hold (M1) */
}

int ui_wants_quit(const ui_t *ui)
{
    return ui->quitting && SDL_GetTicks() >= ui->quit_at;
}

/* --- outside-menu actions (PLAN §8.2 binding) ------------------------- */

/* U2 keyboard gate input (see ui.h): OUR name-entry state. SDL's
 * SDL_IsTextInputActive() is SDL_TRUE from startup on SDL >= 2.32
 * (libsdl-org/SDL#9309) and would swallow every keydown. */
int ui_text_entry_active(const ui_t *ui)
{
    return ui->in_menu == MENU_NAME;
}

void ui_handle_action(ui_t *ui, ui_action_t act)
{
    if (ui->quitting)
        return;
    if (ui->in_menu != MENU_OFF) {             /* menu context remap */
        menu_action(ui, act);
        return;
    }
    int *cur = cur_cursor(ui);
    int n = content_count(ui);

    switch (act) {
    case ACT_TRIGGER:
        atomic_store(&ui->mb->trigger_req, 1);
        ui->trig_flash_until = SDL_GetTicks() + 300;
        break;
    case ACT_RUN:
        ui->draft.run = !ui->draft.run;        /* run travels in mailbox */
        commit(ui);
        break;
    case ACT_TAB_BACK:
        ui->tab = (top_tab_t)((ui->tab + TAB_COUNT - 1) % TAB_COUNT);
        freq_digit_sync(ui);           /* new tab -> possibly a new row */
        break;
    case ACT_TAB_NEXT:
        ui->tab = (top_tab_t)((ui->tab + 1) % TAB_COUNT);
        freq_digit_sync(ui);           /* new tab -> possibly a new row */
        break;
    case ACT_SEL_UP:
        if (is_osc_tab(ui) && !ui->focus[ui->tab])
            break;                     /* on the sub-tab row: inert */
        if (is_osc_tab(ui) && *cur == 0)
            ui->focus[ui->tab] = 0;    /* UP from the top of content */
        else
            *cur = (*cur + n - 1) % n; /* move the cursor (wraps) */
        freq_digit_sync(ui);           /* row entry resets the digit
                                        * cursor (PR-E) */
        break;
    case ACT_SEL_DOWN:
        if (is_osc_tab(ui) && !ui->focus[ui->tab])
            ui->focus[ui->tab] = 1;    /* DOWN enters the content */
        else
            *cur = (*cur + 1) % n;
        freq_digit_sync(ui);           /* row entry resets the digit
                                        * cursor (PR-E) */
        break;
    case ACT_NAV_LEFT:
    case ACT_NAV_RIGHT: {
        int dir = act == ACT_NAV_RIGHT ? +1 : -1;
        if (is_osc_tab(ui) && !ui->focus[ui->tab]) {
            /* LEFT/RIGHT on the sub-tab row cycles sub-tabs */
            int ci = chain_of(ui);
            ui->sub[ci] = (sub_tab_t)((ui->sub[ci] + dir + SUB_COUNT) %
                                      SUB_COUNT);
        } else {
            /* R12/R12a (PLAN_HZ_EDIT.md): the row-dependent NAV dispatch
             * is TABLE-DRIVEN — the item spec's nav mode decides what
             * LEFT/RIGHT do; no id comparisons, no per-page flags. The
             * sub-tab branch above keeps priority exactly as before. */
            item_t it = content_item(ui, *cur);
            const item_spec_t *sp = item_spec(it.id);
            switch (sp ? sp->nav : NAV_NONE) {
            case NAV_STEPSEL: {        /* STEP row: move the rectangle,
                                        * wrapping across the spec's
                                        * min..max (0..7) */
                int n = sp->max - sp->min + 1;
                int s = ui->seq_sel[chain_of(ui)] - sp->min;
                ui->seq_sel[chain_of(ui)] =
                    sp->min + (((s + dir) % n) + n) % n;
                break;
            }
            case NAV_DIGITS:           /* digit box: move the digit
                                        * cursor — LEFT walks toward the
                                        * 10000s digit, RIGHT toward the
                                        * 0.01s; dimmed rows are
                                        * digit-inert (R10) */
                if (!item_dim(ui, it))
                    /* freq_digit counts FROM THE LEFT (0 = 10000s): RIGHT
                     * (+1) walks toward the 0.01s digit, LEFT (-1) toward
                     * the 10000s; clamped 0..6 (R3) */
                    ui->freq_digit = clampi(ui->freq_digit + dir, 0, 6);
                break;
            default:                   /* NAV_NONE: inert (as today) */
                break;
            }
        }
        break;
    }
    case ACT_VAL_DEC:
    case ACT_VAL_INC: {
        int dir = act == ACT_VAL_INC ? +1 : -1;
        val_edit(ui, dir, SDL_GetTicks());   /* valrepeat.c (M1) */
        break;
    }
    case ACT_MENU:
        ui->in_menu = MENU_MAIN;               /* overlay on any screen */
        ui->menu_cursor = 5;
        break;
    case ACT_CONFIRM:
        break;                     /* menu select only — inert outside */
    }
}

/* S3 (PLAN_OPTIMIZATION.md O6): the input-policy gate — while the SAVE
 * name-entry dialog is open the keyboard belongs to the TEXTINPUT/
 * BACKSPACE path; only ESC (cancel) and ENTER (confirm, owner order
 * 2026-11-05) reach the keymap. SDL 2.32 enables text input from
 * startup (libsdl-org/SDL#9309), so the gate keys off OUR state via
 * ui_text_entry_active(), never SDL_IsTextInputActive(). Gamepad
 * buttons are unaffected (no text entry on them). Lives HERE (the UI
 * owns its input policy), not in main.c. */
int ui_event_filter(const ui_t *ui, const SDL_Event *e)
{
    if (e->type == SDL_KEYDOWN && ui_text_entry_active((ui_t *)ui) &&
        e->key.keysym.sym != SDLK_ESCAPE &&
        e->key.keysym.sym != SDLK_RETURN)
        return 0;                      /* swallowed by text entry */
    return 1;
}

void ui_handle_event(ui_t *ui, SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
        SDL_StopTextInput();
        ui->in_menu = MENU_QUIT;
        ui->quitting = 1;
        ui->quit_at = SDL_GetTicks();          /* no flash on WM close */
        break;
    case SDL_KEYDOWN:
        if (ui->in_menu == MENU_NAME &&
            e->key.keysym.sym == SDLK_BACKSPACE)
            name_delete(ui);
        break;
    case SDL_TEXTINPUT:
        if (ui->in_menu == MENU_NAME) {
            for (const char *p = e->text.text; *p; p++)
                name_insert(ui, *p);
        }
        break;
    /* R36S SAVE name entry is handled in menu_action() via the value
     * actions (ACT_VAL_INC inserts / ACT_VAL_DEC deletes — the grid
     * picker routes through keymap_action like everything else,
     * PLAN_KEYMAP_REBIND §4). Keyboard TEXTINPUT/Backspace above is
     * desktop text entry, not a bound key. */
    case SDL_KEYUP:
    case SDL_CONTROLLERBUTTONUP:
        /* the value-key hold ends at release (keymap_val_release):
         * desktop KEYUP previously never cleared it — the synthesized
         * repeat ran away after the key was released */
        if (keymap_val_release(e))
            ui->val_held_dir = 0;
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            ui->val_held_dir = 0;      /* stuck hold on focus loss */
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        ui->val_held_dir = 0;          /* stuck hold on disconnect */
        break;
    default:
        break;
    }
}
