/* src/ui/ui_draw.c — SDL2 UI rendering (PLAN §8.6; X3,
 * PLAN_CODE_REVIEW_FIXES.md Phase 8: moved out of ui.c, pure structure,
 * no behavior change). Draws the 640x480 logical frame: status strip,
 * top tab bar + sub-tab bar, section-driven content pages (OSC/ENV+FLT/
 * MOD/MASTER + the SEQ page's step strip), two-line hint bar and the
 * menu window overlays (PLAN §8.4). ui_tick retires flash flags before
 * ui_draw (D9) — the draw side only tests the flags.
 * Layout (AMENDED, owner order, PLAN §8.6): 10x10 font cells (letters
 * +2 pts, ui/font8x8.c); top tab bar + sub-tab bar are 20 px filled
 * strips; content starts at y=80 on every tab (2 blank lines after the
 * sub-tab bar); item row pitch is 16 px everywhere (row_cursor
 * {0, y-1, 640, 15}); the SEQ page keeps its 2-blank-rows + "Steps"
 * label + 52 px strip row structure at the same 16 px pitch; every
 * page fits above the two-line hint bar (y=456/468).
 * Section rendering: gray non-selectable label row at x=24; section
 * items indented to x=44; ONE empty 16 px row between a section's last
 * item and the next section label (none after the last section).
 * The LOAD window shows the CURRENT FOLDER PATH on its title line;
 * subdirectories with a "/" suffix, errors inline.
 * Text: single glyph-atlas draw path (X1) — see draw_text below.
 * Palette (theme.h, IMPLEMENTATION.md §6 amended): every color comes
 * from the dark-purple theme constants; no scattered literals. */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ui.h"
#include "ui_internal.h"
#include "../config.h"
#include "../preset/schema.h"

#include "font8x8.h"
#include "theme.h"
#include "seq_items.h"

static const char *TAB_NAMES[TAB_COUNT] =
  { "OSC 1", "OSC 2", "OSC 3", "MOD", "MASTER" };
static const char *SUB_NAMES[SUB_COUNT] = { "OSC", "ENV+FLT", "SEQ" };

/* the section split of the current page (drives rendering; the cursor
 * walks the flat list and skips over labels/gaps visually) */
static const section_t *page_sections(const ui_t *ui, int *nsec)
{
    if (!is_osc_tab(ui)) {
        if (ui->tab == TAB_MOD) { *nsec = MOD_SECTIONS_N; return MOD_SECTIONS; }
        *nsec = MASTER_SECTIONS_N; return MASTER_SECTIONS;
    }
    if (cur_sub(ui) == SUB_ENVFLT) {
        *nsec = ENVFLT_SECTIONS_N; return ENVFLT_SECTIONS;
    }
    *nsec = OSC_SECTIONS_N; return OSC_SECTIONS;      /* SUB_OSC */
}

/* --- value formatting ------------------------------------------------ */

static void fmt_sec(char *b, size_t blen, double t)
{
    if (t <= 0.0)     snprintf(b, blen, "0.000 s");
    else if (t < 1.0) snprintf(b, blen, "%.3f s", t);
    else              snprintf(b, blen, "%.2f s", t);
}

