/* tests/test_keymap.c — compile-level keymap binding check (M6, rebind
 * PLAN_KEYMAP_REBIND §1) + the keymap_label() caller-buffer contract
 * (remediation F5). Compiled with -DTARGET_DESKTOP, SDL-linked (labels
 * come from SDL_GetKeyName via ui/keymap.c, so the static-buffer aliasing
 * F5 fixes is covered by comparing two separate caller buffers).
 * Asserts the desktop KB_* bindings are OUR key ids (src/ui/keymap.h
 * ophis_key_t), not SDL SDLK_* constants. Owner-ordered rebinding
 * (2026-11-05): T = TRIGGER, Space = RUN (outside menus only),
 * Enter = CONFIRM (menu select), Esc = MENU (desktop's only cancel). */
#include "harness.h"

#include <string.h>

#include "../src/config.h"
#include "../src/ui/keymap.h"

int main(void)
{
#if !defined(TARGET_DESKTOP)
    #error "this test must be compiled with -DTARGET_DESKTOP"
#endif
    /* OURS — ophis key ids, not SDL keycodes. */
    CHECK(KB_TAB_BACK == OPHIS_KEY_Q);
    CHECK(KB_TAB_NEXT == OPHIS_KEY_E);
    CHECK(KB_RUN       == OPHIS_KEY_SPACE);
    CHECK(KB_TRIGGER   == OPHIS_KEY_T);
    CHECK(KB_CONFIRM   == OPHIS_KEY_RETURN);
    CHECK(KB_SEL_UP    == OPHIS_KEY_UP);
    CHECK(KB_SEL_DOWN  == OPHIS_KEY_DOWN);
    CHECK(KB_NAV_LEFT  == OPHIS_KEY_LEFT);
    CHECK(KB_NAV_RIGHT == OPHIS_KEY_RIGHT);
    CHECK(KB_VAL_DEC   == OPHIS_KEY_Z);
    CHECK(KB_VAL_INC   == OPHIS_KEY_X);
    CHECK(KB_MENU      == OPHIS_KEY_ESCAPE);
    CHECK(KB_MENU2     == -1);   /* desktop: Select unbound (dead label) */
    /* Target semantic flags: on desktop Enter confirms and Esc is the
     * ONLY cancel — Space does not confirm, TRIGGER does not cancel. */
    CHECK(KB_RUN_CONFIRMS_MENU == 0);
    CHECK(KB_TRIGGER_CANCELS_MENU == 0);

    /* Sanity: no SDLK_* leaked into config.h (SDLK_* are large values,
     * our ids are 0..11 — every KB_* must be in that id range). */
    CHECK((int)KB_TRIGGER   < 12);
    CHECK((int)KB_RUN       < 12);
    CHECK((int)KB_SEL_UP    < 12);
    CHECK((int)KB_SEL_DOWN  < 12);
    CHECK((int)KB_NAV_LEFT  < 12);
    CHECK((int)KB_NAV_RIGHT < 12);
    CHECK((int)KB_VAL_DEC   < 12);
    CHECK((int)KB_VAL_INC   < 12);
    CHECK((int)KB_TAB_BACK  < 12);
    CHECK((int)KB_TAB_NEXT  < 12);
    CHECK((int)KB_MENU      < 12);

    /* F5: caller-buffer label contract — the label is written into the
     * CALLER's buffer and buf is returned; two bindings labelled into two
     * separate buffers keep distinct strings (the old single static buffer
     * aliased every label to one pointer). */
    char b1[16], b2[16];
    CHECK(keymap_label(KB_TAB_BACK, b1, sizeof b1) == b1);
    CHECK(keymap_label(KB_TAB_NEXT, b2, sizeof b2) == b2);
    CHECK(strcmp(b1, "Q") == 0);
    CHECK(strcmp(b2, "E") == 0);
    /* Known desktop labels (SDL_GetKeyName capitalization). */
    CHECK(keymap_label(KB_RUN, b1, sizeof b1) == b1);
    CHECK(strcmp(b1, "Space") == 0);
    CHECK(keymap_label(KB_TRIGGER, b1, sizeof b1) == b1);
    CHECK(strcmp(b1, "T") == 0);
    CHECK(keymap_label(KB_CONFIRM, b1, sizeof b1) == b1);
    CHECK(strcmp(b1, "Return") == 0);
    CHECK(keymap_label(KB_VAL_DEC, b1, sizeof b1) == b1);
    CHECK(strcmp(b1, "Z") == 0);
    CHECK(keymap_label(KB_VAL_INC, b1, sizeof b1) == b1);
    CHECK(strcmp(b1, "X") == 0);
    CHECK(keymap_label(KB_MENU, b1, sizeof b1) == b1);
    CHECK(strcmp(b1, "Escape") == 0);
    /* Does not fit -> NULL ("Space" needs 6 incl NUL); exactly fits -> buf. */
    CHECK(keymap_label(KB_RUN, b1, 1) == NULL);
    CHECK(keymap_label(KB_RUN, b1, 5) == NULL);
    CHECK(keymap_label(KB_RUN, b1, 6) == b1);
    /* Unmapped value -> NULL. */
    CHECK(keymap_label(100, b1, sizeof b1) == NULL);
    HARNESS_REPORT();
}
