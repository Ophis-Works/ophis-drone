/* tests/test_ui_menu.c — menu_action() contracts: in MENU_MAIN /
 * MENU_CONFIRM / MENU_LOAD the NAV and VAL actions are NO-OPS; on desktop
 * ACT_CONFIRM (Enter) confirms, ACT_MENU (Esc) is the ONLY cancel, and
 * ACT_RUN (Space) / ACT_TRIGGER (T) do nothing inside menus. MENU_NAME
 * keeps its NAV (grid cursor) / VAL (insert/delete) handlers on purpose:
 * the SAVE grid picker is the R36S name-entry path — the test pins that
 * divergence so it cannot regress either way.
 * SDL-linked (ui.c is an SDL TU): SDL_Init video with the dummy driver
 * keeps SDL_StartTextInput / SDL_GetTicks headless; no window is shown.
 * Presets are staged in the dir preset_dir() itself resolves (beside the
 * test binary, absolute — CWD-independent) so the LOAD navigator has both
 * a non-empty and an empty case without touching the real presets. */
#include "harness.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>

#include "../src/config.h"
#include "../src/ui/seq_items.h"
/* param accessors (ui.c) — the MENU_* mirror below keeps ui_internal.h
 * out of this TU */
#include "../src/dsp/types.h"
#include "../src/preset/schema.h"
#include "../src/ui/seq_items.h"
extern double param_get(const synth_params_t *d, param_kind_t kind,
                        int ci, int si);
extern void   param_set(synth_params_t *d, param_kind_t kind, int ci,
                        int si, double v);
#include "../src/preset/defaults.h"
#include "../src/preset/preset.h"
#include "../src/ui/ui.h"

/* ui.h exposes in_menu as a bare int; these mirror ui.c's internal menu
 * sub-state enum (MENU_OFF..MENU_QUIT — fixed order, see ui.c). */
enum { MENU_OFF = 0, MENU_MAIN, MENU_CONFIRM, MENU_NAME, MENU_LOAD };

static mailbox_t mb;
static ui_t ui;
static char stage[1024];          /* dir preset_dir() resolves (absolute) */

static void fresh_ui(void) { ui_init(&ui, &mb); }

/* Remove every entry in dir, then the dir itself (test hygiene: an
 * interrupted earlier run must not leave state behind). */
static void purge_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[1400];               /* dir (<=1023) + '/' + d_name */
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 ||
            strcmp(e->d_name, "..") == 0) continue;
        int nw = snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (nw < 0 || (size_t)nw >= sizeof p)
            continue;              /* failed or truncated: nothing to
                                    * unlink by that name */
        unlink(p);
    }
    closedir(d);
    rmdir(dir);
}

/* Open MENU_MAIN and confirm the item at idx (0 NEW, 1 SAVE, 2 LOAD). */
static void open_item(int idx)
{
    fresh_ui();
    ui_handle_action(&ui, ACT_MENU);           /* opens MAIN, cursor = 5 */
    while (ui.menu_cursor != idx)
        ui_handle_action(&ui, ACT_SEL_UP);     /* wrap upward to idx */
    ui_handle_action(&ui, ACT_CONFIRM);        /* Enter -> sub-state */
}

static void test_main_nav_val_inert(void)
{
    fresh_ui();
    ui_handle_action(&ui, ACT_MENU);
    CHECK(ui.in_menu == MENU_MAIN);
    /* NAV/VAL do nothing — in particular they do NOT close. */
    ui_handle_action(&ui, ACT_NAV_LEFT);  CHECK(ui.in_menu == MENU_MAIN);
    ui_handle_action(&ui, ACT_NAV_RIGHT); CHECK(ui.in_menu == MENU_MAIN);
    ui_handle_action(&ui, ACT_VAL_INC);   CHECK(ui.in_menu == MENU_MAIN);
    ui_handle_action(&ui, ACT_VAL_DEC);   CHECK(ui.in_menu == MENU_MAIN);
    /* Q/E inactive in menu windows (pinned, already true). */
    ui_handle_action(&ui, ACT_TAB_BACK);  CHECK(ui.in_menu == MENU_MAIN);
    ui_handle_action(&ui, ACT_TAB_NEXT);  CHECK(ui.in_menu == MENU_MAIN);
    /* Up/down still navigate the item list. */
    ui.menu_cursor = 5;
    ui_handle_action(&ui, ACT_SEL_DOWN);  CHECK(ui.menu_cursor == 0);
    /* Desktop: Esc is the ONLY close; Space
     * (ACT_RUN) and T (ACT_TRIGGER) do nothing inside menus. */
    ui_handle_action(&ui, ACT_TRIGGER);   CHECK(ui.in_menu == MENU_MAIN);
    ui_handle_action(&ui, ACT_RUN);       CHECK(ui.in_menu == MENU_MAIN);
    CHECK(ui.menu_cursor == 0);           /* Space confirmed nothing */
    ui_handle_action(&ui, ACT_MENU);      /* Esc closes */
    CHECK(ui.in_menu == MENU_OFF);
    ui_handle_action(&ui, ACT_MENU);      /* and reopens */
    CHECK(ui.in_menu == MENU_MAIN);
    ui_handle_action(&ui, ACT_MENU);      /* Esc closes again */
    CHECK(ui.in_menu == MENU_OFF);
}

static void test_confirm_nav_val_inert(void)
{
    open_item(0);                          /* NEW -> confirm dialog */
    CHECK(ui.in_menu == MENU_CONFIRM);
    CHECK(ui.confirm_cursor == 1);
    ui_handle_action(&ui, ACT_NAV_LEFT);  CHECK(ui.in_menu == MENU_CONFIRM);
    ui_handle_action(&ui, ACT_NAV_RIGHT); CHECK(ui.in_menu == MENU_CONFIRM);
    ui_handle_action(&ui, ACT_VAL_INC);   CHECK(ui.in_menu == MENU_CONFIRM);
    ui_handle_action(&ui, ACT_VAL_DEC);   CHECK(ui.in_menu == MENU_CONFIRM);
    ui_handle_action(&ui, ACT_TAB_BACK);  CHECK(ui.in_menu == MENU_CONFIRM);
    ui_handle_action(&ui, ACT_SEL_UP);    CHECK(ui.confirm_cursor == 0);
    ui_handle_action(&ui, ACT_RUN);       /* Space: inert on desktop */
    CHECK(ui.in_menu == MENU_CONFIRM);
    ui_handle_action(&ui, ACT_CONFIRM);   /* Enter: YES -> close */
    CHECK(ui.in_menu == MENU_OFF);
}

