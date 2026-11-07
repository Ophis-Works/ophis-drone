/* src/ui/keymap.h — OUR key ids + SDL event -> ui_action_t dispatch
 * (IMPLEMENTATION.md §6, PLAN §8.1/§9). Binding values come ONLY from
 * config.h KB_* — no #ifdef outside config.h. */
#ifndef OPHIS_KEYMAP_H
#define OPHIS_KEYMAP_H

/* OURS — ophis key ids, not SDL keycodes. These are our own identifiers;
 * do not confuse them with SDL's SDLK_* (config.h desktop KB_* name these;
 * they are resolved to SDL keycodes only inside ui/keymap.c, which keeps
 * the single SDL mapping table). */
typedef enum {
  OPHIS_KEY_Q, OPHIS_KEY_E, OPHIS_KEY_SPACE, OPHIS_KEY_RETURN,
  OPHIS_KEY_UP, OPHIS_KEY_DOWN, OPHIS_KEY_LEFT, OPHIS_KEY_RIGHT,
  OPHIS_KEY_Z, OPHIS_KEY_X, OPHIS_KEY_T,
  OPHIS_KEY_ESCAPE
} ophis_key_t;

typedef enum {
  ACT_TRIGGER, ACT_RUN, ACT_SEL_UP, ACT_SEL_DOWN,
  ACT_NAV_LEFT, ACT_NAV_RIGHT, ACT_VAL_DEC, ACT_VAL_INC,
  ACT_TAB_NEXT, ACT_TAB_BACK, ACT_MENU,
  ACT_CONFIRM                       /* menu select only (desktop Enter) */
} ui_action_t;

/* SDL-dependent declarations; OPHIS_KEYMAP_NO_SDL lets SDL-free TUs
 * (tests) include this header for the ids above. */
#ifndef OPHIS_KEYMAP_NO_SDL
#include <SDL.h>
/* Returns 1 and fills *out if the event maps to an action, else 0.
 * Handles SDL_KEYDOWN (desktop KB_* are OUR key ids, resolved to SDL
 * keycodes here) and SDL_CONTROLLERBUTTONDOWN (handheld KB_* are button
 * numbers, PLAN §9). A keydown with e.key.repeat set maps ONLY to
 * ACT_VAL_INC / ACT_VAL_DEC (holding a value key auto-repeats; holding
 * RUN/MENU/tabs/selection must not re-fire) — every other repeated
 * action returns 0. A SDL_CONTROLLERBUTTONDOWN maps nothing when the
 * target's KB_* are ophis key ids (desktop: they collide numerically
 * with the SDL button constants); on button-constant targets (handheld)
 * it maps as configured. */
int keymap_action(SDL_Event *e, ui_action_t *out);
/* +1/-1 when the event RELEASES a value key (SDL_KEYUP of the KB_VAL_
 * INC/KB_VAL_DEC binding on desktop, CONTROLLERBUTTONUP of the same on
 * handheld), else 0 — ends the value-key hold (owner order 2026-11-05:
 * holding the value keys retrigs; without this the desktop KEYUP never
 * cleared the hold and the repeat ran away). */
int keymap_val_release(const SDL_Event *e);
/* THE one label place for KB_* values (hint bar, PLAN_KEYMAP_REBIND §2):
 * an ophis key id -> SDL_GetKeyName of its keycode; a numeric button
 * constant matching an SDL_CONTROLLER_BUTTON_* value ->
 * SDL_GameControllerGetStringForButton, uppercased. Caller-buffer form:
 * writes the label (NUL-terminated) into buf and returns buf; returns
 * NULL if it does not fit in n bytes or kb matches neither form. No
 * shared static — callers own their buffers. */
const char *keymap_label(int kb, char *buf, size_t n);
#endif /* OPHIS_KEYMAP_NO_SDL */

#endif /* OPHIS_KEYMAP_H */
