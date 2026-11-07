/* src/ui/keymap.c — dispatch over config.h KB_* values only (PLAN §9:
 * config.h is the only target difference; this file is byte-identical
 * between builds). Desktop KB_* are OUR key ids (ophis_key_t); the ONE
 * place they are resolved to SDL keycodes is the table below. Handheld
 * KB_* are SDL controller button numbers; both event types are checked
 * against the same constants so no #ifdef is needed here. */
#include "keymap.h"

#include <SDL.h>

#include <ctype.h>

#include "../config.h"

/* THE single SDL mapping table: OUR key ids -> SDL keycodes. */
static const SDL_Keycode key_to_sdl[] = {
    [OPHIS_KEY_Q]      = SDLK_q,
    [OPHIS_KEY_E]      = SDLK_e,
    [OPHIS_KEY_SPACE]  = SDLK_SPACE,
    [OPHIS_KEY_RETURN] = SDLK_RETURN,
    [OPHIS_KEY_UP]     = SDLK_UP,
    [OPHIS_KEY_DOWN]   = SDLK_DOWN,
    [OPHIS_KEY_LEFT]   = SDLK_LEFT,
    [OPHIS_KEY_RIGHT]  = SDLK_RIGHT,
    [OPHIS_KEY_Z]      = SDLK_z,
    [OPHIS_KEY_X]      = SDLK_x,
    [OPHIS_KEY_T]      = SDLK_t,
    [OPHIS_KEY_ESCAPE] = SDLK_ESCAPE,
};

/* OUR key id (already resolved) -> action. KB_CONFIRM is a dead case
 * label on the handheld (-1: A keeps the RUN/confirm double role via
 * KB_RUN_CONFIRMS_MENU); on desktop Enter maps to ACT_CONFIRM (menu
 * select only, owner order 2026-11-05). */
static int map_code(int code, ui_action_t *out)
{
    switch (code) {
    case KB_TRIGGER:   *out = ACT_TRIGGER;   return 1;
    case KB_RUN:       *out = ACT_RUN;       return 1;
    case KB_CONFIRM:   *out = ACT_CONFIRM;   return 1;
    case KB_SEL_UP:    *out = ACT_SEL_UP;    return 1;
    case KB_SEL_DOWN:  *out = ACT_SEL_DOWN;  return 1;
    case KB_NAV_LEFT:  *out = ACT_NAV_LEFT;  return 1;
    case KB_NAV_RIGHT: *out = ACT_NAV_RIGHT; return 1;
    case KB_VAL_DEC:   *out = ACT_VAL_DEC;   return 1;
    case KB_VAL_INC:   *out = ACT_VAL_INC;   return 1;
    case KB_TAB_NEXT: *out = ACT_TAB_NEXT; return 1;
    case KB_TAB_BACK: *out = ACT_TAB_BACK; return 1;
    case KB_MENU:      *out = ACT_MENU;      return 1;
    case KB_MENU2:     *out = ACT_MENU;      return 1;
    default:                                 return 0;
    }
}

/* SDL keycode -> OUR key id, or -1 if unmapped. */
static int sdl_to_ophis(SDL_Keycode sym)
{
    for (int k = OPHIS_KEY_Q; k <= OPHIS_KEY_ESCAPE; k++)
        if (key_to_sdl[k] == sym)
            return k;
    return -1;
}

/* Values 0..10 are BOTH valid ophis key ids and valid controller button
 * constants (SDL_CONTROLLER_BUTTON_A == 0 ...). Which reading a KB_* value
 * takes is decided by config.h — the same mechanism map_code() uses with
 * its `case KB_*:` labels, no #ifdef: on desktop KB_SEL_UP is an ophis id
 * (< SDL_CONTROLLER_BUTTON_START), on handheld it is DPAD_UP (> START).
 * Compile-time constant: folds per target, file stays byte-identical. */
#define KB_LABELS_ARE_BUTTONS ((int)(KB_SEL_UP) > (int)SDL_CONTROLLER_BUTTON_START)

/* Short hint-bar names for the controller buttons (owner order
 * 2026-11-07 "please shorten", PLAN_HINT_SHORT §1.2): indexed by
 * SDL_GameControllerButton (A=0, B=1, X=2, Y=3, BACK=4, GUIDE=5, START=6,
 * LEFTSHOULDER=9, RIGHTSHOULDER=10, DPUP=11, DPDOWN=12, DPLEFT=13,
 * DPRIGHT=14); NULL entries fall back to the SDL string uppercased.
 * Entries are already uppercase. */