static void test_name_grid_picker_kept(void)
{
    open_item(1);                          /* SAVE -> name entry */
    CHECK(ui.in_menu == MENU_NAME);
    CHECK(SDL_IsTextInputActive());
    /* NAV moves the grid cursor (kept deliberately — R36S name entry). */
    ui_handle_action(&ui, ACT_NAV_RIGHT); CHECK(ui.grid_x == 1);
    ui_handle_action(&ui, ACT_NAV_LEFT);  CHECK(ui.grid_x == 0);
    /* VAL keys insert/delete at the cursor. */
    ui_handle_action(&ui, ACT_VAL_INC);   CHECK(ui.name_len == 1);
    ui_handle_action(&ui, ACT_VAL_DEC);   CHECK(ui.name_len == 0);
    /* Enter confirms (saves), Space does nothing, Esc cancels + stops
     * input. */
    snprintf(ui.name, sizeof ui.name, "menutest");
    ui.name_len = (int)strlen(ui.name);
    ui_handle_action(&ui, ACT_RUN);       CHECK(ui.in_menu == MENU_NAME);
    ui_handle_action(&ui, ACT_CONFIRM);
    CHECK(ui.in_menu == MENU_OFF);
    CHECK(!SDL_IsTextInputActive());
}

/* regression pin (SDL >= 2.32 enables text input by default, so
 * SDL_IsTextInputActive() is SDL_TRUE from startup): main.c's keyboard
 * gate must key off OUR name-entry state, exposed as
 * ui_text_entry_active() — not on SDL's flag. */
static void test_text_entry_state_tracker(void)
{
    fresh_ui();
    CHECK(!ui_text_entry_active(&ui));
    open_item(1);                          /* SAVE -> name entry */
    CHECK(ui_text_entry_active(&ui));
    ui_handle_action(&ui, ACT_MENU);       /* Esc/Start cancels */
    CHECK(!ui_text_entry_active(&ui));
    CHECK(!SDL_IsTextInputActive());
}

/* SAVE opens with the
 * last SUCCESSFULLY saved name prefilled; failed saves record nothing;
 * the record updates on each success. The record lives in ui_t (one
 * instance per app session), so the reopens here stay on ONE ui — the
 * fresh_ui-per-scenario helper would wipe the memory under test. */
static void save_reopen(void)              /* same-session MENU -> SAVE */
{
    ui_handle_action(&ui, ACT_MENU);           /* opens MAIN on any screen */
    while (ui.menu_cursor != 1)
        ui_handle_action(&ui, ACT_SEL_UP);
    ui_handle_action(&ui, ACT_CONFIRM);        /* -> MENU_NAME */
}

static void test_save_prefills_last_name(void)
{
    purge_dir(stage);
    /* 1. fresh session + failed save (EMPTY NAME): nothing recorded,
     *    the next SAVE opens blank */
    open_item(1);
    CHECK(ui.in_menu == MENU_NAME);
    ui_handle_action(&ui, ACT_CONFIRM);    /* empty name: stays */
    CHECK(ui.in_menu == MENU_NAME);
    ui_handle_action(&ui, ACT_MENU);       /* cancel */
    save_reopen();
    CHECK(ui.name_len == 0 && ui.name[0] == '\0');
    ui_handle_action(&ui, ACT_MENU);       /* cancel */

    /* 2. a successful save records the name; SAVE opens typed */
    save_reopen();                             /* back into name entry */
    snprintf(ui.name, sizeof ui.name, "abc");
    ui.name_len = 3;
    ui_handle_action(&ui, ACT_CONFIRM);
    CHECK(ui.in_menu == MENU_OFF);
    save_reopen();
    CHECK(ui.name_len == 3);
    CHECK(strcmp(ui.name, "abc") == 0);

    /* 3. the record follows the newest success */
    snprintf(ui.name, sizeof ui.name, "dude");
    ui.name_len = 4;
    ui_handle_action(&ui, ACT_CONFIRM);
    CHECK(ui.in_menu == MENU_OFF);
    save_reopen();
    CHECK(ui.name_len == 4);
    CHECK(strcmp(ui.name, "dude") == 0);
    ui_handle_action(&ui, ACT_MENU);
    purge_dir(stage);
}

static void test_load_nav_val_inert(void)
{
    open_item(2);                          /* LOAD -> folder navigator */
    CHECK(ui.in_menu == MENU_LOAD);
    CHECK(ui.load_count >= 1);             /* non-empty: preset seeded */
    {
        int cur = ui.load_cursor;
        ui_handle_action(&ui, ACT_NAV_LEFT);
        CHECK(ui.in_menu == MENU_LOAD);
        CHECK(ui.load_cursor == cur);
        ui_handle_action(&ui, ACT_NAV_RIGHT);
        CHECK(ui.in_menu == MENU_LOAD);
        CHECK(ui.load_cursor == cur);
        ui_handle_action(&ui, ACT_VAL_INC); CHECK(ui.in_menu == MENU_LOAD);
        ui_handle_action(&ui, ACT_VAL_DEC); CHECK(ui.in_menu == MENU_LOAD);
        ui_handle_action(&ui, ACT_SEL_DOWN);/* up/down still navigate */
        CHECK(ui.load_cursor == (cur + 1) % ui.load_count);
    }
    /* Desktop: Space loads nothing, T cancels nothing; Esc closes. */
    ui_handle_action(&ui, ACT_RUN);       CHECK(ui.in_menu == MENU_LOAD);
    ui_handle_action(&ui, ACT_TRIGGER);   CHECK(ui.in_menu == MENU_LOAD);
    ui_handle_action(&ui, ACT_MENU);      CHECK(ui.in_menu == MENU_OFF);
}

/* the navigator behaves like a file picker.
 * An existing-but-empty presets dir lists exactly ".." (up); a MISSING
 * presets dir is created by the LOAD open; an UNCREATABLE presets dir
 * falls back to its parent. Closes go through ACT_MENU (Esc) — stable
 * across both targets. */
/* The value keys NEVER trigger — no retrig,
 * no TRIG flash, no mailbox writes. The single trigger path in the
 * whole UI is ACT_TRIGGER (the TRIGGER key): inclusion principle,
 * pinned here. The synthesized hold (400 ms delay, 120 ms rate) sweeps
 * VALUES only. */
