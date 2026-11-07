/* src/ui/ui_menu.c — SDL2 UI menu/dialog logic (PLAN §8.4; X3,
 * PLAN_CODE_REVIEW_FIXES.md Phase 8: moved out of ui.c, pure structure,
 * no behavior change). One in_menu switch: CONFIRM = select (Enter on
 * desktop; on the handheld A keeps it via KB_RUN_CONFIRMS_MENU), Start /
 * Esc cancel, plus TRIGGER where KB_TRIGGER_CANCELS_MENU says so
 * (owner-ordered desktop rules, 2026-11-05: Esc is the desktop's only
 * cancel; Space/T are inert in menus). U4 (Phase 4): NAV/VAL keys are
 * inert in MENU_MAIN / MENU_CONFIRM / MENU_LOAD. Sub-windows:
 *   - MENU_MAIN   NEW/SAVE/LOAD/PANIC/EXIT/BACK (cursor starts on BACK).
 *   - MENU_CONFIRM  factory-defaults YES/NO; YES loads OPHIS_DEFAULTS,
 *                   one mailbox commit (H3: PANIC commits draft.run = 0 —
 *                   the UI owns RUN).
 *   - MENU_NAME   SAVE name entry: desktop text input (TEXTINPUT /
 *                 Backspace via ui_handle_event), R36S character-grid
 *                 picker (cells A-Z 0-9 - _ SPC DEL; VALUE keys insert /
 *                 delete — PLAN_KEYMAP_REBIND §4).
 *   - MENU_LOAD   folder navigator (PLAN §8.4): merged subdirectory +
 *                 *.json entry list via preset_list_dirs/preset_list,
 *                 entered with preset_path; errors inline.
 *   - MENU_QUIT   no keys (EXIT flash / WM close).
 * All UI->engine traffic is via mailbox.h channels only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "ui_internal.h"
#include "../config.h"
#include "../preset/defaults.h"
#include "../preset/preset.h"

/* --- LOAD folder navigator (PLAN §8.4) -------------------------------- */

static void menu_close(ui_t *ui);            /* defined below (menu window) */

/* merged entry list: subdirs (is_dir, incl. "..") + *.json presets,
 * one alphabetical list (strcmp on the entry name) */
static int load_cmp(const void *a, const void *b)
{
    const struct { char name[33]; int is_dir; } *x = a, *y = b;
    return strcmp(x->name, y->name);
}

static void load_refresh(ui_t *ui)
{
    ui->load_err[0] = '\0';
    ui->load_count = 0;
    ui->load_cursor = 0;
    ui->load_top = 0;

    char names[32][33];
    int n = 0;
    if (preset_list_dirs(ui->load_dir, names, &n, 32) != 0) {
        /* File-picker behavior (user order 2026-11-05): a dir that
         * cannot be opened falls back to its parent once — preset_path
         * clamps ".." at "/", so at the root there is no parent and the
         * error shows as before. */
        char parent[1024];
        if (preset_path(parent, (int)sizeof parent, ui->load_dir,
                        "..") == 0 &&
            strcmp(parent, ui->load_dir) != 0) {
            snprintf(ui->load_dir, sizeof ui->load_dir, "%s", parent);
        } else {
            snprintf(ui->load_err, sizeof ui->load_err, "CANNOT OPEN DIR");
        }
        n = 0;
        if (ui->load_err[0] == '\0' &&
            preset_list_dirs(ui->load_dir, names, &n, 32) != 0) {
            snprintf(ui->load_err, sizeof ui->load_err, "CANNOT OPEN DIR");
            n = 0;
        }
    }
    for (int i = 0; i < n && ui->load_count < 64; i++) {
        memcpy(ui->load_entries[ui->load_count].name, names[i], 33);
        ui->load_entries[ui->load_count].is_dir = 1;
        ui->load_count++;
    }
    n = 0;
    if (preset_list(ui->load_dir, names, &n, 32) != 0 && ui->load_err[0]) {
        /* dir itself unreadable: files list empty too, error already set */
        n = 0;
    }
    for (int i = 0; i < n && ui->load_count < 64; i++) {
        memcpy(ui->load_entries[ui->load_count].name, names[i], 33);
        ui->load_entries[ui->load_count].is_dir = 0;
        ui->load_count++;
    }
    if (ui->load_count > 1)
        qsort(ui->load_entries, (size_t)ui->load_count,
              sizeof ui->load_entries[0], load_cmp);
}

