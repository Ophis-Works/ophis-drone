/* config.h — THE ONLY TARGET DIFFERENCE (PLAN.md §9, IMPLEMENTATION.md §5).
 *
 * The Makefile passes exactly one macro per target (-DTARGET_DESKTOP,
 * -DTARGET_R36S or -DTARGET_RG28XX); this file is the only place that
 * macro is read.
 *
 * Binding values (KB_*) are the sanctioned change point (PLAN.md §9):
 * the R36S values below are PROPOSED until verified against the physical
 * device in M0 (see CHECKLIST_R36S.md); desktop top-tab keys are Q/E
 * (user-set).
 */
#ifndef OPHIS_CONFIG_H
#define OPHIS_CONFIG_H

#if defined(TARGET_R36S)

  /* Handheld (ArkOS on RK3326): 640x480 panel, scale 1, fullscreen. */
  #define UI_SCALE      1
  #define KB_FULLSCREEN 1

  /* R36S bindings — community mapping (PortMaster DB, printed labels).
   * Do not positionalize (see old/src/io/maps/r36s/input_map.h).
   * Values are SDL *logical* gamepad button constants
   * (SDL_CONTROLLER_BUTTON_*: A=0 B=1 X=2 Y=3 Back=4 Guide=5 Start=6
   * L=9 R=10 DPAD_UP=11 DPAD_DOWN=12 DPAD_LEFT=13 DPAD_RIGHT=14);
   * SDL translates the pad's raw bN buttons via the mapping loaded in
   * main.c (PLAN_KEYMAP_REBIND §3, verified against SDL_gamecontroller.h). */
  #define KB_TRIGGER    1  /* B = TRIGGER/RETRIG (printed B)             */
  #define KB_RUN        0  /* A = RUN/STOP (printed A)                   */
  #define KB_SEL_UP     11 /* d-pad up   (SDL_CONTROLLER_BUTTON_DPAD_UP)   */
  #define KB_SEL_DOWN   12 /* d-pad down (SDL_CONTROLLER_BUTTON_DPAD_DOWN) */
  #define KB_NAV_LEFT   13 /* d-pad left (SDL_CONTROLLER_BUTTON_DPAD_LEFT)  */
  #define KB_NAV_RIGHT  14 /* d-pad right(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) */
  #define KB_VAL_DEC    3  /* Y = value -1 / cycle (printed Y)           */
  #define KB_VAL_INC    2  /* X = value +1 / cycle (printed X)           */
  #define KB_TAB_BACK   9  /* L shoulder (SDL_CONTROLLER_BUTTON_LEFTSHOULDER) */
  #define KB_TAB_NEXT   10 /* R shoulder (SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) */
  #define KB_MENU       6  /* Start      (SDL_CONTROLLER_BUTTON_START)   */
  /* CONFIRM unbound (dead case label -1): A keeps its RUN/confirm double
   * role via KB_RUN_CONFIRMS_MENU below (owner order 2026-11-05).
   * Select = second MENU key (owner demand 2026-11-05): with Start dead
   * on the device, MENU -> EXIT must not depend on it. */
  #define KB_CONFIRM              (-1)
  #define KB_MENU2                4  /* Select (SDL_CONTROLLER_BUTTON_BACK) */
  #define KB_RUN_CONFIRMS_MENU    1
  #define KB_TRIGGER_CANCELS_MENU 1
  /* Select: unassigned (X/Y are value keys; SAVE name entry uses X insert, Y delete). */