void item_value(const ui_t *ui, item_t it, char *b, size_t blen)
{
    const item_spec_t *sp = item_spec(it.id);
    if (sp == NULL) {                         /* unknown id (unreachable
                                                 via content_item) */
        snprintf(b, blen, "?");
        return;
    }
    if (item_dim(ui, it)) {                   /* dimmed value display */
        snprintf(b, blen, "----");
        return;
    }
    double v = item_get(ui, it);
    switch (sp->fmt) {
    case ITF_DIV:     snprintf(b, blen, "/%d", (int)v); break;
    case ITF_HZ:      /* centi-Hz odometer boxes (PLAN_FREQ_DECIMALS.md
                       * R1/R5): "%05d.%02d Hz" — integer part + period +
                       * 2 decimals; a preset fraction is SHOWN, not
                       * rounded (R5 fractional rule) */
    case ITF_TONE: {
                      int centi = (int)lround(v * 100.0);
                      int ip = centi / 100, dec = centi - ip * 100;
                      snprintf(b, blen, "%05d.%02d Hz", ip, dec);
                      break;
    }
    case ITF_SEC:     fmt_sec(b, blen, v); break;
    case ITF_CENTS:   snprintf(b, blen, "%.2f s", v); break;
    case ITF_WAVE:    ui_enum_display(schema_wave_name((int)v), b, blen);
    break;
    case ITF_DEST:    /* R0f: '_' -> ' ' ("flt_cut" -> "FLT CUT") —
                       * NOT ui_enum_display's '.' separator */
                      ui_dest_display(schema_dest_name((int)v), b, blen);
    break;
    case ITF_TGT:     /* R2: the routing target shows the OSCILLATOR
                       * NAME, never the raw 0/1/2 */
                      snprintf(b, blen, "OSC %d", (int)v + 1);
    break;
    case ITF_XSRC:    /* R3: the frozen OSC-name table, not the raw
                       * A/B/C-letter preset strings */
                      ui_xsrc_display((int)v, b, blen);
    break;
    case ITF_FILT:    ui_enum_display(schema_filt_name((int)v), b, blen);
    break;
    case ITF_POLES:   ui_enum_display(schema_poles_name((int)v), b, blen);
    break;
    case ITF_AMT:
        snprintf(b, blen, amt_dest_of(ui, it) == MOD_PITCH
                                  ? "%+.0f Hz" : "%+.0f %%", v);
        break;
    case ITF_ONOFF:   snprintf(b, blen, "%s", (int)v ? "ON" : "OFF"); break;
    case ITF_SEQMODE: snprintf(b, blen, "%s", SEQMODE_NAMES[(int)v]); break;
    case ITF_SEQONCE: snprintf(b, blen, "%s", SEQONCE_NAMES[(int)v]); break;
    case ITF_STEPSEL: snprintf(b, blen, "STEP %d", (int)v + 1); break;
    case ITF_INT:     snprintf(b, blen, "%d", (int)v); break;
    case ITF_XDIV:    snprintf(b, blen, "x%d", (int)v); break;
    case ITF_DUTY:
    case ITF_PCT:     snprintf(b, blen, "%d %%", (int)v); break;
    case ITF_SPCT:    snprintf(b, blen, "%+d %%", (int)v); break;
    }
}

/* --- text drawing ----------------------------------------------------- */

/* X1 (PLAN_CODE_REVIEW_FIXES.md Phase 8): the 96 scaled 10x10 glyphs
 * (ASCII 0x20..0x7F — the same set font_glyph10 serves) live in ONE SDL
 * texture atlas, built once on the first draw_text call (UI thread only):
 * SDL_TEXTUREACCESS_STATIC, white lit pixels on transparent background,
 * SDL_BLENDMODE_BLEND. draw_text tints it per call with
 * ColorMod/AlphaMod and draws ONE quad per glyph (SDL_RenderCopy)
 * instead of one SDL_RenderFillRect per lit pixel — the R36S
 * frame-budget finding. Glyph shapes stay single-sourced in
 * ui/font8x8.c. Works on both targets (desktop renderer at logical
 * scale 2; the R36S SDL renderer at scale 1). */
#define ATLAS_GLYPHS 96

static SDL_Texture *font_atlas_build(SDL_Renderer *r)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_STATIC,
                                       ATLAS_GLYPHS * OPHIS_FONT_W,
                                       OPHIS_FONT_H);
    if (!t)
        return NULL;
    Uint32 px[ATLAS_GLYPHS * OPHIS_FONT_W * OPHIS_FONT_H];
    for (int g = 0; g < ATLAS_GLYPHS; g++) {
        const unsigned char *gl = font_glyph10((unsigned char)(0x20 + g));
        for (int dy = 0; dy < OPHIS_FONT_H; dy++) {
            unsigned char bits = gl[dy];
            /* row-major over the atlas width; glyph g occupies columns
             * g*OPHIS_FONT_W..+9 of row dy */
            Uint32 *row = px + dy * ATLAS_GLYPHS * OPHIS_FONT_W
                               + g * OPHIS_FONT_W;
            for (int dx = 0; dx < OPHIS_FONT_W; dx++)
                row[dx] = (bits & (1u << dx)) ? 0xFFFFFFFFu : 0x00000000u;
        }
    }
    if (SDL_UpdateTexture(t, NULL, px,
                          ATLAS_GLYPHS * OPHIS_FONT_W *
                          (int)sizeof(Uint32)) != 0) {
        SDL_DestroyTexture(t);
        return NULL;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}

