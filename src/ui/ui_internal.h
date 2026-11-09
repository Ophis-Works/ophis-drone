/* src/ui/ui_internal.h — PRIVATE interface between the ui.c translation
 * units (X3, PLAN_CODE_REVIEW_FIXES.md Phase 8 — pure structure, no
 * behavior change). ui.c keeps the state machine (tabs, focus, cursor,
 * value edits, event routing), ui_menu.c the menu/dialog logic
 * (PLAN §8.4: NEW/SAVE/LOAD/confirm/quit windows), ui_draw.c the
 * rendering (PLAN §8.6). The public API stays ui.h; everything declared
 * here is internal to src/ui and must not be used outside it. The shared
 * helpers keep their pre-split names (they were static in ui.c). */
#ifndef OPHIS_UI_INTERNAL_H
#define OPHIS_UI_INTERNAL_H

#include "ui.h"
#include "seq_items.h"

/* menu window sub-states (ui_t.in_menu is a bare int; order is part of
 * the contract — tests/test_ui_menu.c mirrors it) */
enum { MENU_OFF, MENU_MAIN, MENU_CONFIRM, MENU_NAME, MENU_LOAD, MENU_QUIT };

/* --- tab navigation / content helpers (ui.c) ------------------------- */

int       is_osc_tab(const ui_t *ui);
int       chain_of(const ui_t *ui);            /* chain index for OSC tabs */
sub_tab_t cur_sub(const ui_t *ui);
int      *cur_cursor(ui_t *ui);                /* per tab+sub-tab cursor  */
int       content_count(const ui_t *ui);      /* page's flat item count  */
item_t    content_item(const ui_t *ui, int i); /* flat item i (binds the
                                               * SEQ step si)            */
int       item_dim(const ui_t *ui, item_t it);
void      edit_item(ui_t *ui, item_t it, int dir);   /* one edit step   */

/* --- PLAN_HZ_EDIT.md: the 5-digit odometer boxes (R1) ----------------
 * The digit-box rows (digit cursor ui_t.freq_digit + the drawn digit
 * marker) are a SPEC property: item_spec(id)->nav == NAV_DIGITS (R12a,
 * seq_items.h); there is no id-list predicate. ---- */

/* R13 (PLAN_HZ_EDIT.md): the digit-marker gate, shared by ui_draw.c's
 * two draw paths — true iff `it` IS the cursor row AND the focus is on
 * the content (OSC tabs: ui->focus[chain_of(ui)] != 0; MOD/MASTER have
 * no sub-tab row -> always content) AND !item_dim AND the spec nav
 * mode == NAV_DIGITS. The row highlight keeps its own sel condition. */
int       ui_digit_marker_active(const ui_t *ui, item_t it);

/* --- M1 (PLAN_OPTIMIZATION.md O6): the value-key state machine
 * (valrepeat.c) -------------------------------------------------------- */
void      val_edit(ui_t *ui, int dir, Uint32 now);
void      valrepeat_tick(ui_t *ui, Uint32 now);

/* --- D3 (PLAN_OPTIMIZATION.md O4): the member accessors under the
 * spec table — one switch on param_kind_t each ------------------------- */
double param_get(const synth_params_t *d, param_kind_t kind, int ci,
                 int si);
void   param_set(synth_params_t *d, param_kind_t kind, int ci, int si,
                 double v);

/* --- value formatting (ui_draw.c) -------------------------------------- */

/* the item's value string for display (PLAN_WAVE_DISPLAY.md P1: hoisted
 * out of ui_draw.c's static scope so the enum-row pins can call it) */
void item_value(const ui_t *ui, item_t it, char *b, size_t blen);

/* --- spec-table access (ui.c): the item's param slot ------------------ */

double item_get(const ui_t *ui, item_t it);

/* parent mod dest of an AMT item (PITCH narrows the AMT range, §8.3) */
mod_dest_t amt_dest_of(const ui_t *ui, item_t it);

/* --- mailbox commit (ui.c): one write of the draft ------------------- */

void commit(ui_t *ui);

/* --- menu/dialog (ui_menu.c) ------------------------------------------ */

void menu_action(ui_t *ui, ui_action_t act);   /* context remap per §8.4 */
void name_insert(ui_t *ui, char c);            /* SAVE name entry (also
                                                * fed from ui_handle_event:
                                                * TEXTINPUT / Backspace)  */
void name_delete(ui_t *ui);

#endif /* OPHIS_UI_INTERNAL_H */