static void test_valkey_no_retrig(void)
{
    fresh_ui();
    CHECK(atomic_load(&mb.retrig_req) == 0);
    CHECK(atomic_load(&mb.trigger_req) == 0);
    ui_handle_action(&ui, ACT_SEL_DOWN);       /* enter content (focus) */
    synth_params_t before = ui.draft;
    ui_handle_action(&ui, ACT_VAL_INC);        /* press + hold */
    CHECK(ui.val_held_dir == +1);
    CHECK(memcmp(&ui.draft, &before, sizeof before) != 0); /* applied */
    CHECK(atomic_load(&mb.retrig_req) == 0);   /* NO chain retrig */
    CHECK(atomic_load(&mb.trigger_req) == 0);  /* whole-synth untouched */
    CHECK(ui.trig_flash_until == 0);           /* NO TRIG flash */

    Uint32 t0 = SDL_GetTicks();
    synth_params_t once = ui.draft;
    ui_tick(&ui, t0 + 400);                    /* first hold repeat */
    CHECK(memcmp(&ui.draft, &once, sizeof once) != 0);     /* swept */
    CHECK(atomic_load(&mb.retrig_req) == 0);
    CHECK(atomic_load(&mb.trigger_req) == 0);
    CHECK(ui.trig_flash_until == 0);
    CHECK(ui.val_next_repeat >= t0 + 400 + 120);
    ui_tick(&ui, t0 + 525);                    /* second repeat */
    CHECK(atomic_load(&mb.retrig_req) == 0);
    CHECK(atomic_load(&mb.trigger_req) == 0);
    CHECK(ui.trig_flash_until == 0);

    SDL_Event e;                               /* release ends the hold */
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_x;                 /* the value key released */
    ui_handle_event(&ui, &e);
    CHECK(ui.val_held_dir == 0);
    ui_tick(&ui, t0 + 1000);
    CHECK(atomic_load(&mb.retrig_req) == 0);   /* nothing after release */
    CHECK(atomic_load(&mb.trigger_req) == 0);

    /* DESKTOP keyboard release (the runaway bug: val_held_dir was only
     * cleared on controller button-up, so a Z/X hold on desktop kept
     * firing value+retrig every 120 ms FOREVER after the key was
     * released) */
    fresh_ui();
    ui_handle_action(&ui, ACT_SEL_DOWN);       /* enter content (focus) */
    ui_handle_action(&ui, ACT_VAL_INC);        /* Z/X keydown arms */
    CHECK(ui.val_held_dir == +1);
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_x;                 /* the value key released */
    ui_handle_event(&ui, &e);
    CHECK(ui.val_held_dir == 0);
    /* a non-value KEYUP must NOT clear the hold */
    ui_handle_action(&ui, ACT_VAL_DEC);
    CHECK(ui.val_held_dir == -1);
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYUP;
    e.key.keysym.sym = SDLK_q;
    ui_handle_event(&ui, &e);
    CHECK(ui.val_held_dir == -1);
    /* losing window focus or the controller must clear a stuck hold */
    memset(&e, 0, sizeof e);
    e.type = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    ui_handle_event(&ui, &e);
    CHECK(ui.val_held_dir == 0);
    ui_handle_action(&ui, ACT_VAL_INC);
    CHECK(ui.val_held_dir == +1);
    memset(&e, 0, sizeof e);
    e.type = SDL_CONTROLLERDEVICEREMOVED;
    e.cdevice.which = 0;
    ui_handle_event(&ui, &e);
    CHECK(ui.val_held_dir == 0);

    fresh_ui();                                /* DEC symmetric */
    ui_handle_action(&ui, ACT_SEL_DOWN);       /* enter content (focus) */
    before = ui.draft;
    ui_handle_action(&ui, ACT_VAL_DEC);
    CHECK(ui.val_held_dir == -1);
    CHECK(memcmp(&ui.draft, &before, sizeof before) != 0);
    CHECK(atomic_load(&mb.retrig_req) == 0);
    CHECK(atomic_load(&mb.trigger_req) == 0);
    CHECK(ui.trig_flash_until == 0);

    /* INCLUSION: the one trigger path is the TRIGGER key itself. */
    ui_handle_action(&ui, ACT_TRIGGER);
    CHECK(atomic_load(&mb.trigger_req) == 1);
    CHECK(ui.trig_flash_until != 0);
}

static void test_load_empty_shows_up(void){
    purge_dir(stage);                      /* exists-but-empty */
    open_item(2);
    CHECK(ui.in_menu == MENU_LOAD);
    CHECK(ui.load_count == 1);
    CHECK(strcmp(ui.load_entries[0].name, "..") == 0);
    CHECK(ui.load_err[0] == '\0');
    ui_handle_action(&ui, ACT_NAV_LEFT);  CHECK(ui.in_menu == MENU_LOAD);
    CHECK(ui.load_cursor == 0);
    ui_handle_action(&ui, ACT_VAL_INC);   CHECK(ui.in_menu == MENU_LOAD);
    ui_handle_action(&ui, ACT_MENU);      CHECK(ui.in_menu == MENU_OFF);
}

static void test_load_missing_dir_created(void)
{
    rmdir(stage);                          /* gone entirely */
    open_item(2);
    CHECK(ui.in_menu == MENU_LOAD);
    struct stat st;
    CHECK(stat(stage, &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(ui.load_count == 1);             /* ".." only: fresh dir */
    CHECK(ui.load_err[0] == '\0');
    ui_handle_action(&ui, ACT_MENU);      CHECK(ui.in_menu == MENU_OFF);
}

static void test_load_uncreatable_falls_back(void)
{
    char parent[1024];
    snprintf(parent, sizeof parent, "%s", stage);
    char *slash = strrchr(parent, '/');
    CHECK(slash != NULL);
    *slash = '\0';
    CHECK(rmdir(stage) == 0);              /* gone (needs parent write) */
    /* Locking the parent needs the right to chmod it — a normal build
     * (exe in build/tests) has it; exotic exe locations (e.g. /tmp in
     * an ASan run) may not, and there the simulation cannot be staged:
     * skip rather than fake it. */
    if (chmod(parent, 0555) != 0) {
        printf("  (skip uncreatable-dir fallback: cannot chmod %s)\n",
               parent);
        return;
    }
    open_item(2);
    CHECK(ui.in_menu == MENU_LOAD);
    CHECK(strcmp(ui.load_dir, stage) != 0);/* parent opened instead */
    CHECK(ui.load_count >= 1);
    CHECK(ui.load_err[0] == '\0');
    ui_handle_action(&ui, ACT_MENU);      CHECK(ui.in_menu == MENU_OFF);
    CHECK(chmod(parent, 0755) == 0);
}

/* ---- item_value renders the DRAFT's
 * enum-valued rows through the display functions. The WAVE / DEST / FILT
 * / POLES cases snprintf THROUGH the same buffer they format from
 * (overlapping in/out, C11 7.21.6.6p4) — those four rows draw empty/
 * garbage if done wrong; these pins guard the direct-call
 * fix. The already-direct cases (ITF_XSRC / ITF_TGT) are guarded against
 * a sloppy rewrite. item_value is hoisted out of ui_draw.c's static scope
 * as part of the fix (declared in ui_internal.h); this TU mirrors that
 * declaration locally, like param_get/param_set above. ---- */
extern void item_value(const ui_t *ui, item_t it, char *b, size_t blen);
extern item_t content_item(const ui_t *ui, int i);   /* flat item i (ui.c) */
/* edit-surface mirrors (ui_internal.h decls, same
 * pattern as item_value/content_item above) */
extern void   edit_item(ui_t *ui, item_t it, int dir);
extern double item_get(const ui_t *ui, item_t it);

static void test_item_value_enum_rows(void)
{
    /* a raw draft (no ui_init): tab/sub set so content_item resolves the
     * real page tables; sub[0] carries the factory defaults */
    ui_t u;
    memset(&u, 0, sizeof u);
    u.tab = TAB_OSC1;
    u.sub[0] = SUB_OSC;
    u.draft.chain[0].sub[0].div = 1;
    u.draft.chain[0].sub[0].wave = WAVE_SINE;
    u.draft.chain[0].sub[0].duty = 50;
    u.draft.chain[0].sub[0].level = 0;
    u.draft.chain[0].wave = WAVE_SAW;
    u.draft.chain[0].fmode = FILT_HP;
    u.draft.chain[0].poles = 24;
    u.draft.xmod[0].src = 1;
    u.draft.xmod[0].dest = MOD_RING;
    u.draft.xmod[0].tgt = 2;

    char b[32];
    item_t it;

    /* the four snprintf-aliasing cases — EMPTY/garbage if done wrong */
    it = content_item(&u, 1);              /* MAIN_ITEMS[1] = WAVE */
    CHECK(it.id == IT_OSC_WAVE);
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "SAW") == 0);

    u.tab = TAB_MOD;
    it = content_item(&u, 3);              /* MOD_ITEMS[3] = DEST, slot 0
                                            * (the 6-row SLOT model:
                                            * SLOT · SOURCE · TARGET ·
                                            * DEST · DEPTH · LEVEL) */
    CHECK(it.id == IT_XMOD_DEST && it.si == 0);
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "RING") == 0);

    /* the SLOT row shows 1-based ("SLOT %d",
     * v+1) — green-today guard: written with
     * the implementation, not red-first */
    it = content_item(&u, 0);              /* MOD_ITEMS[0] = SLOT */
    CHECK(it.id == IT_MOD_SLOT && it.si == 0);
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "SLOT 1") == 0);

    /* the NEW dest strings ride the same
     * table-driven display — "flt_env" -> "FLT ENV", "adsr_a" -> "ADSR A"
     * ('_' -> ' ', uppercased). Red until dest_tab gains the entries. */
    it = content_item(&u, 3);              /* MOD_ITEMS[3] = DEST again */
    u.draft.xmod[0].dest = MOD_FENV;
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "FLT ENV") == 0);
    u.draft.xmod[0].dest = MOD_ADSR_A;
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "ADSR A") == 0);

    /* SEQ DIV shows "/n" (matches the sub-oscillator DIV display).
     * Red until ui_draw.c's ITF_XDIV switches "x%d" -> "/%d". */
    u.tab = TAB_OSC1;                      /* SEQ ids live on OSC tabs */
    u.draft.seq[0].div = 4;
    it = (item_t){ NULL, IT_SEQ_DIV, 0 };
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "/4") == 0);

    u.tab = TAB_OSC1;
    u.sub[0] = SUB_ENVFLT;
    it = content_item(&u, 4);              /* FILT_ITEMS[0] = MODE */
    CHECK(it.id == IT_FILT_MODE);
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "HP") == 0);

    it = content_item(&u, 8);              /* FILT_ITEMS[4] = POLES */
    CHECK(it.id == IT_FILT_POLES);
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "24DB") == 0);

    /* the previously-working direct cases stay correct */
    u.tab = TAB_MOD;
    it = content_item(&u, 1);              /* SOURCE, slot 0 */
    CHECK(it.id == IT_XMOD_SRC && it.si == 0);
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "OSC 2") == 0);        /* src 1 = chain B = OSC 2 */

    it = content_item(&u, 2);              /* TARGET (TGT), slot 0 */
    CHECK(it.id == IT_XMOD_TGT && it.si == 0);
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "OSC 3") == 0);        /* tgt 2 -> "OSC %d" (v + 1) */
}