/* per-pixel fallback (atlas build failed: no RGBA8888 / OOM) — the
 * pre-X1 renderer, kept so a failed atlas init degrades to the old
 * draw cost instead of losing all text */
static void draw_text_pixels(SDL_Renderer *r, int x, int y, const char *s,
                             SDL_Color c)
{
    for (; *s; s++, x += OPHIS_FONT_W) {
        const unsigned char *g = font_glyph10((unsigned char)*s);
        if (!g)
            continue;
        for (int dy = 0; dy < OPHIS_FONT_H; dy++) {
            unsigned char bits = g[dy];
            for (int dx = 0; dx < OPHIS_FONT_W; dx++)
                if (bits & (1u << dx)) {
                    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
                    SDL_Rect px = { x + dx, y + dy, 1, 1 };
                    SDL_RenderFillRect(r, &px);
                }
        }
    }
}

/* draw_text contract unchanged: same signature, same call sites, same
 * output — lit pixels exactly, in the given color, at the given cell */
static void draw_text(SDL_Renderer *r, int x, int y, const char *s,
                      SDL_Color c)
{
    static SDL_Texture *atlas;         /* built once, UI thread only */
    if (!atlas)
        atlas = font_atlas_build(r);
    if (!atlas) {
        draw_text_pixels(r, x, y, s, c);
        return;
    }
    SDL_SetTextureColorMod(atlas, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(atlas, c.a);
    for (; *s; s++, x += OPHIS_FONT_W) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 0x20 || ch > 0x7F)
            continue;                  /* outside the glyph set (was NULL
                                        * from font_glyph10) */
        SDL_Rect src = { (ch - 0x20) * OPHIS_FONT_W, 0,
                         OPHIS_FONT_W, OPHIS_FONT_H };
        SDL_Rect dst = { x, y, OPHIS_FONT_W, OPHIS_FONT_H };
        SDL_RenderCopy(r, atlas, &src, &dst);
    }
}

/* Palette (theme.h, IMPLEMENTATION.md §6 amended): every text color comes
 * from the dark-purple theme constants; no scattered literals below. */
static SDL_Color ophis_sdl_color(ophis_rgb_t c)
{
    SDL_Color r = { c.r, c.g, c.b, 255 };
    return r;
}

#define C_WHITE  ophis_sdl_color(THEME_TEXT)       /* normal text           */
#define C_GRAY   ophis_sdl_color(THEME_DIM_TEXT)   /* dimmed labels         */
#define C_BLACK  ophis_sdl_color(THEME_SEL_TEXT)   /* text on selection     */
#define C_GREEN  ophis_sdl_color(THEME_ACCENT)     /* active value/cursor   */
#define C_RED    ophis_sdl_color(THEME_ERROR)      /* errors                */

/* cursor highlight for a text row (16 px row pitch, owner order,
 * PLAN §8.6 amended) */
static void row_cursor(SDL_Renderer *r, int y)
{
    SDL_Rect row = { 0, y - 1, 640, 15 };
    SDL_SetRenderDrawColor(r, THEME_SEL_FILL.r, THEME_SEL_FILL.g,
                           THEME_SEL_FILL.b, 255);
    SDL_RenderFillRect(r, &row);
}

/* hint bar, two 10-px lines (PLAN §8.6: the keymap in effect on this
 * screen, in this target's binding words; labels via keymap_label() —
 * THE one label place, PLAN_KEYMAP_REBIND §2/§4). keymap_label() is
 * caller-buffer form (remediation F5): one small local buffer per label
 * so the two snprintfs never alias. Buffer sizes are size_t — the
 * contract is the type, not a comment. */
