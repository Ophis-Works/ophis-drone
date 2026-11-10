/* tests/test_keymap_r36s.c — compile-level keymap binding check for the
 * handheld target. Compiled with
 * -DTARGET_R36S and no SDL: asserts the R36S KB_* bindings are the SDL
 * *logical* gamepad button constants (SDL translates raw bN buttons via
 * the mapping loaded in main.c):
 *   A=0 RUN, B=1 TRIGGER, X=2 VAL_INC, Y=3 VAL_DEC, L=9 / R=10 tabs,
 *   Start=6 menu, d-pad 11/12 selection, 13/14 navigation.
 * The KB_* values are pinned as literals: this test pins config.h's R36S
 * KB_* numbers to the agreed constants and
 * is SDL-free, so it CANNOT detect a renumbering of SDL's
 * SDL_CONTROLLER_BUTTON_* enum — cross-check the numbers against
 * SDL_gamecontroller.h manually (see the config.h comment above the
 * R36S KB_* block) whenever SDL changes.
 * The keymap_label() pins: the bound handheld
 * keys must label to the SHORT hint-bar forms. keymap_label lives in
 * src/ui/keymap.c behind SDL
 * (SDL_GameControllerGetStringForButton), so this test is SDL-linked
 * now (same layout as test_keymap_events_r36s). */
#include "harness.h"

#include <ctype.h>
#include <string.h>

#include <SDL.h>

#include "../src/config.h"
#include "../src/ui/keymap.h"

int main(void)
{
#if !defined(TARGET_R36S) && !defined(TARGET_RG28XX)
    #error "this test must be compiled with -DTARGET_R36S or -DTARGET_RG28XX"
#endif
    /* Face buttons (SDL_CONTROLLER_BUTTON_A/B/X/Y). */
    CHECK(KB_RUN     == 0); /* A = RUN/STOP   */
    CHECK(KB_TRIGGER == 1); /* B = TRIGGER    */
    CHECK(KB_VAL_INC == 2); /* X = value +1   */
    CHECK(KB_VAL_DEC == 3); /* Y = value -1   */
    /* Shoulders (LEFT/RIGHTSHOULDER) and Start. */
    CHECK(KB_TAB_BACK == 9);  /* L = tab back */
    CHECK(KB_TAB_NEXT == 10); /* R = tab next */
    CHECK(KB_MENU     == 6);  /* Start = menu */
    /* Select (BACK) = second MENU key: with
     * Start dead on the device, MENU -> EXIT must not depend on it. */
    CHECK(KB_MENU2    == 4);
    /* D-pad: selection up/down, navigation left/right. */
    CHECK(KB_SEL_UP    == 11);
    CHECK(KB_SEL_DOWN  == 12);
    CHECK(KB_NAV_LEFT  == 13);
    CHECK(KB_NAV_RIGHT == 14);
    /* CONFIRM is unbound on the handheld (dead case label -1): A keeps
     * its RUN/confirm double role via KB_RUN_CONFIRMS_MENU, B keeps the
     * menu cancel via KB_TRIGGER_CANCELS_MENU (the Enter/Esc-only
     * desktop rules must not touch the R36S map). */
    CHECK(KB_CONFIRM == -1);
    CHECK(KB_RUN_CONFIRMS_MENU == 1);
    CHECK(KB_TRIGGER_CANCELS_MENU == 1);

    /* every bound handheld key labels to the SHORT hint-bar form —
     *   L/R:TABS  UP/DOWN:SEL  Y/X:EDIT  A:RUN  B:TRIG  START:MENU
     * and MENU2 (Select/BACK, bound but unshown) labels "SELECT".
     * keymap_label returns NULL when the label does not fit; every
     * pinned short form must fit a 16-byte caller buffer (the
     * caller-buffer contract, same pattern as tests/test_keymap.c). */
    char lb[16];
    CHECK(keymap_label(KB_TAB_BACK, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "L") == 0);
    CHECK(keymap_label(KB_TAB_NEXT, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "R") == 0);
    CHECK(keymap_label(KB_SEL_UP, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "UP") == 0);
    CHECK(keymap_label(KB_SEL_DOWN, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "DOWN") == 0);
    CHECK(keymap_label(KB_VAL_DEC, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "Y") == 0);
    CHECK(keymap_label(KB_VAL_INC, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "X") == 0);
    CHECK(keymap_label(KB_RUN, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "A") == 0);
    CHECK(keymap_label(KB_TRIGGER, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "B") == 0);
    CHECK(keymap_label(KB_MENU, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "START") == 0);
    CHECK(keymap_label(KB_MENU2, lb, sizeof lb) == lb);
    CHECK(strcmp(lb, "SELECT") == 0);

    /* an id NOT in the short table still
     * falls back to the SDL string UPPERCASED (today's behavior). Pinned
     * against the SDL string itself — the expectation is "whatever
     * SDL_GameControllerGetStringForButton returns, uppercased", not a
     * hardcoded guess. SDL_CONTROLLER_BUTTON_GUIDE (5) is a real button
     * no KB_* binds on the handheld (config.h binds 0,1,2,3,4,6,9,10,
     * 11,12,13,14). */
    {
        const char *sdl = SDL_GameControllerGetStringForButton(
            SDL_CONTROLLER_BUTTON_GUIDE);
        CHECK(sdl != NULL);
        if (sdl) {
            size_t i;
            char exp[16];
            /* The fallback label must fit the same 16-byte buffers. */
            CHECK(strlen(sdl) + 1 <= sizeof exp);
            for (i = 0; sdl[i] && i + 1 < sizeof exp; i++)
                exp[i] = (char)toupper((unsigned char)sdl[i]);
            exp[i] = '\0';
            CHECK(keymap_label(SDL_CONTROLLER_BUTTON_GUIDE, lb,
                               sizeof lb) == lb);
            CHECK(strcmp(lb, exp) == 0);
        }
    }
    HARNESS_REPORT();
}