static const char *const button_short_label[SDL_CONTROLLER_BUTTON_MAX] = {
    [SDL_CONTROLLER_BUTTON_A]              = "A",
    [SDL_CONTROLLER_BUTTON_B]              = "B",
    [SDL_CONTROLLER_BUTTON_X]              = "X",
    [SDL_CONTROLLER_BUTTON_Y]              = "Y",
    [SDL_CONTROLLER_BUTTON_BACK]           = "SELECT",
    [SDL_CONTROLLER_BUTTON_GUIDE]          = "GUIDE",
    [SDL_CONTROLLER_BUTTON_START]          = "START",
    [SDL_CONTROLLER_BUTTON_LEFTSHOULDER]   = "L",
    [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]  = "R",
    [SDL_CONTROLLER_BUTTON_DPAD_UP]        = "UP",
    [SDL_CONTROLLER_BUTTON_DPAD_DOWN]      = "DOWN",
    [SDL_CONTROLLER_BUTTON_DPAD_LEFT]      = "LEFT",
    [SDL_CONTROLLER_BUTTON_DPAD_RIGHT]     = "RIGHT",
};

/* +1/-1 when the event RELEASES a value key, else 0 (keymap.h). */
int keymap_val_release(const SDL_Event *e)
{
    int v = -1;
    if (e->type == SDL_KEYUP)
        v = sdl_to_ophis(e->key.keysym.sym);
    else if (e->type == SDL_CONTROLLERBUTTONUP && KB_LABELS_ARE_BUTTONS)
        v = e->cbutton.button;
    if (v == KB_VAL_INC) return 1;
    if (v == KB_VAL_DEC) return -1;
    return 0;
}

/* Values 0..10 are BOTH valid ophis key ids and valid controller button
 * constants (SDL_CONTROLLER_BUTTON_A == 0 ...). Which reading a KB_* value
 * takes is decided by config.h — the same mechanism map_code() uses with
 * its `case KB_*:` labels, no #ifdef: on desktop KB_SEL_UP is an ophis id
 * (< SDL_CONTROLLER_BUTTON_START), on handheld it is DPAD_UP (> START).
 * Compile-time constant: folds per target, file stays byte-identical. */
/* Copy s into buf (NUL-terminated, uppercased if upper) and return buf;
 * NULL if s is NULL or does not fit in n bytes. Caller-buffer form of
 * keymap_label (remediation F5: the single static buffer aliased every
 * label in one snprintf to one pointer). */
static const char *label_copy(const char *s, char *buf, size_t n, int upper)
{
    size_t i;
    if (!s)
        return NULL;
    for (i = 0; s[i]; i++) {
        if (i + 1 >= n)
            return NULL;
        buf[i] = upper ? (char)toupper((unsigned char)s[i]) : s[i];
    }
    buf[i] = '\0';
    return buf;
}

const char *keymap_label(int kb, char *buf, size_t n)
{
    if (!KB_LABELS_ARE_BUTTONS && kb >= OPHIS_KEY_Q && kb <= OPHIS_KEY_ESCAPE)
        return label_copy(SDL_GetKeyName(key_to_sdl[kb]), buf, n, 0);
    if (kb >= 0 && kb < SDL_CONTROLLER_BUTTON_MAX) {
        if (KB_LABELS_ARE_BUTTONS && button_short_label[kb])
            return label_copy(button_short_label[kb], buf, n, 1);
        return label_copy(SDL_GameControllerGetStringForButton(
                              (SDL_GameControllerButton)kb),
                          buf, n, 1);
    }
    return NULL;
}

int keymap_action(SDL_Event *e, ui_action_t *out)
{
    switch (e->type) {
    case SDL_KEYDOWN: {
        int k = sdl_to_ophis(e->key.keysym.sym);
        ui_action_t a;
        if (k < 0 || !map_code(k, &a))
            return 0;
        /* Remediation F1: auto-repeat only on the value keys — holding
         * RUN/MENU/tabs/selection must not re-fire per repeat. */
        if (e->key.repeat && a != ACT_VAL_INC && a != ACT_VAL_DEC)
            return 0;
        *out = a;
        return 1;
    }
    case SDL_CONTROLLERBUTTONDOWN:
        /* Remediation F6: when KB_* are ophis key ids (desktop) they
         * collide numerically with the SDL button constants — map
         * nothing; on button-constant targets (handheld) unchanged. */
        return KB_LABELS_ARE_BUTTONS
                   ? map_code((int)e->cbutton.button, out) : 0;
    default:
        return 0;
    }
}