/* ---- the digit-box rows render
 * 5 integer digits + PERIOD + 2 decimals — "00220.00 Hz", "20000.00 Hz"
 * at the ceiling, "00000.01 Hz" at the 0.01 floor, a preset-loaded 55.5
 * as "00055.50 Hz" — on the freq rows (OSC FREQ, FILTER CUT, DELAY
 * TONE); ui_draw.c's item_value formats them "%05d.%02d Hz". ---- */
static void test_item_value_freq_odometer(void)
{
    ui_t u;
    memset(&u, 0, sizeof u);
    u.tab = TAB_OSC1;
    u.sub[0] = SUB_OSC;
    char b[32];
    item_t it = content_item(&u, 0);       /* MAIN_ITEMS[0] = FREQ */
    CHECK(it.id == IT_OSC_FREQ);

    u.draft.chain[0].freq_hz = 220.0f;
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "00220.00 Hz") == 0);

    u.draft.chain[0].freq_hz = 20000.0f;   /* the ceiling */
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "20000.00 Hz") == 0);

    u.draft.chain[0].freq_hz = 0.01f;      /* the 0.01 Hz edit floor */
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "00000.01 Hz") == 0);

    u.draft.chain[0].freq_hz = 55.5f;      /* fraction SHOWN, not rounded */
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "00055.50 Hz") == 0);

    u.sub[0] = SUB_ENVFLT;                 /* FILTER CUT: same box */
    it = content_item(&u, 5);              /* ADSR(4) + FILT_ITEMS[1] */
    CHECK(it.id == IT_FILT_CUT);
    u.draft.chain[0].fc = 1500.0f;
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "01500.00 Hz") == 0);

    u.tab = TAB_MASTER;                    /* DELAY TONE: same box */
    it = content_item(&u, 8);              /* MASTER_ITEMS[8] = TONE */
    CHECK(it.id == IT_DLY_TONE);
    u.draft.delay.tone = 8500;
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "08500.00 Hz") == 0);
}

/* ---- the fractional-
 * preservation rule in the centi-Hz space — a preset-loaded 55.5 Hz
 * SHOWS its fraction ("00055.50 Hz"; no rounding — the
 * value is exactly 5550 centi-Hz) and item_get still returns 55.5; the
 * first 1s-digit step snaps through centi-Hz ints (55.50 -> 56.50).
 * Driven through edit_item (ui_internal.h surface, mirrored above);
 * the digit cursor is explicitly parked on the 1s digit (index 4 of
 * 0..6 from the left — a memset ui leaves freq_digit 0, which is the
 * 10000s digit in the new space). Red until the centi-Hz edit wiring
 * lands. ---- */
static void test_freq_fractional_rounding(void)
{
    ui_t u;
    memset(&u, 0, sizeof u);
    u.mb = &mb;
    u.tab = TAB_OSC1;
    u.sub[0] = SUB_OSC;
    item_t it = content_item(&u, 0);       /* MAIN_ITEMS[0] = FREQ */
    CHECK(it.id == IT_OSC_FREQ);

    u.draft.chain[0].freq_hz = 55.5f;
    u.freq_digit = 4;                      /* the 1s digit (first
                                            * non-decimal digit) */
    char b[32];
    item_value(&u, it, b, sizeof b);
    CHECK(strcmp(b, "00055.50 Hz") == 0);  /* fraction displayed */
    CHECK(fexact(item_get(&u, it), 55.5));  /* value keeps the fraction */

    edit_item(&u, it, +1);                 /* one 1s-digit step */
    CHECK(fexact(item_get(&u, it), 56.5));  /* 5550 +100 centi-Hz */
}

/* ---- EVERY numeric edit field rolls
 * over at its bounds instead of clamping — driven through edit_item
 * (the same edit surface the val keys reach via val_edit). Red until
 * the generic clamp branch of edit_item becomes a wrap branch. ---- */
