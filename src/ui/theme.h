/* theme.h — UI palette constants (IMPLEMENTATION.md §6, amended: dark-purple
 * theme, WCAG AA >= 4.5:1 for every text/background pair, unit-tested in
 * tests/test_theme.c). Declarations only — the definitions live in
 * ui/theme.c (implementation, later turn). SDL-free on purpose so the test
 * harness needs no SDL. */
#ifndef OPHIS_THEME_H
#define OPHIS_THEME_H

typedef struct {
    unsigned char r, g, b;
} ophis_rgb_t;

/* background family (dark purple; relative luminance < 0.25) */
extern const ophis_rgb_t THEME_BG;        /* page background               */
extern const ophis_rgb_t THEME_TRACK;     /* slider/bar track, panel fill  */
extern const ophis_rgb_t THEME_SEL_FILL;  /* selection highlight fill      */
extern const ophis_rgb_t THEME_FLASH_FILL;/* transient flash fill          */

/* foreground family */
extern const ophis_rgb_t THEME_TEXT;       /* normal text on BG            */
extern const ophis_rgb_t THEME_DIM_TEXT;   /* dimmed labels on BG          */
extern const ophis_rgb_t THEME_SEL_TEXT;   /* text on SEL_FILL             */
extern const ophis_rgb_t THEME_BAR;        /* bar value on TRACK           */
extern const ophis_rgb_t THEME_FLASH_TEXT; /* text on FLASH_FILL           */
extern const ophis_rgb_t THEME_ACCENT;     /* active value / cursor accent */
extern const ophis_rgb_t THEME_SEQ_SEL;    /* SEQ selected-step border (PLAN
                                            * KEYMAP_REBIND §5) — on
                                            * BG/TRACK/SEL_FILL */
extern const ophis_rgb_t THEME_ERROR;      /* error messages on BG         */
extern const ophis_rgb_t THEME_ORANGE;    /* SEQ playing-step dot (owner
                                           * order) — on BG/TRACK/SEL_FILL */

#endif /* OPHIS_THEME_H */