#elif defined(TARGET_RG28XX)

  /* Handheld (Allwinner H700, EmuELEC-family firmware): the panel is
   * natively portrait 480x640 — main.c renders the 640x480 UI offscreen
   * and presents it rotated (PLAN_RG28XX_BUILD M3). Scale 1, fullscreen. */
  #define UI_SCALE      1
  #define KB_FULLSCREEN 1

  /* RG28XX bindings — SDL *logical* gamepad button constants, same
   * convention as the R36S block. SDL translates the pad's raw bN
   * buttons via the self-registered mapping in main.c (mainline H700
   * pad measured on-device: printed A=0 B=1 Y=2 X=3, L1=4 R1=5,
   * Select=8 Start=9 Menu=10, D-pad = hat 0). MENU opens on the top
   * Menu key (the firmware maps it to BTN_START/SDL START). */
  #define KB_TRIGGER    1  /* B = TRIGGER/RETRIG (printed B)             */
  #define KB_RUN        0  /* A = RUN/STOP (printed A)                   */
  #define KB_SEL_UP     11 /* d-pad up   (SDL_CONTROLLER_BUTTON_DPAD_UP)   */
  #define KB_SEL_DOWN   12 /* d-pad down (SDL_CONTROLLER_BUTTON_DPAD_DOWN) */
  #define KB_NAV_LEFT   13 /* d-pad left (SDL_CONTROLLER_BUTTON_DPAD_LEFT)  */
  #define KB_NAV_RIGHT  14 /* d-pad right(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) */
  #define KB_VAL_DEC    3  /* Y = value -1 / cycle (printed Y)           */
  #define KB_VAL_INC    2  /* X = value +1 / cycle (printed X)           */
  #define KB_TAB_BACK   9  /* L shoulder (SDL_CONTROLLER_BUTTON_LEFTSHOULDER) */
  #define KB_TAB_NEXT   10 /* R shoulder (SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) */
  #define KB_MENU       6  /* top Menu key (SDL_CONTROLLER_BUTTON_START) */
  #define KB_CONFIRM              (-1)
  #define KB_MENU2                4  /* Select (SDL_CONTROLLER_BUTTON_BACK) */
  #define KB_RUN_CONFIRMS_MENU    1
  #define KB_TRIGGER_CANCELS_MENU 1
  /* Select: unassigned (X/Y are value keys; SAVE name entry uses X insert, Y delete). */

#else /* TARGET_DESKTOP */

  /* Desktop: 640x480 logical at scale 2 => 1280x960 window. */
  #define UI_SCALE      2
  #define KB_FULLSCREEN 0

  /* Desktop bindings — OUR ids (src/ui/keymap.h ophis_key_t), NOT SDL
   * SDLK_* constants. They are resolved to SDL keycodes only inside
   * ui/keymap.c, which keeps the single SDL mapping table.
   * Owner-ordered menu rules (2026-11-05): Enter = CONFIRM (selects
   * options in the Esc menu, saves in name entry), Esc = the ONLY menu
   * cancel/close, T = TRIGGER, Space = RUN/STOP outside menus and
   * nothing inside them. */
  #define KB_TRIGGER    OPHIS_KEY_T       /* T = TRIGGER/RETRIG (rebind)  */
  #define KB_RUN        OPHIS_KEY_SPACE   /* Space = RUN/STOP (rebind)    */
  #define KB_CONFIRM    OPHIS_KEY_RETURN  /* Enter = menu select (rebind) */
  #define KB_SEL_UP     OPHIS_KEY_UP      /* Up    = selection prev       */
  #define KB_SEL_DOWN   OPHIS_KEY_DOWN    /* Down  = selection next       */
  #define KB_NAV_LEFT   OPHIS_KEY_LEFT    /* Left  = navigation (PLAN §8.2) */
  #define KB_NAV_RIGHT  OPHIS_KEY_RIGHT   /* Right = navigation (PLAN §8.2) */
  #define KB_VAL_DEC    OPHIS_KEY_Z       /* Z = value -1 / cycle (rebind) */
  #define KB_VAL_INC    OPHIS_KEY_X       /* X = value +1 / cycle (rebind) */
  #define KB_TAB_BACK   OPHIS_KEY_Q       /* Q = tab back (PLAN §8.2)     */
  #define KB_TAB_NEXT   OPHIS_KEY_E       /* E = tab next (PLAN §8.2)     */
  #define KB_MENU       OPHIS_KEY_ESCAPE  /* Esc = MENU       (PLAN §9)   */
  #define KB_RUN_CONFIRMS_MENU    0
  #define KB_TRIGGER_CANCELS_MENU 0
  #define KB_MENU2                (-1)  /* desktop: Select unbound */

#endif

#endif /* OPHIS_CONFIG_H */