static void test_edit_rollover(void)
{
    ui_t u;
    memset(&u, 0, sizeof u);
    u.mb = &mb;
    u.tab = TAB_OSC1;                      /* chain_of -> 0 */
    u.draft.chain[0].wave = WAVE_SQUARE;   /* DUTY undimmed */
    item_t it;

    /* VEL: percent 0..100 */
    it = (item_t){ NULL, IT_VEL, 0 };
    param_set(&u.draft, PK_CHAIN_VEL, 0, 0, 100);
    edit_item(&u, it, +1);
    CHECK(fexact(item_get(&u, it), 0));
    param_set(&u.draft, PK_CHAIN_VEL, 0, 0, 0);
    edit_item(&u, it, -1);
    CHECK(fexact(item_get(&u, it), 100));

    /* ENV: signed percent -100..+100 (+1 above 100 -> -100) */
    it = (item_t){ NULL, IT_FILT_ENV, 0 };
    param_set(&u.draft, PK_CHAIN_ENV, 0, 0, 100);
    edit_item(&u, it, +1);
    CHECK(fexact(item_get(&u, it), -100));

    /* DUTY: 5..95 (95 -> 5) */
    it = (item_t){ NULL, IT_OSC_DUTY, 0 };
    param_set(&u.draft, PK_CHAIN_DUTY, 0, 0, 95);
    edit_item(&u, it, +1);
    CHECK(fexact(item_get(&u, it), 5));

    /* BPM: 1..500 (500 -> 1) */
    it = (item_t){ NULL, IT_BPM, 0 };
    param_set(&u.draft, PK_TOP_BPM, 0, 0, 500);
    edit_item(&u, it, +1);
    CHECK(fexact(item_get(&u, it), 1));

    /* MASTER: percent 0..100 (0 -> 100) */
    it = (item_t){ NULL, IT_MASTER, 0 };
    param_set(&u.draft, PK_TOP_MASTER, 0, 0, 0);
    edit_item(&u, it, -1);
    CHECK(fexact(item_get(&u, it), 100));

    /* SEQ DIV: 1..16 (guards the uniform rule on the "already wraps" row) */
    it = (item_t){ NULL, IT_SEQ_DIV, 0 };
    param_set(&u.draft, PK_SEQ_DIV, 0, 0, 16);
    edit_item(&u, it, +1);
    CHECK(fexact(item_get(&u, it), 1));

    /* ADSR S: percent 0..100 (top -> 0) */
    it = (item_t){ NULL, IT_S, 0 };
    param_set(&u.draft, PK_ADSR_S, 0, 0, 100);
    edit_item(&u, it, +1);
    CHECK(fexact(item_get(&u, it), 0));

    /* MIXVOL: percent 0..100 per chain (si IS the chain index) */
    it = (item_t){ NULL, IT_MIXVOL, 1 };
    param_set(&u.draft, PK_CHAIN_MIXVOL, 0, 1, 100);
    edit_item(&u, it, +1);
    CHECK(fexact(item_get(&u, it), 0));
    param_set(&u.draft, PK_CHAIN_MIXVOL, 0, 1, 0);
    edit_item(&u, it, -1);
    CHECK(fexact(item_get(&u, it), 100));
}

/* ---- the SEQ page joins the
 * odometer scheme with ROW-DEPENDENT LEFT/RIGHT, dispatched TABLE-DRIVEN
 * off the spec's nav mode — STEP row: the rectangle moves; PITCH row:
 * the DIGIT cursor moves (never the rectangle); VEL row: nothing moves.
 * Driven through ui_handle_action on a raw SEQ-page draft (memset-zeroed
 * + the same tab/sub setup pattern the enum-row pins above use; focus 1
 * puts the cursor ON content, cursor == the SEQ page's flat row, which
 * is 1:1 with the drawn rows). Red until R12a's nav_mode dispatch
 * replaces the current "every SEQ row moves the rectangle" branch
 * (src/ui/ui.c ACT_NAV_LEFT/RIGHT). ---- */

/* a SEQ-page ui on content row `row` (SEQ_ITEMS flat idx: 4 = STEP,
 * 5 = PITCH, 6 = VEL), digit cursor at `fd`, rectangle at `sel` */
static ui_t seq_ui(int row, int fd, int sel)
{
    ui_t u;
    memset(&u, 0, sizeof u);
    u.mb = &mb;
    u.tab = TAB_OSC1;
    u.sub[0] = SUB_SEQ;
    u.focus[0] = 1;                    /* content, not the sub-tab row */
    /* the cursor is persisted per tab+sub-tab (ui.h layout note):
     * index = tab*SUB_COUNT + sub — TAB_OSC1/SUB_SEQ is slot 2, NOT 0 */
    u.cursor[(int)TAB_OSC1 * SUB_COUNT + (int)SUB_SEQ] = row;
    u.freq_digit = fd;
    u.seq_sel[0] = sel;                /* chain A's rectangle */
    return u;
}

