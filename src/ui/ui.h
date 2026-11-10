/* src/ui/ui.h — SDL2 UI.
 * 5 top tabs (OSC 1|2|3, MOD, MASTER) with OSC sub-tabs
 * (OSC|ENV+FLT|SEQ), focus model per the binding text, SEQ step
 * page, menu window overlay
 * (NEW/CONFIRM/SAVE/LOAD folder navigator/PANIC/EXIT/BACK). All
 * UI->engine traffic goes through the lock-free mailbox channels
 * (audio/mailbox.h). */
#ifndef OPHIS_UI_H
#define OPHIS_UI_H

#include <SDL.h>

#include "../audio/mailbox.h"
#include "keymap.h"

/* Top tabs, order per the tab
 * reorg: MIX renamed MOD, FX tab removed — its delay/reverb params are
 * MASTER sections; Q/E desktop, L/R shoulder R36S cycle, wrap). MOD and
 * MASTER have no sub-tab row (focus starts on content). */
typedef enum {
  TAB_OSC1, TAB_OSC2, TAB_OSC3, TAB_MOD, TAB_MASTER, TAB_COUNT
} top_tab_t;

/* Sub-tabs of each OSC tab (LEFT/RIGHT on the sub-tab row cycles).
 * Reorg: the SUB sub-tab merged into OSC (its items are
 * the OSC page's SUB 1/SUB 2 sections), FILT renamed ENV+FLT (it gained
 * the ADSR section that moved off the OSC page). */
typedef enum { SUB_OSC, SUB_ENVFLT, SUB_SEQ, SUB_COUNT } sub_tab_t;

typedef struct {
  /* navigation — persisted per tab + sub-tab */
  top_tab_t       tab;
  sub_tab_t       sub[3];              /* current sub-tab per OSC tab
                                        * (still 3 entries: the 3 OSC
                                        * tabs; MOD/MASTER have none) */
  int             focus[3];            /* per OSC tab: 0 = sub-tab row,
                                        * 1 = content */
  /* content cursor, per tab+sub-tab: the page's FLAT item index 0..N-1
   * (labels and gaps are visual only, see ui.c). Layout —
   * kept at 15 slots deliberately,
   * low-value churn): index = tab*SUB_COUNT + (is_osc_tab ? sub : 0),
   * so only 11 of the TAB_COUNT*SUB_COUNT = 15 slots are reachable —
   * OSC tabs 0-2 own indices 0..8 (one per sub-tab), MOD owns 9 and
   * MASTER owns 12 (non-OSC tabs always take sub 0); indices 10, 11,
   * 13, 14 are never indexed. */
  int             cursor[TAB_COUNT * SUB_COUNT];
  int             seq_sel[3];          /* SEQ page: selected rectangle per
                                       * chain (PITCH/VEL bind to it) */
  /* the MOD page's selected matrix slot
   * 0..15 ("SLOT 1".."SLOT 16") — the SLOT row drives it (NAV_STEPSEL
   * roll-over) and the five rows below resolve their si to it at use
   * time. UI state only: never committed, not persisted. */
  int             mod_slot;
  /* digit cursor of the 7-digit odometer boxes: index 0..6 into the
   * value's digits counted from the LEFT —
   * 0 = the 10000s digit, 6 = the 0.01s decimal digit — used to index
   * digit_place's PLACES[] table. Reset to the 1s digit (4) whenever
   * the cursor enters a digit-box row (OSC FREQ / FILTER CUT / DELAY
   * TONE); RIGHT walks toward the 1s digit, LEFT toward the 10000s. */
  int             freq_digit;
  /* menu window (overlay on any screen) */
  int             in_menu;             /* 0 closed, else sub-state id */
  int             menu_cursor;
  int             confirm_cursor;      /* YES/NO */
  /* LOAD folder navigator state */
  char            load_dir[1024];      /* CURRENT FOLDER PATH (title line) */
  struct { char name[33]; int is_dir; } load_entries[64];
  int             load_cursor;         /* index into load_entries */
  int             load_top;            /* first visible row */
  int             load_count;
  char            load_err[64];
  char            save_err[64];
  char            name[33];            /* SAVE name entry buffer */
  int             name_len;
  char            last_saved_name[33]; /* last SUCCESSFUL save: prefills the
                                        * next
                                        * SAVE; empty = none this session */
  /* preset I/O dir (resolved once; navigator starts here) */
  char            preset_dir[512];
  /* SAVE name-entry grid picker (R36S path; also usable with a pad) */
  int             grid_x, grid_y;
  /* draft params (the UI's local copy; committed to the mailbox) */
  synth_params_t  draft;
  mailbox_t      *mb;
  /* flash timers (ms, SDL_GetTicks based) */
  Uint32          trig_flash_until;
  Uint32          panic_flash_until;
  Uint32          quit_at;             /* EXIT: QUITTING flash until */
  int             quitting;
  /* value-key hold (holding X/Y retrigs): the
   * pad never auto-repeats controller buttons, so ui_tick synthesizes
   * the repeat — value change + retrig until release. 0 = not held. */
  int             val_held_dir;
  Uint32          val_next_repeat;
} ui_t;

void ui_init(ui_t *ui, mailbox_t *mb);

/* Feed one keymap action. */
void ui_handle_action(ui_t *ui, ui_action_t act);

/* 1 while the SAVE name-entry dialog owns the keyboard (our state, not
 * SDL's: SDL 2.32 enables text input by default — libsdl-org/SDL#9309 —
 * so SDL_IsTextInputActive() is SDL_TRUE from startup and cannot gate
 * keymap dispatch). main.c checks this before mapping keydowns. */
int  ui_text_entry_active(const ui_t *ui);

/* Feed a raw SDL event NOT mapped by keymap_action: SDL_QUIT,
 * SDL_TEXTINPUT (SAVE name entry on desktop). */
void ui_handle_event(ui_t *ui, SDL_Event *e);

/* Draw the current tab (640x480 logical) + overlay. */
void ui_draw(ui_t *ui, SDL_Renderer *ren, const ui_status_t *st);

/* Periodic tick (call each loop pass with the current SDL_GetTicks());
 * retires flashes and runs the value-key hold repeat (value change +
 * retrig while a value key is held). */
void ui_tick(ui_t *ui, Uint32 now);

/* input-policy gate — 1 = the event reaches the keymap, 0 = the UI
 * swallows it (SAVE name entry owns the keyboard) */
int ui_event_filter(const ui_t *ui, const SDL_Event *e);

/* 1 when the UI loop should end (EXIT flash done or window closed). */
int  ui_wants_quit(const ui_t *ui);

#endif /* OPHIS_UI_H */