static void load_confirm(ui_t *ui)
{
    if (ui->load_count == 0) return;
    char name[33];
    int  is_dir;
    snprintf(name, 33, "%s", ui->load_entries[ui->load_cursor].name);
    is_dir = ui->load_entries[ui->load_cursor].is_dir;
    if (is_dir) {                               /* enter subdir / ".." up */
        char newdir[1024];
        if (preset_path(newdir, (int)sizeof newdir, ui->load_dir,
                        name) != 0) {
            snprintf(ui->load_err, sizeof ui->load_err, "PATH TOO LONG");
            return;
        }
        snprintf(ui->load_dir, sizeof ui->load_dir, "%s", newdir);
        load_refresh(ui);
        return;
    }
    char err[128];
    synth_params_t ld;
    if (preset_load(ui->load_dir, name, &ld, err, (int)sizeof err) == 0) {
        ui->draft = ld;                         /* one mailbox commit */
        commit(ui);
        menu_close(ui);
    } else {
        snprintf(ui->load_err, sizeof ui->load_err, "%.63s", err);
    }
}

/* --- menu window (one in_menu switch; RUN = confirm, TRIGGER = cancel) */

static void menu_close(ui_t *ui)
{
    if (ui->in_menu == MENU_NAME)
        SDL_StopTextInput();
    ui->in_menu = MENU_OFF;
    ui->menu_cursor = 5;
}

static void menu_confirm(ui_t *ui)             /* run current menu item */
{
    switch (ui->menu_cursor) {
    case 0:                                    /* NEW */
        ui->in_menu = MENU_CONFIRM;
        ui->confirm_cursor = 1;
        break;
    case 1:                                    /* SAVE */
        ui->in_menu = MENU_NAME;
        ui->name_len = 0;
        ui->name[0] = '\0';
        ui->save_err[0] = '\0';
        ui->grid_x = ui->grid_y = 0;
        SDL_StartTextInput();
        break;
    case 2:                                    /* LOAD: folder navigator */
        snprintf(ui->load_dir, sizeof ui->load_dir, "%s", ui->preset_dir);
        /* User order 2026-11-05: the presets dir is created on first
         * LOAD; on failure the parent fallback in load_refresh still
         * shows a sane folder. */
        preset_ensure_dir(ui->load_dir);
        load_refresh(ui);
        ui->in_menu = MENU_LOAD;
        break;
    case 3:                                    /* PANIC: immediate */
        atomic_store(&ui->mb->panic_req, 1);
        /* H3: the UI owns RUN — the engine's panic guard is one block
         * only, so the drone stays silent after PANIC only if the UI
         * commits draft.run = 0 (one mailbox commit; UI strip agrees). */
        ui->draft.run = 0;
        commit(ui);
        ui->panic_flash_until = SDL_GetTicks() + 500;
        break;
    case 4:                                    /* EXIT */
        SDL_StopTextInput();
        ui->in_menu = MENU_QUIT;
        ui->quitting = 1;
        ui->quit_at = SDL_GetTicks() + 1000;   /* QUITTING flash 1 s */
        break;
    default:                                   /* BACK */
        menu_close(ui);
        break;
    }
}

static void name_save(ui_t *ui)
{
    char err[128];
    if (ui->name_len <= 0) {
        snprintf(ui->save_err, sizeof ui->save_err, "EMPTY NAME");
        return;
    }
    if (preset_save(ui->preset_dir, ui->name, &ui->draft,
                    err, (int)sizeof err) != 0) {
        snprintf(ui->save_err, sizeof ui->save_err, "%.63s", err);
        return;                                /* errors inline, stay */
    }
    menu_close(ui);
}