static void test_seq_nav_rows(void)
{
    item_t it;
    char b[32];

    /* PITCH row renders ZERO-PADDED with decimals (the ITF_HZ
     * box): the bound step (si = seq_sel) shows "00220.00 Hz" */
    {
        ui_t u = seq_ui(5, 0, 3);
        u.draft.seq[0].steps[3].pitch_hz = 220.0f;
        it = content_item(&u, 5);
        CHECK(it.id == IT_STEP_PITCH && it.si == 3);
        item_value(&u, it, b, sizeof b);
        CHECK(strcmp(b, "00220.00 Hz") == 0);
    }

    /* PITCH row: ROW ENTRY resets the digit cursor to index 4 — the 1s
     * digit, the FIRST NON-DECIMAL digit (7 digits 0..6 counted
     * from the left, the period cell skipped). Red until the reset
     * constant moves off 0. */
    {
        ui_t u = seq_ui(5, 6, 3);
        ui_handle_action(&u, ACT_SEL_DOWN);    /* -> VEL row (no box) */
        ui_handle_action(&u, ACT_SEL_UP);      /* row entry: PITCH again */
        CHECK(u.freq_digit == 4);      /* the 1s digit, index 4 of 0..6 */
        CHECK(u.seq_sel[0] == 3);      /* the rectangle must not move */
    }

    /* PITCH row: NAV moves the DIGIT cursor, NEVER the rectangle;
     * RIGHT walks toward the 0.01s digit (index 6) and a step there
     * moves +0.01 Hz */
    {
        ui_t u = seq_ui(5, 4, 3);      /* default: the 1s digit */
        u.draft.seq[0].steps[3].pitch_hz = 220.0f;
        ui_handle_action(&u, ACT_NAV_RIGHT);
        CHECK(u.freq_digit == 5);      /* 4 -> 5: the 0.1s digit */
        CHECK(u.seq_sel[0] == 3);      /* the rectangle must not move */
        ui_handle_action(&u, ACT_NAV_RIGHT);
        CHECK(u.freq_digit == 6);      /* 5 -> 6: the 0.01s digit */
        ui_handle_action(&u, ACT_VAL_INC);
        CHECK(fexactf(u.draft.seq[0].steps[3].pitch_hz, 220.01f));
        CHECK(fexactf(u.draft.seq[0].steps[0].pitch_hz, 0.0f));
        CHECK(fexactf(u.draft.seq[0].steps[4].pitch_hz, 0.0f));
    }

    /* PITCH row: LEFT walks toward the 10000s digit (index 0) and a
     * step there moves +10000.00 Hz (place 10000 Hz = 1000000
     * centi-Hz — the 7 places are 10000..0.01) */
    {
        ui_t u = seq_ui(5, 4, 3);
        u.draft.seq[0].steps[3].pitch_hz = 220.0f;
        ui_handle_action(&u, ACT_NAV_LEFT);
        CHECK(u.freq_digit == 3);      /* 4 -> 3: the 10s digit */
        ui_handle_action(&u, ACT_NAV_LEFT);
        CHECK(u.freq_digit == 2);      /* 3 -> 2: the 100s digit */
        ui_handle_action(&u, ACT_NAV_LEFT);
        CHECK(u.freq_digit == 1);      /* 2 -> 1: the 1000s digit */
        ui_handle_action(&u, ACT_NAV_LEFT);
        CHECK(u.freq_digit == 0);      /* 1 -> 0: the 10000s digit */
        CHECK(u.seq_sel[0] == 3);      /* the rectangle must not move */
        ui_handle_action(&u, ACT_VAL_INC);
        CHECK(fexactf(u.draft.seq[0].steps[3].pitch_hz, 10220.0f));
    }

    /* STEP row: NAV moves the rectangle (unchanged behavior), 0..7
     * wrap; the digit cursor is untouched */
    {
        ui_t u = seq_ui(4, 2, 0);
        it = content_item(&u, 4);
        CHECK(it.id == IT_SEQ_STEPSEL);
        ui_handle_action(&u, ACT_NAV_LEFT);
        CHECK(u.seq_sel[0] == 7);      /* wraps down */
        ui_handle_action(&u, ACT_NAV_RIGHT);   /* fresh 0 below */
        /* (the LEFT above left 7; reset to pin RIGHT from 0) */
        u.seq_sel[0] = 0;
        ui_handle_action(&u, ACT_NAV_RIGHT);
        CHECK(u.seq_sel[0] == 1);
        CHECK(u.freq_digit == 2);      /* no digit-cursor movement */
    }

    /* VEL row: NAV is INERT — neither the rectangle nor the digit
     * cursor moves */
    {
        ui_t u = seq_ui(6, 1, 3);
        it = content_item(&u, 6);
        CHECK(it.id == IT_STEP_VEL);
        ui_handle_action(&u, ACT_NAV_LEFT);
        CHECK(u.seq_sel[0] == 3);
        CHECK(u.freq_digit == 1);
        ui_handle_action(&u, ACT_NAV_RIGHT);
        CHECK(u.seq_sel[0] == 3);
        CHECK(u.freq_digit == 1);
    }

    /* PITCH digit edit (X/Z): ACT_VAL_INC with the cursor on the 1s
     * digit (index 4) steps the BOUND step's pitch by exactly one Hz —
     * and only that step */
    {
        ui_t u = seq_ui(5, 4, 3);      /* freq_digit 4 = the 1s digit */
        u.draft.seq[0].steps[3].pitch_hz = 220.0f;
        ui_handle_action(&u, ACT_VAL_INC);
        CHECK(fexactf(u.draft.seq[0].steps[3].pitch_hz, 221.0f));
        CHECK(fexactf(u.draft.seq[0].steps[0].pitch_hz, 0.0f));
        CHECK(fexactf(u.draft.seq[0].steps[4].pitch_hz, 0.0f));
    }
}

/* the nav dispatch is TABLE-DRIVEN —
 * the item spec itself carries the per-row nav mode; NO id matching in
 * ui.c. Compile-red until the nav_mode enum (NAV_STEPSEL / NAV_DIGITS /
 * NAV_NONE) and the spec field exist — expected at the pin stage. */
static void test_seq_nav_spec_modes(void)
{
    CHECK(item_spec(IT_SEQ_STEPSEL)->nav == NAV_STEPSEL);
    CHECK(item_spec(IT_STEP_PITCH)->nav == NAV_DIGITS);
    CHECK(item_spec(IT_STEP_VEL)->nav == NAV_NONE);
}

/* ---- the digit marker's FOCUS GATE —
 * the marker condition in draw_sections (src/ui/ui_draw.c:309,
 * "sel && !dim && item_spec(it.id)->nav == NAV_DIGITS" feeding
 * draw_digit_marker at :269) has NO content-focus term: with the cursor
 * parked on a digit-box row and FOCUS on the sub-tab row
 * (ui.focus[tab] == 0 after moving UP out of the content), sel is still
 * true and the underscore draws — it must not ("the
 * underscore should only appear in the digits when that line is
 * selected"). The gate is pinned as an OBSERVABLE through the
 * predicate the marker reads, exposed for the fix (extern mirror, same
 * established practice as item_value/edit_item/content_item above):
 *   int ui_digit_marker_active(const ui_t *ui, item_t it);
 * true iff `it` IS the cursor row && CONTENT FOCUS && !dim && spec nav
 * == NAV_DIGITS. MOD/MASTER pages have no sub-tab row (ui.h focus[] is
 * per OSC tab; navigation keeps them on content via is_osc_tab), so
 * there the predicate is focus-INDEPENDENT. Red: ui_draw.c's marker
 * condition lacks the focus term and exposes no predicate yet — the
 * extern does not link (compile red); the runtime UP case keeps the
 * gate honest once the extern lands. ---- */
extern int ui_digit_marker_active(const ui_t *ui, item_t it);