static void hint_binding(char *b1, size_t n1, char *b2, size_t n2)
{
    char l_tab_b[16], l_tab_n[16], l_sel_u[16], l_sel_d[16];
    char l_dec[16], l_inc[16], l_run[16], l_trig[16], l_menu[16];
    const char *tab_b = keymap_label(KB_TAB_BACK, l_tab_b, sizeof l_tab_b);
    const char *tab_n = keymap_label(KB_TAB_NEXT, l_tab_n, sizeof l_tab_n);
    const char *sel_u = keymap_label(KB_SEL_UP, l_sel_u, sizeof l_sel_u);
    const char *sel_d = keymap_label(KB_SEL_DOWN, l_sel_d, sizeof l_sel_d);
    const char *dec   = keymap_label(KB_VAL_DEC, l_dec, sizeof l_dec);
    const char *inc   = keymap_label(KB_VAL_INC, l_inc, sizeof l_inc);
    const char *run   = keymap_label(KB_RUN, l_run, sizeof l_run);
    const char *trig  = keymap_label(KB_TRIGGER, l_trig, sizeof l_trig);
    const char *menu  = keymap_label(KB_MENU, l_menu, sizeof l_menu);

    snprintf(b1, n1, "%s/%s:TABS  %s/%s:SEL  %s/%s:EDIT",
             tab_b ? tab_b : "?", tab_n ? tab_n : "?",
             sel_u ? sel_u : "?", sel_d ? sel_d : "?",
             dec ? dec : "?", inc ? inc : "?");
    snprintf(b2, n2, "%s:RUN  %s:TRIG  %s:MENU",
             run ? run : "?", trig ? trig : "?", menu ? menu : "?");
}

/* R3/PR-D/R12a (PLAN_HZ_EDIT.md), amended by PLAN_FREQ_DECIMALS.md
 * (PR-C/R1): the digit marker — one short underline under the ACTIVE
 * digit cell of the fixed-width "%05d.%02d Hz" field (value text starts
 * at x=460, one 10 px cell per glyph; cells from the left: 5 integer
 * digits, then the fixed period separator, then the 2 decimal digits —
 * 8 cells total, the period is not editable). freq_digit counts the 7
 * editable digits 0..6 from the left, so the cell index is freq_digit
 * for the integer digits (0..4) and freq_digit + 1 for the decimal
 * digits (5 -> cell 6, 6 -> cell 7), skipping the period cell. Drawn
 * only when ui_digit_marker_active() gates it on (R13): cursor row +
 * content focus + not dim + NAV_DIGITS. */

/* R13 (PLAN_HZ_EDIT.md): the marker's gate, factored out of the two
 * draw paths (draw_sections + the SEQ row loop) — the mode comes from
 * the item spec, never an id comparison. The row highlight keeps its
 * own sel condition (R13 does not change it). */
int ui_digit_marker_active(const ui_t *ui, item_t it)
{
    int slot = (int)ui->tab * SUB_COUNT +
               (is_osc_tab(ui) ? (int)cur_sub(ui) : 0);
    item_t cur = content_item(ui, ui->cursor[slot]);
    int sel = (cur.id == it.id && cur.si == it.si &&
               cur.name == it.name);
    int content = !is_osc_tab(ui) || ui->focus[chain_of(ui)] != 0;
    return sel && content && !item_dim(ui, it) &&
           item_spec(it.id)->nav == NAV_DIGITS;
}

static void draw_digit_marker(ui_t *ui, SDL_Renderer *r, int y)
{
    /* editable digit -> cell index from the left; the period cell (5)
     * is skipped (PR-C) */
    int d = ui->freq_digit <= 4 ? ui->freq_digit : ui->freq_digit + 1;
    SDL_Rect mk = { 460 + d * OPHIS_FONT_W,
                    y + OPHIS_FONT_H + 1, OPHIS_FONT_W, 2 };
    SDL_SetRenderDrawColor(r, THEME_ACCENT.r, THEME_ACCENT.g,
                           THEME_ACCENT.b, 255);
    SDL_RenderFillRect(r, &mk);
}

/* SECTION-DRIVEN page renderer (owner order, PLAN §8.2/§8.6 amended):
 * used for the OSC, ENV+FLT, MOD and MASTER pages. For each section of
 * the page's flat item list: a gray non-selectable label row at x=24,
 * then the section's items indented to x=44 (name) / x=460 (value), one
 * 16 px row each; then ONE empty 16 px row before the next section
 * label (none after the last section). The cursor index is the page's
 * flat item index — the sections give first/count, a running flat index
 * drives the sel comparison, and the cursor walks over labels and gaps
 * visually. Returns the y below the last drawn row. */