/* --- name entry charset (PLAN §8.4/§9: 1..32, [A-Za-z0-9 _-]) -------- */

static int name_char_ok(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-';
}

void name_insert(ui_t *ui, char c)
{
    if (ui->name_len < 32 && name_char_ok(c)) {
        ui->name[ui->name_len++] = c;
        ui->name[ui->name_len] = '\0';
    }
}

void name_delete(ui_t *ui)
{
    if (ui->name_len > 0)
        ui->name[--ui->name_len] = '\0';
}

/* SAVE name-entry grid picker (PLAN §8.4): the grid is A-Z 0-9 - _
 * (36 chars), cell 36 = SPC, cell 37 = DEL. Invoked by the VALUE keys
 * (ACT_VAL_INC inserts, ACT_VAL_DEC deletes — PLAN_KEYMAP_REBIND §4). */
static void name_grid_pick(ui_t *ui)
{
    static const char grid[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
    int cell = ui->grid_y * 8 + ui->grid_x;
    if (cell < 36)       name_insert(ui, grid[cell]);
    else if (cell == 36) name_insert(ui, ' ');
    else if (cell == 37) name_delete(ui);
}

/* MENU_CONFIRM YES/NO apply (Enter on desktop, A on the handheld): YES
 * loads the factory defaults in one mailbox commit; both answers close. */
static void confirm_dialog_apply(ui_t *ui)
{
    if (ui->confirm_cursor == 0) {             /* YES */
        ui->draft = OPHIS_DEFAULTS;            /* one mailbox commit */
        commit(ui);
    }
    menu_close(ui);
}

void menu_action(ui_t *ui, ui_action_t act)
{
    switch (ui->in_menu) {
    case MENU_MAIN:
        switch (act) {
        case ACT_SEL_UP:   ui->menu_cursor = (ui->menu_cursor + 5) % 6; break;
        case ACT_SEL_DOWN: ui->menu_cursor = (ui->menu_cursor + 1) % 6; break;
        case ACT_CONFIRM:  menu_confirm(ui); break;   /* Enter selects */
        case ACT_RUN:      /* Space confirms only where the map says so
                            * (handheld A); desktop: inert (owner order
                            * 2026-11-05) */
            if (KB_RUN_CONFIRMS_MENU) menu_confirm(ui);
            break;
        case ACT_TAB_BACK:
        case ACT_TAB_NEXT: break;      /* Q/E inactive in menu windows */
        case ACT_NAV_LEFT:
        case ACT_NAV_RIGHT:
        case ACT_VAL_DEC:
        case ACT_VAL_INC: break;   /* U4: NAV/VAL inert (used to close) */
        default:
            /* Start/Esc always close; TRIGGER closes only where the
             * B-button cancel is part of the map (handheld) — desktop
             * cancels with Esc alone (owner order 2026-11-05). */
            if (act == ACT_TRIGGER && !KB_TRIGGER_CANCELS_MENU) break;
            menu_close(ui);
            break;
        }
        break;
    case MENU_CONFIRM:
        switch (act) {
        case ACT_SEL_UP:
        case ACT_SEL_DOWN: ui->confirm_cursor ^= 1; break;
        case ACT_CONFIRM:                      /* Enter selects YES/NO */
            confirm_dialog_apply(ui);
            break;
        case ACT_RUN:                          /* A on the handheld only */
            if (KB_RUN_CONFIRMS_MENU) confirm_dialog_apply(ui);
            break;
        case ACT_TAB_BACK:
        case ACT_TAB_NEXT: break;      /* Q/E inactive in menu windows */
        case ACT_NAV_LEFT:
        case ACT_NAV_RIGHT:
        case ACT_VAL_DEC:
        case ACT_VAL_INC: break;   /* U4: NAV/VAL inert (used to close) */
        default:
            if (act == ACT_TRIGGER && !KB_TRIGGER_CANCELS_MENU) break;
            menu_close(ui);
            break;
        }
        break;
    case MENU_NAME:
        switch (act) {
        case ACT_SEL_UP:   if (ui->grid_y > 0) ui->grid_y--; break;
        case ACT_SEL_DOWN:
            /* grid is 5 rows: rows 0-3 hold cells 0..31, row 4 only
             * cells 32..37 — entering row 4 clamps x so the cursor
             * stays on drawable cells (cell <= 37) */
            if (ui->grid_y < 4) {
                ui->grid_y++;
                if (ui->grid_y == 4 && ui->grid_x > 5)
                    ui->grid_x = 5;
            }
            break;
        case ACT_NAV_LEFT: if (ui->grid_x > 0) ui->grid_x--; break;
        case ACT_NAV_RIGHT:
            if (ui->grid_x < (ui->grid_y == 4 ? 5 : 7)) ui->grid_x++;
            break;
        case ACT_VAL_INC:  name_grid_pick(ui); break; /* value key inserts */
        case ACT_VAL_DEC:  name_delete(ui); break;    /* value key deletes */
        case ACT_CONFIRM:  name_save(ui); break;   /* Enter = save */
        case ACT_RUN:      /* handheld A = save; desktop Space: inert */
            if (KB_RUN_CONFIRMS_MENU) name_save(ui);
            break;
        case ACT_TAB_BACK:
        case ACT_TAB_NEXT: break;      /* Q/E inactive in menu windows */
        default:
            if (act == ACT_TRIGGER && !KB_TRIGGER_CANCELS_MENU) break;
            menu_close(ui);            /* Esc cancel (TRIGGER on handheld) */
            break;
        }
        break;
    case MENU_LOAD:
        if (act == ACT_TAB_BACK || act == ACT_TAB_NEXT)
            break;                     /* Q/E inactive in menu windows */
        if (ui->load_count > 0) {
            switch (act) {
            case ACT_SEL_UP:
                ui->load_cursor =
                    (ui->load_cursor + ui->load_count - 1) % ui->load_count;
                break;
            case ACT_SEL_DOWN:
                ui->load_cursor = (ui->load_cursor + 1) % ui->load_count;
                break;
            case ACT_CONFIRM:
            case ACT_RUN:
                /* Enter selects (desktop); A confirms (handheld via
                 * KB_RUN_CONFIRMS_MENU). dir: enter; preset: load/close */
                if (act == ACT_CONFIRM || KB_RUN_CONFIRMS_MENU)
                    load_confirm(ui);
                break;
            case ACT_NAV_LEFT:
            case ACT_NAV_RIGHT:
            case ACT_VAL_DEC:
            case ACT_VAL_INC: break;   /* U4: NAV/VAL inert (used to close) */
            default:
                if (act == ACT_TRIGGER && !KB_TRIGGER_CANCELS_MENU) break;
                menu_close(ui);        /* Esc (TRIGGER on handheld) */
                break;
            }
            /* visible window (14 rows) follows the selection */
            if (ui->load_cursor < ui->load_top)
                ui->load_top = ui->load_cursor;
            if (ui->load_cursor >= ui->load_top + 14)
                ui->load_top = ui->load_cursor - 13;
        } else {
            /* empty dir (incl. parent-fallback view): the close set
             * mirrors the branches above — Esc always; Enter closes
             * (desktop) / A and TRIGGER close (handheld flags) */
            if (act == ACT_MENU || act == ACT_CONFIRM ||
                (KB_RUN_CONFIRMS_MENU && act == ACT_RUN) ||
                (KB_TRIGGER_CANCELS_MENU && act == ACT_TRIGGER))
                menu_close(ui);
        }
        break;
    default:                                   /* MENU_QUIT: no keys */
        break;
    }
}