static void test_digit_marker_focus_gate(void)
{
    item_t it;

    /* (a) OSC FREQ, cursor on it, focus on the CONTENT -> 1 */
    {
        ui_t u;
        memset(&u, 0, sizeof u);
        u.tab = TAB_OSC1;
        u.sub[0] = SUB_OSC;                /* chain A wave 0 = SINE: not
                                            * noise, so FREQ is undimmed */
        u.focus[0] = 1;                    /* content, not the sub-tab row */
        it = content_item(&u, 0);          /* MAIN_ITEMS[0] = FREQ */
        CHECK(it.id == IT_OSC_FREQ);
        CHECK(ui_digit_marker_active(&u, it) == 1);
    }

    /* (b) the BUG: same row, same cursor, but focus on the SUB-TAB row
     * (focus[tab] == 0 after ACT_SEL_UP out of the content) -> 0 — the
     * marker condition's sel is still true today, so the underscore
     * draws with no content focus */
    {
        ui_t u;
        memset(&u, 0, sizeof u);
        u.tab = TAB_OSC1;
        u.sub[0] = SUB_OSC;
        u.focus[0] = 0;                    /* parked on the sub-tab row */
        it = content_item(&u, 0);
        CHECK(it.id == IT_OSC_FREQ);
        CHECK(ui_digit_marker_active(&u, it) == 0);
    }

    /* (c) MOD/MASTER page: NO sub-tab row (focus[] is per OSC tab) ->
     * 1 REGARDLESS of focus. DELAY TONE is the MASTER page's digit box
     * (MASTER_ITEMS[8], ITF_HZ/TONE, NAV_DIGITS per seq_items.c:271). */
    {
        ui_t u;
        memset(&u, 0, sizeof u);
        u.tab = TAB_MASTER;
        /* MASTER owns cursor slot tab*SUB_COUNT + 0 = 12 (ui.h layout);
         * park the cursor ON the TONE row (cursor row == it) */
        u.cursor[(int)TAB_MASTER * SUB_COUNT] = 8;
        it = content_item(&u, 8);
        CHECK(it.id == IT_DLY_TONE);
        u.focus[0] = 1;                    /* OSC-tab focus state: inert */
        CHECK(ui_digit_marker_active(&u, it) == 1);
        u.focus[0] = 0;
        CHECK(ui_digit_marker_active(&u, it) == 1);
    }

    /* runtime 1 -> 0 through the real state machine (the ask's
     * "ACT_NAV_UP" is ACT_SEL_UP in this codebase — keymap.h:19 has no
     * nav *_UP; UP selection is ACT_SEL_UP, which parks on the sub-tab
     * row from the top of content, src/ui/ui.c:482-483): */
    {
        fresh_ui();
        ui_handle_action(&ui, ACT_SEL_DOWN);  /* into content: cursor 0 */
        it = content_item(&ui, 0);            /* = OSC FREQ */
        CHECK(it.id == IT_OSC_FREQ);
        CHECK(ui_digit_marker_active(&ui, it) == 1);
        ui_handle_action(&ui, ACT_SEL_UP);    /* UP to the sub-tab row */
        CHECK(ui.focus[ui.tab] == 0);         /* focus left the content */
        CHECK(ui_digit_marker_active(&ui, it) == 0);   /* gate closed */
    }
}

/* ---- the MOD page's SLOT row
 * selects the slot the five rows below edit — NAV on the SLOT row rolls
 * the selection 0..15 WITH roll-over, and DEPTH then edits
 * the SELECTED slot's amount (the slot rides the ui state at use
 * time, exactly like SEQ PITCH/VEL). Driven through the real key path
 * (ui_handle_action); the selection is ui_t-internal, so it is
 * observed ONLY through the DEPTH writes. A slot whose dest is OFF dims
 * its DEPTH row (ui.c item_dim), so the edited slots are made live. ---- */
static void test_mod_slot_row(void)
{
    /* SLOT 7 (index 6): six NAV_RIGHTs from fresh SLOT 1, then DEPTH
     * writes xmod[6].amount — and only that slot's amount */
    {
        ui_t u;
        memset(&u, 0, sizeof u);
        u.mb = &mb;
        u.tab = TAB_MOD;
        u.cursor[(int)TAB_MOD * SUB_COUNT] = 0;   /* the SLOT row */
        u.draft.xmod[6].dest = MOD_PITCH;         /* DEPTH live (not dim) */
        for (int i = 0; i < 6; i++)
            ui_handle_action(&u, ACT_NAV_RIGHT);  /* SLOT 1 -> SLOT 7 */
        ui_handle_action(&u, ACT_SEL_DOWN);       /* cursor onto DEPTH */
        ui_handle_action(&u, ACT_SEL_DOWN);
        ui_handle_action(&u, ACT_SEL_DOWN);
        ui_handle_action(&u, ACT_SEL_DOWN);
        item_t it = content_item(&u, 4);          /* DEPTH (flat 4) */
        CHECK(it.id == IT_XMOD_AMT);              /* red today: LEVEL */
        ui_handle_action(&u, ACT_VAL_INC);        /* edit DEPTH */
        CHECK(fexactf(u.draft.xmod[6].amount, 1.0f));  /* red today */
        CHECK(u.draft.xmod[0].mix == 0);               /* red today: the
                                    * write lands on slot 0's LEVEL */
        CHECK(fexactf(u.draft.xmod[0].amount, 0.0f));  /* slot 0 amount
                                                          * untouched */
    }

    /* roll-over DOWN: NAV_LEFT from SLOT 1 wraps to SLOT 16 (index 15);
     * DEPTH then writes xmod[15].amount */
    {
        ui_t u;
        memset(&u, 0, sizeof u);
        u.mb = &mb;
        u.tab = TAB_MOD;
        u.cursor[(int)TAB_MOD * SUB_COUNT] = 0;
        u.draft.xmod[15].dest = MOD_PITCH;
        ui_handle_action(&u, ACT_NAV_LEFT);       /* 0 -> 15 roll-over */
        ui_handle_action(&u, ACT_SEL_DOWN);
        ui_handle_action(&u, ACT_SEL_DOWN);
        ui_handle_action(&u, ACT_SEL_DOWN);
        ui_handle_action(&u, ACT_SEL_DOWN);
        item_t it = content_item(&u, 4);
        CHECK(it.id == IT_XMOD_AMT);
        ui_handle_action(&u, ACT_VAL_INC);
        CHECK(fexactf(u.draft.xmod[15].amount, 1.0f));  /* red today */
        CHECK(u.draft.xmod[0].mix == 0);                /* red today */
        CHECK(fexactf(u.draft.xmod[0].amount, 0.0f));   /* slot 0 untouched */
    }

    /* roll-over UP: fifteen NAV_RIGHTs from SLOT 1 reach SLOT 16 (15),
     * one more wraps 15 -> 0, and DEPTH writes xmod[0].amount */
    {
        ui_t u;
        memset(&u, 0, sizeof u);
        u.mb = &mb;
        u.tab = TAB_MOD;
        u.cursor[(int)TAB_MOD * SUB_COUNT] = 0;
        u.draft.xmod[15].dest = MOD_PITCH;
        u.draft.xmod[0].dest = MOD_PITCH;
        for (int i = 0; i < 15; i++)
            ui_handle_action(&u, ACT_NAV_RIGHT);  /* 0 -> 15 */
        ui_handle_action(&u, ACT_SEL_DOWN);
        ui_handle_action(&u, ACT_SEL_DOWN);
        ui_handle_action(&u, ACT_SEL_DOWN);
        ui_handle_action(&u, ACT_SEL_DOWN);
        item_t it = content_item(&u, 4);
        CHECK(it.id == IT_XMOD_AMT);
        ui_handle_action(&u, ACT_VAL_INC);
        CHECK(fexactf(u.draft.xmod[15].amount, 1.0f));  /* red today */
        CHECK(u.draft.xmod[0].mix == 0);                /* red today */
        ui_handle_action(&u, ACT_NAV_RIGHT);      /* 15 -> 0 roll-over */
        ui_handle_action(&u, ACT_VAL_INC);
        CHECK(fexactf(u.draft.xmod[0].amount, 1.0f));   /* red today */
        CHECK(fexactf(u.draft.xmod[15].amount, 1.0f));  /* the second
                                    * write must not touch slot 15 */
    }
}