static int draw_sections(ui_t *ui, SDL_Renderer *r, int y)
{
    int nsec;
    const section_t *sec = page_sections(ui, &nsec);
    int cur = *cur_cursor(ui);
    char b[160];
    int flat = 0;

    for (int s = 0; s < nsec; s++) {
        draw_text(r, 24, y, sec[s].label, C_GRAY);
        y += 16;
        for (int k = 0; k < sec[s].count; k++, flat++) {
            item_t it = content_item(ui, flat);
            int sel = (flat == cur);
            int dim = item_dim(ui, it);
            if (sel) row_cursor(r, y);
            draw_text(r, 44, y, it.name,
                      sel ? C_BLACK : (dim ? C_GRAY : C_WHITE));
            item_value(ui, it, b, sizeof b);
            draw_text(r, 460, y, b,
                      sel ? C_BLACK : (dim ? C_GRAY : C_WHITE));
            if (ui_digit_marker_active(ui, it))
                draw_digit_marker(ui, r, y);
            y += 16;
        }
        if (s < nsec - 1)
            y += 16;              /* ONE empty row before the next label */
    }
    return y;
}

void ui_draw(ui_t *ui, SDL_Renderer *r, const ui_status_t *st)
{
    char b[160], b2[160];

    SDL_SetRenderDrawColor(r, THEME_BG.r, THEME_BG.g, THEME_BG.b, 255);
    SDL_RenderClear(r);

    /* ---- status strip (PLAN §8.1): product title centered
     * (PLAN_KEYMAP_REBIND §4); TRIGGER/PANIC flash on the right side ---- */
    snprintf(b, sizeof b, "RUN:%s  GATE:%.2f",
             st->run ? "ON" : "OFF", (double)st->gate);
    draw_text(r, 8, 4, b, st->run ? C_GREEN : C_WHITE);
    draw_text(r, (640 - 11 * OPHIS_FONT_W) / 2, 4, "OPHIS DRONE", C_WHITE);
    /* D9: flash flags are retired by ui_tick (0 = inactive); no clock
     * comparison at draw time */
    if (ui->trig_flash_until)
        draw_text(r, 560, 4, "TRIGGER", C_GREEN);
    if (ui->panic_flash_until)
        draw_text(r, 560, 4, "PANIC", C_RED);

    int osc = is_osc_tab(ui);
    int ci = chain_of(ui);

    /* ---- top tab bar (PLAN §8.6, owner order): 20 px filled strip ---- */
    {
        SDL_Rect strip = { 0, 16, 640, 20 };
        SDL_SetRenderDrawColor(r, THEME_TRACK.r, THEME_TRACK.g,
                               THEME_TRACK.b, 255);
        SDL_RenderFillRect(r, &strip);
        int x = 8;
        for (int t = 0; t < TAB_COUNT; t++) {
            draw_text(r, x, 21, TAB_NAMES[t],
                      t == (int)ui->tab ? C_GREEN : C_WHITE);
            x += (int)strlen(TAB_NAMES[t]) * OPHIS_FONT_W + 10;
            if (t < TAB_COUNT - 1) {
                draw_text(r, x, 21, "|", C_GRAY);
                x += 30;
            }
        }
    }

    /* live envelope bar on chain (OSC) tabs; live step cursor on SEQ */
    if (osc) {
        draw_text(r, 496, 21, "ENV", C_GRAY);
        SDL_Rect bar = { 528, 21, (int)(st->env[ci] * 100.0f), 8 };
        SDL_SetRenderDrawColor(r, THEME_BAR.r, THEME_BAR.g, THEME_BAR.b, 255);
        SDL_RenderFillRect(r, &bar);
        SDL_Rect box = { 528, 21, 100, 8 };
        SDL_SetRenderDrawColor(r, THEME_TRACK.r, THEME_TRACK.g,
                               THEME_TRACK.b, 255);
        SDL_RenderDrawRect(r, &box);
    }

    /* ---- sub-tab bar (OSC tabs only, PLAN §8.6): 20 px filled strip ---- */
    if (osc) {
        SDL_Rect strip = { 0, 40, 640, 20 };
        SDL_SetRenderDrawColor(r, THEME_TRACK.r, THEME_TRACK.g,
                               THEME_TRACK.b, 255);
        SDL_RenderFillRect(r, &strip);
        int x = 24;
        for (int s = 0; s < SUB_COUNT; s++) {
            int cur = (s == (int)cur_sub(ui));
            SDL_Color c = cur ? (ui->focus[ui->tab] ? C_GREEN : C_WHITE)
                              : C_WHITE;
            draw_text(r, x, 45, SUB_NAMES[s], c);
            if (cur && !ui->focus[ui->tab]) {  /* arrows start here */
                SDL_Rect u = { x - 2, 57,
                               (int)strlen(SUB_NAMES[s]) * OPHIS_FONT_W + 2,
                               2 };
                SDL_SetRenderDrawColor(r, THEME_ACCENT.r, THEME_ACCENT.g,
                                       THEME_ACCENT.b, 255);
                SDL_RenderFillRect(r, &u);
            }
            x += (int)strlen(SUB_NAMES[s]) * OPHIS_FONT_W + 30;
        }
    }

    /* ---- content: starts at y=80 on ALL tabs (2 blank lines after the
     * sub-tab bar / after the blank space where it would be); every row
     * is 16 px tall — all pages fit above the hint bar (y < 456) ---- */
    int y = 80;
    if (osc && cur_sub(ui) == SUB_SEQ) {
        /* ---- SEQ page (PLAN §8.2 AMENDED, owner order): SINGLE
         * COLUMN, one parameter per row, table-driven — cursor index ==
         * item == drawn row, iterate SEQ_ITEMS top-to-bottom:
         * SEQ ON · DIV · MODE · ONCE · STEP · PITCH · VEL (NO BPM — the
         * GLOBAL tempo lives on the MASTER tab). The MODE and ONCE
         * selectors are visible rows (presence regression:
         * tests/test_seq_items.c). ---- */
        const synth_params_t *d = &ui->draft;
        int cur = *cur_cursor(ui);
        int sel = ui->seq_sel[ci];

        /* the page names the chain it edits (owner order 2026-11-06:
         * "UI should design what is currently selected") */
        snprintf(b, sizeof b, "SEQUENCER - OSC %d", ci + 1);
        draw_text(r, 24, y, b, C_GRAY);
        y += 24;

        for (int i = 0; i < SEQ_ITEMS_N; i++) {
            item_t it = content_item(ui, i);   /* binds PITCH/VEL si to
                                                  * the selected rectangle */
            if (it.id == IT_SEQ_STEPSEL) {
                /* the STEP row: 2 blank 16 px rows, a gray "Steps" label
                 * row, then the 8 step rectangles as ONE row of the
                 * column (52 px pitch); filled when vel > 0, empty when
                 * vel = 0, the selected rectangle highlighted, the live
                 * step drawn */
                y += 32;                       /* 2 blank 16 px rows */
                draw_text(r, 24, y, "Steps", C_GRAY);
                y += 16;
                for (int q = 0; q < 8; q++) {
                    SDL_Rect sq = { 24 + q * 76, y, 64, 40 };
                    int live = (st->step[ci] == q && d->seq[ci].on);
                    if (d->seq[ci].steps[q].vel > 0) {
                        SDL_SetRenderDrawColor(r, THEME_SEL_FILL.r,
                                                THEME_SEL_FILL.g,
                                                THEME_SEL_FILL.b, 255);
                        SDL_RenderFillRect(r, &sq);
                    } else {
                        SDL_SetRenderDrawColor(r, THEME_TRACK.r,
                                                THEME_TRACK.g,
                                                THEME_TRACK.b, 255);
                        SDL_RenderFillRect(r, &sq);
                    }
                    /* step markers (owner order, PLAN §8.2 amended by
                     * PLAN_KEYMAP_REBIND §5): the SELECTED step
                     * (PITCH/VEL bind to it) gets a THICK THEME_SEQ_SEL
                     * frame — 5 nested rects drawn OUTWARD so the
                     * 64x40 cell body is not shrunk; the step PLAYING
                     * NOW gets an ORANGE dot inside the square */
                    if (q == sel) {
                        SDL_SetRenderDrawColor(r, THEME_SEQ_SEL.r,
                                                THEME_SEQ_SEL.g,
                                                THEME_SEQ_SEL.b, 255);
                        for (int t = 0; t < 5; t++) {
                            SDL_Rect b5 = { sq.x - t, sq.y - t,
                                           sq.w + 2 * t, sq.h + 2 * t };
                            SDL_RenderDrawRect(r, &b5);
                        }
                    } else {
                        SDL_SetRenderDrawColor(r, THEME_DIM_TEXT.r,
                                                THEME_DIM_TEXT.g,
                                                THEME_DIM_TEXT.b, 255);
                        SDL_RenderDrawRect(r, &sq);
                    }
                    if (live) {
                        SDL_Rect dot = { sq.x + 28, sq.y + 12, 8, 8 };
                        SDL_SetRenderDrawColor(r, THEME_ORANGE.r,
                                                THEME_ORANGE.g,
                                                THEME_ORANGE.b, 255);
                        SDL_RenderFillRect(r, &dot);
                    }
                    snprintf(b, sizeof b, "%d", q + 1);
                    draw_text(r, sq.x + 2, sq.y + 28, b,
                              d->seq[ci].steps[q].vel > 0 ? C_BLACK
                                                          : C_GRAY);
                }
                if (cur == i) {                /* row cursor on the strip */
                    SDL_Rect sel2 = { 20 + sel * 76, y - 3, 72, 46 };
                    SDL_SetRenderDrawColor(r, THEME_SEL_TEXT.r,
                                            THEME_SEL_TEXT.g,
                                            THEME_SEL_TEXT.b, 255);
                    SDL_RenderDrawRect(r, &sel2);
                }
                y += 52;                       /* taller row (the strip) */
            } else {
                /* one parameter per row: name left, value right; the
                 * row highlight via row_cursor (MODE value is
                 * SEQMODE_NAMES[...mode], ONCE value is
                 * SEQONCE_NAMES[...once] — via item_value) */
                int sel_row = (i == cur);
                if (sel_row) row_cursor(r, y);
                draw_text(r, 24, y, it.name, sel_row ? C_BLACK : C_WHITE);
                item_value(ui, it, b, sizeof b);
                draw_text(r, 460, y, b, sel_row ? C_BLACK : C_GREEN);
                /* R12a: the PITCH row is a digit box — the marker keys
                 * off the spec's nav mode, same gate as draw_sections */
                if (ui_digit_marker_active(ui, it))
                    draw_digit_marker(ui, r, y);
                y += 16;
            }
        }
    } else {
        /* OSC / ENV+FLT / MOD / MASTER pages: section-driven (labels at
         * x=24, items indented to x=44, ONE 16 px gap row between
         * sections) */
        y = draw_sections(ui, r, y);
    }

    /* ---- hint bar: TWO lines (PLAN §8.6, owner order) ---- */
    hint_binding(b, sizeof b, b2, sizeof b2);
    draw_text(r, 4, 456, b, C_GRAY);
    draw_text(r, 4, 468, b2, C_GRAY);

    /* ---- menu window overlay (PLAN §8.4) ---- */
    if (ui->in_menu == MENU_MAIN || ui->in_menu == MENU_CONFIRM) {
        SDL_Rect win = { 160, 120, 320, 300 };
        SDL_SetRenderDrawColor(r, THEME_BG.r, THEME_BG.g, THEME_BG.b, 255);
        SDL_RenderFillRect(r, &win);
        SDL_SetRenderDrawColor(r, THEME_DIM_TEXT.r, THEME_DIM_TEXT.g,
                               THEME_DIM_TEXT.b, 255);
        SDL_RenderDrawRect(r, &win);
        if (ui->in_menu == MENU_MAIN) {
            static const char *items[6] =
                { "NEW", "SAVE", "LOAD", "PANIC", "EXIT", "BACK" };
            draw_text(r, 180, 132, "MENU", C_WHITE);
            for (int i = 0; i < 6; i++) {
                draw_text(r, 200, 164 + i * 30, items[i],
                          i == ui->menu_cursor ? C_GREEN : C_WHITE);
            }
        } else {
            draw_text(r, 190, 150, "RESET TO FACTORY DEFAULTS?", C_WHITE);
            draw_text(r, 230, 210, "YES",
                      ui->confirm_cursor == 0 ? C_GREEN : C_WHITE);
            draw_text(r, 300, 210, "NO",
                      ui->confirm_cursor == 1 ? C_GREEN : C_WHITE);
        }
    } else if (ui->in_menu == MENU_NAME) {
        SDL_Rect win = { 80, 60, 480, 340 };
        SDL_SetRenderDrawColor(r, THEME_BG.r, THEME_BG.g, THEME_BG.b, 255);
        SDL_RenderFillRect(r, &win);
        SDL_SetRenderDrawColor(r, THEME_DIM_TEXT.r, THEME_DIM_TEXT.g,
                               THEME_DIM_TEXT.b, 255);
        SDL_RenderDrawRect(r, &win);
        draw_text(r, 100, 72, "SAVE PRESET", C_WHITE);
        snprintf(b, sizeof b, "NAME: %s_", ui->name);
        draw_text(r, 100, 96, b, C_WHITE);
        if (ui->save_err[0])
            draw_text(r, 100, 112, ui->save_err, C_RED);
        static const char grid[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "0123456789-_ SPC DEL";
        for (int gy = 0; gy < 5; gy++) {
            for (int gx = 0; gx < 8; gx++) {
                int cell = gy * 8 + gx;
                if (cell >= 38) break;
                char lab[3];
                if (cell == 36)      snprintf(lab, sizeof lab, "SP");
                else if (cell == 37) snprintf(lab, sizeof lab, "DL");
                else { lab[0] = grid[cell]; lab[1] = '\0'; }
                int gsel = (gx == ui->grid_x && gy == ui->grid_y);
                snprintf(b, sizeof b, "%s", lab);
                draw_text(r, 104 + gx * 52, 136 + gy * 30, b,
                          gsel ? C_GREEN : C_WHITE);
            }
        }
        /* help line in this target's binding words (PLAN_KEYMAP_REBIND
         * §2): labels via keymap_label() — insert = the VAL_INC key,
         * delete = the VAL_DEC key, save = CONFIRM (Enter on desktop;
         * the handheld KB_CONFIRM is unbound, so its label is NULL and
         * the RUN label shows — A saves there), cancel = MENU */
        {
            char l_inc[16], l_dec[16], l_conf[16], l_run[16], l_menu[16];
            const char *inc = keymap_label(KB_VAL_INC, l_inc, sizeof l_inc);
            const char *dec = keymap_label(KB_VAL_DEC, l_dec, sizeof l_dec);
            const char *conf = keymap_label(KB_CONFIRM, l_conf,
                                            sizeof l_conf);
            const char *run = keymap_label(KB_RUN, l_run, sizeof l_run);
            const char *menu = keymap_label(KB_MENU, l_menu, sizeof l_menu);
            const char *save = conf ? conf : run;
            snprintf(b, sizeof b,
                     "GRID:SEL %s:INS %s:DEL %s:SAVE %s:CANCEL",
                     inc ? inc : "?", dec ? dec : "?",
                     save ? save : "?", menu ? menu : "?");
            draw_text(r, 96, 368, b, C_GRAY);
        }
    } else if (ui->in_menu == MENU_LOAD) {
        /* folder navigator: CURRENT FOLDER PATH on the title line */
        SDL_Rect win = { 20, 60, 600, 380 };
        SDL_SetRenderDrawColor(r, THEME_BG.r, THEME_BG.g, THEME_BG.b, 255);
        SDL_RenderFillRect(r, &win);
        SDL_SetRenderDrawColor(r, THEME_DIM_TEXT.r, THEME_DIM_TEXT.g,
                               THEME_DIM_TEXT.b, 255);
        SDL_RenderDrawRect(r, &win);
        draw_text(r, 36, 72, "LOAD PRESET", C_WHITE);
        snprintf(b, sizeof b, "%.58s", ui->load_dir);
        draw_text(r, 36, 90, b, C_GRAY);
        if (ui->load_count == 0) {
            draw_text(r, 36, 116,
                      ui->load_err[0] ? ui->load_err : "EMPTY FOLDER",
                      C_RED);
        } else {
            for (int i = 0; i < 14 && ui->load_top + i < ui->load_count;
                 i++) {
                int idx = ui->load_top + i;
                int sel = (idx == ui->load_cursor);
                snprintf(b, sizeof b, "%s%s",
                         ui->load_entries[idx].name,
                         ui->load_entries[idx].is_dir ? "/" : "");
                draw_text(r, 64, 112 + i * 20, b,
                          sel ? C_GREEN
                              : (ui->load_entries[idx].is_dir ? C_WHITE
                                                              : C_GRAY));
            }
        }
        if (ui->load_err[0])
            draw_text(r, 36, 416, ui->load_err, C_RED);
    } else if (ui->in_menu == MENU_QUIT) {
        draw_text(r, 280, 232, "QUITTING", C_WHITE);
    }

    SDL_RenderPresent(r);
}
