/* tests/test_keymap_events.c — keymap_action() event-level checks
 * (remediation F1 auto-repeat gate, F6 controller-button collision; the
 * F5 keymap_label caller-buffer contract lives in test_keymap.c).
 * SDL-linked: constructs SDL_Event structs directly. One source compiled
 * twice, mirroring test_keymap / test_keymap_r36s:
 *   -DTARGET_DESKTOP -> build/tests/test_keymap_events
 *   -DTARGET_R36S    -> build/tests/test_keymap_events_r36s */
#include "harness.h"

#include <string.h>

#include <SDL.h>

#include "../src/config.h"
#include "../src/ui/keymap.h"

static SDL_Event key_down(SDL_Keycode sym, Uint8 repeat)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = sym;
    e.key.repeat = repeat;
    return e;
}

static SDL_Event cbutton(Uint8 button)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.button = button;
    return e;
}

int main(void)
{
    SDL_Event e;
    ui_action_t a;

#if defined(TARGET_DESKTOP)

    /* Plain keydown maps (repeat=0). */
    e = key_down(SDLK_SPACE, 0);
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_RUN);
    /* Owner-ordered desktop bindings (2026-11-05): T = TRIGGER,
     * Enter = CONFIRM (menu select), Space = RUN (transport only). */
    e = key_down(SDLK_t, 0);
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_TRIGGER);
    e = key_down(SDLK_RETURN, 0);
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_CONFIRM);
    e = key_down(SDLK_z, 0);
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_VAL_DEC);
    e = key_down(SDLK_x, 0);
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_VAL_INC);

    /* F1: repeat=1 maps ONLY the VAL_INC/VAL_DEC binding; RUN/TRIGGER/
     * CONFIRM/MENU/TAB/NAV/SEL return 0 (no action). */
    e = key_down(SDLK_SPACE, 1);   /* RUN     */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_t, 1);       /* TRIGGER */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_RETURN, 1);  /* CONFIRM */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_ESCAPE, 1);  /* MENU    */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_q, 1);       /* TAB_BACK  */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_e, 1);       /* TAB_NEXT  */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_UP, 1);      /* SEL_UP    */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_DOWN, 1);    /* SEL_DOWN  */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_LEFT, 1);    /* NAV_LEFT  */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_RIGHT, 1);   /* NAV_RIGHT */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_z, 1);       /* VAL_DEC repeats */
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_VAL_DEC);
    e = key_down(SDLK_x, 1);       /* VAL_INC repeats */
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_VAL_INC);

    /* Unmapped key and KEYUP map nothing. */
    e = key_down(SDLK_a, 0);
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_SPACE, 0);
    e.type = SDL_KEYUP;
    CHECK(keymap_action(&e, &a) == 0);

    /* value-key RELEASE ends the hold (owner order 2026-11-05): KEYUP
     * of the VAL_INC/VAL_DEC bindings -> +1/-1; other keys -> 0;
     * controller events map nothing on desktop (F6) */
    e = key_down(SDLK_x, 0);
    e.type = SDL_KEYUP;
    CHECK(keymap_val_release(&e) == 1);
    e = key_down(SDLK_z, 0);
    e.type = SDL_KEYUP;
    CHECK(keymap_val_release(&e) == -1);
    e = cbutton(SDL_CONTROLLER_BUTTON_X);
    e.type = SDL_CONTROLLERBUTTONUP;
    CHECK(keymap_val_release(&e) == 0);

    /* F6: desktop KB_* are ophis key ids 0..11, numerically colliding
     * with the SDL controller button constants — a controller event
     * maps NOTHING on desktop semantics. */
    e = cbutton(SDL_CONTROLLER_BUTTON_A);         /* 0 == OPHIS_KEY_Q     */
    CHECK(keymap_action(&e, &a) == 0);
    e = cbutton(SDL_CONTROLLER_BUTTON_B);         /* 1 == OPHIS_KEY_E     */
    CHECK(keymap_action(&e, &a) == 0);
    e = cbutton(SDL_CONTROLLER_BUTTON_X);         /* 2 == OPHIS_KEY_SPACE */
    CHECK(keymap_action(&e, &a) == 0);
    e = cbutton(SDL_CONTROLLER_BUTTON_Y);
    CHECK(keymap_action(&e, &a) == 0);
    e = cbutton(SDL_CONTROLLER_BUTTON_START);     /* 6 == OPHIS_KEY_UP    */
    CHECK(keymap_action(&e, &a) == 0);
    e = cbutton(SDL_CONTROLLER_BUTTON_DPAD_UP);   /* 11: past the ids     */
    CHECK(keymap_action(&e, &a) == 0);
    e = cbutton(200);
    CHECK(keymap_action(&e, &a) == 0);

#elif defined(TARGET_R36S)

    /* KB_* are button constants: controller events map as configured
     * (behavior unchanged by F6). */
    e = cbutton(0);  CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_RUN);
    e = cbutton(1);  CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_TRIGGER);
    e = cbutton(2);  CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_VAL_INC);
    e = cbutton(3);  CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_VAL_DEC);
    e = cbutton(9);  CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_TAB_BACK);
    e = cbutton(10); CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_TAB_NEXT);
    e = cbutton(6);  CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_MENU);
    e = cbutton(11); CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_SEL_UP);
    e = cbutton(12); CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_SEL_DOWN);
    e = cbutton(13); CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_NAV_LEFT);
    e = cbutton(14); CHECK(keymap_action(&e, &a) == 1); CHECK(a == ACT_NAV_RIGHT);
    e = cbutton(4);  /* Select = second MENU (owner demand 2026-11-05:
                      * exit path without Start) */
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_MENU);
    /* value-key RELEASE on the handheld: X(2) -> +1, Y(3) -> -1 */
    e = cbutton(2);
    e.type = SDL_CONTROLLERBUTTONUP;
    CHECK(keymap_val_release(&e) == 1);
    e = cbutton(3);
    e.type = SDL_CONTROLLERBUTTONUP;
    CHECK(keymap_val_release(&e) == -1);
    e = cbutton(0);
    e.type = SDL_CONTROLLERBUTTONUP;
    CHECK(keymap_val_release(&e) == 0);
    e = cbutton(200);
    CHECK(keymap_action(&e, &a) == 0);

    /* F1 on the handheld build too (pre-existing collision semantics: a
     * keydown resolves to OUR id and is matched against the KB_* button
     * values). repeat=1 keydown mapping to a non-VAL action -> 0; mapping
     * to VAL_INC -> 1. */
    e = key_down(SDLK_q, 1);      /* ophis id 0 == KB_RUN */
    CHECK(keymap_action(&e, &a) == 0);
    e = key_down(SDLK_SPACE, 1);  /* ophis id 2 == KB_VAL_INC */
    CHECK(keymap_action(&e, &a) == 1);
    CHECK(a == ACT_VAL_INC);

#else
    #error "compile with -DTARGET_DESKTOP or -DTARGET_R36S"
#endif
    HARNESS_REPORT();
}