int main(void)
{
    /* Headless SDL: dummy video driver so SDL_StartTextInput works. */
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    mailbox_init(&mb);

    /* Preset staging: exactly the dir preset_dir() resolves (absolute,
     * beside the test binary) — the test no longer depends on the CWD.
     * preset_save creates the dir if missing. */
    if (preset_dir(stage, sizeof stage) != 0) {
        printf("preset_dir failed\n");
        return 1;
    }
    {
        char err[128];
        synth_params_t d = OPHIS_DEFAULTS;
        if (preset_save(stage, "menutest", &d, err, sizeof err) != 0) {
            printf("preset_save failed: %s\n", err);
            return 1;
        }
    }
    test_main_nav_val_inert();
    test_confirm_nav_val_inert();
    test_name_grid_picker_kept();
    test_text_entry_state_tracker();
    test_load_nav_val_inert();
    test_valkey_no_retrig();
    test_save_prefills_last_name();
    /* ---- the MOD SLOT row (before SDL_Quit: the val path reads
     * SDL_GetTicks) ---- */
    test_mod_slot_row();
    test_load_empty_shows_up();
    test_load_missing_dir_created();
    test_load_uncreatable_falls_back();
    purge_dir(stage);
    SDL_Quit();
    /* ---- param_get/param_set round-
     * trip every kind on distinct slots — the ONE accessor pair the
     * item_get/item_set ladders collapsed into ---- */
    {
        synth_params_t d;
        memset(&d, 0, sizeof d);
        d = OPHIS_DEFAULTS;
        struct { param_kind_t k; int ci, si; double v; } cases[] = {
            { PK_CHAIN_FREQ, 0, 0, 123.5 }, { PK_CHAIN_WAVE, 1, 0, 3 },
            { PK_CHAIN_DUTY, 2, 0, 77 },    { PK_CHAIN_LEVEL, 0, 0, 55 },
            { PK_CHAIN_VEL, 1, 0, 66 },     { PK_ADSR_A, 0, 0, 0.25 },
            { PK_ADSR_D, 0, 0, 0.5 },       { PK_ADSR_S, 0, 0, 80 },
            { PK_ADSR_R, 0, 0, 1.5 },       { PK_CHAIN_FMODE, 0, 0, 2 },
            { PK_CHAIN_FC, 0, 0, 1500.0 },  { PK_CHAIN_RES, 0, 0, 42 },
            { PK_CHAIN_ENV, 0, 0, -60 },    { PK_CHAIN_POLES, 0, 0, 24 },
            { PK_CHAIN_MIXVOL, 0, 2, 33 },  /* si = chain 2 */
            { PK_SUB_DIV, 0, 1, 7 },        { PK_SUB_WAVE, 0, 1, 2 },
            { PK_SUB_DUTY, 0, 1, 60 },      { PK_SUB_LEVEL, 0, 1, 45 },
            { PK_XMOD_SRC, 0, 2, 9 },       { PK_XMOD_DEST, 0, 2, 5 },
            { PK_XMOD_AMT, 0, 2, -75 },     { PK_XMOD_MIX, 0, 2, 88 },
            { PK_XMOD_TGT, 0, 2, 1 },       { PK_SEQ_ON, 0, 0, 1 },
            { PK_SEQ_DIV, 0, 0, 9 },        { PK_SEQ_MODE, 0, 0, 4 },
            { PK_SEQ_ONCE, 0, 0, 1 },       { PK_STEP_PITCH, 0, 5, 440.0 },
            { PK_STEP_VEL, 0, 5, 90 },      { PK_TOP_BPM, 0, 0, 300 },
            { PK_TOP_MASTER, 0, 0, 95 },    { PK_TOP_SAFETY, 0, 0, 0 },
            { PK_DLY_TIMEL, 0, 0, 0.75 },   { PK_DLY_FB, 0, 0, 61 },
            { PK_DLY_TONE, 0, 0, 8500 },    { PK_DLY_MIX, 0, 0, 27 },
            { PK_RVB_SIZE, 0, 0, 99 },      { PK_RVB_DAMP, 0, 0, 12 },
            { PK_RVB_MIX, 0, 0, 40 },       { PK_RVB_WIDTH, 0, 0, 66 },
        };
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            param_set(&d, cases[i].k, cases[i].ci, cases[i].si,
                      cases[i].v);
            CHECK(fexact(param_get(&d, cases[i].k, cases[i].ci,
                                   cases[i].si), cases[i].v));
        }
    }

    /* ---- the display transform lives in
     * the UI — schema names are RAW; ui_enum_display reproduces the old
     * UPPERCASE/'.' forms exactly ---- */
    {
        char b[32];
        (void)ui_enum_display("b_sub1", b, sizeof b);
        CHECK(strcmp(b, "B.SUB1") == 0);
        (void)ui_enum_display("a_osc", b, sizeof b);
        CHECK(strcmp(b, "A.OSC") == 0);
        (void)ui_enum_display("saw", b, sizeof b);
        CHECK(strcmp(b, "SAW") == 0);
        (void)ui_enum_display("pingpong", b, sizeof b);
        CHECK(strcmp(b, "PINGPONG") == 0);
        (void)ui_enum_display("12db", b, sizeof b);
        CHECK(strcmp(b, "12DB") == 0);
        /* schema layer is RAW now (strict separation) */
        CHECK(strcmp(schema_wave_name(WAVE_SAW), "saw") == 0);
        CHECK(strcmp(schema_xsrc_name(7), "a_osc") == 0);
        CHECK(strcmp(schema_dest_name(MOD_RING), "ring") == 0);
    }

    /* ---- the enum value rows render the draft ---- */
    test_item_value_enum_rows();

    /* ---- the freq odometer box, the fractional
     * preservation rule and the universal roll-over ---- */
    test_item_value_freq_odometer();
    test_freq_fractional_rounding();
    test_edit_rollover();

    /* ---- the SEQ page extension — row-
     * dependent NAV + the table-driven nav modes ---- */
    test_seq_nav_rows();
    test_seq_nav_spec_modes();

    /* ---- the digit marker's focus gate ---- */
    test_digit_marker_focus_gate();

    /* ---- the input-policy gate ---- */
    {
        ui_t ui2;
        memset(&ui2, 0, sizeof ui2);
        SDL_Event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = SDLK_a;
        CHECK(ui_event_filter(&ui2, &ev) == 1);      /* no text entry */
        ui2.in_menu = 3;                             /* MENU_NAME */
        CHECK(ui_event_filter(&ui2, &ev) == 0);      /* swallowed */
        ev.key.keysym.sym = SDLK_ESCAPE;
        CHECK(ui_event_filter(&ui2, &ev) == 1);      /* ESC passes */
        ev.key.keysym.sym = SDLK_RETURN;
        CHECK(ui_event_filter(&ui2, &ev) == 1);      /* ENTER passes */
        ev.key.keysym.sym = SDLK_a;
        ev.type = SDL_KEYUP;
        CHECK(ui_event_filter(&ui2, &ev) == 1);      /* KEYUP passes */
    }

    HARNESS_REPORT();
}
