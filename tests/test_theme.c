/* test_theme.c — WCAG AA contrast for the dark-purple UI palette
 * (IMPLEMENTATION.md §6 as amended in 6e7c513 + 6e26d5b). Every pair the UI
 * uses must reach >= 4.5:1; the background family must actually be dark
 * purple so the test cannot pass on black/white. SDL-free: reads only the
 * constants declared in src/ui/theme.h. */
#include "harness.h"
#include "ui/theme.h"

#include <math.h>

/* --- WCAG 2.x relative luminance / contrast (sRGB) ---------------------- */

static double chan_lin(unsigned char c)
{
    double s = (double)c / 255.0;
    return s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
}

static double rel_lum(const ophis_rgb_t *c)
{
    return 0.2126 * chan_lin(c->r) + 0.7152 * chan_lin(c->g) +
           0.0722 * chan_lin(c->b);
}

static double contrast(const ophis_rgb_t *a, const ophis_rgb_t *b)
{
    double la = rel_lum(a), lb = rel_lum(b);
    double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

/* HSL lightness (0..1) for the darkness sanity check. */
static double hsl_lightness(const ophis_rgb_t *c)
{
    double r = c->r / 255.0, g = c->g / 255.0, b = c->b / 255.0;
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
    return (mx + mn) / 2.0;
}

#define CHECK_PAIR(name, fg, bg) \
    do { \
        double cr = contrast(&(fg), &(bg)); \
        printf("  %-22s %3d,%3d,%3d on %3d,%3d,%3d : %.2f:1\n", name, \
               (fg).r, (fg).g, (fg).b, (bg).r, (bg).g, (bg).b, cr); \
        CHECK(cr >= 4.5); \
    } while (0)

int main(void)
{
    /* WCAG AA (normal text) for every text/background pair the UI uses. */
    CHECK_PAIR("text/bg",        THEME_TEXT,       THEME_BG);
    CHECK_PAIR("dim-text/bg",    THEME_DIM_TEXT,   THEME_BG);
    CHECK_PAIR("sel-text/sel-fill", THEME_SEL_TEXT, THEME_SEL_FILL);
    CHECK_PAIR("bar/track",      THEME_BAR,        THEME_TRACK);
    CHECK_PAIR("flash-text/flash-fill", THEME_FLASH_TEXT, THEME_FLASH_FILL);
    CHECK_PAIR("accent/bg",      THEME_ACCENT,     THEME_BG);
    CHECK_PAIR("error/bg",       THEME_ERROR,      THEME_BG);
    /* SEQ selected-step border (PLAN_KEYMAP_REBIND §5): drawn as a frame
     * on the dark backgrounds, so it must pass WCAG AA against the
     * background family AND be clearly stronger than THEME_ACCENT (the
     * old 3-px frame color). */
    CHECK_PAIR("seq-sel/bg",       THEME_SEQ_SEL,  THEME_BG);
    CHECK_PAIR("seq-sel/sel-fill", THEME_SEQ_SEL,  THEME_SEL_FILL);
    CHECK(contrast(&THEME_SEQ_SEL, &THEME_BG) >
          contrast(&THEME_ACCENT, &THEME_BG));
    /* SEQ step markers (owner order): the playing-now dot is ORANGE and
     * sits on all three cell backgrounds */
    CHECK_PAIR("orange/bg",      THEME_ORANGE,     THEME_BG);
    CHECK_PAIR("orange/track",   THEME_ORANGE,     THEME_TRACK);
    CHECK_PAIR("orange/sel-fill", THEME_ORANGE,    THEME_SEL_FILL);

    /* Dark-purple sanity: background family must be dark (HSL lightness
     * < 0.25) so the contrast checks cannot pass on e.g. black/white, and
     * the base background must lean blue (purple hue: B > R > G). */
    CHECK(hsl_lightness(&THEME_BG) < 0.25);
    CHECK(hsl_lightness(&THEME_TRACK) < 0.25);
    CHECK(hsl_lightness(&THEME_SEL_FILL) < 0.25);
    CHECK(THEME_BG.b > THEME_BG.r);
    CHECK(THEME_BG.r > THEME_BG.g);

    HARNESS_REPORT();
}
