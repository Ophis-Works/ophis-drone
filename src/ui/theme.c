/* theme.c — dark-purple palette definitions. Values chosen so
 * tests/test_theme.c passes WCAG AA
 * (>= 4.5:1) on every pair, with margin:
 *   text/bg 15.0, dim/bg 6.0, sel-text/sel-fill 13.2, bar/track 7.3,
 *   flash-text/flash-fill 9.4 (ratios at time of writing). */
#include "ui/theme.h"

/* background family (dark purple) */
const ophis_rgb_t THEME_BG         = { 24, 12, 40 };  /* deep purple      */
const ophis_rgb_t THEME_TRACK      = { 46, 28, 72 };  /* track / panel    */
const ophis_rgb_t THEME_SEL_FILL   = { 52, 30, 88 };  /* purple accent    */
const ophis_rgb_t THEME_FLASH_FILL = { 96, 20, 40 };  /* deep magenta     */

/* foreground family */
const ophis_rgb_t THEME_TEXT       = { 232, 228, 245 }; /* lavender-white */
const ophis_rgb_t THEME_DIM_TEXT   = { 150, 140, 178 }; /* muted lavender */
const ophis_rgb_t THEME_SEL_TEXT   = { 248, 244, 255 }; /* near-white     */
const ophis_rgb_t THEME_BAR        = { 196, 166, 240 }; /* light violet   */
const ophis_rgb_t THEME_FLASH_TEXT = { 255, 208, 214 }; /* pink-white     */
const ophis_rgb_t THEME_ACCENT     = { 186, 132, 255 }; /* bright violet  */
const ophis_rgb_t THEME_SEQ_SEL    = { 0, 230, 255 };   /* SEQ selected-step
                                            * border: strong-saturation cyan,
                                            * clearly stronger than ACCENT
                                            * (bg 12.3:1 vs 7.0:1, sel-fill
                                            * 9.4:1) */
const ophis_rgb_t THEME_ERROR      = { 255, 118, 130 }; /* soft red-pink  */
const ophis_rgb_t THEME_ORANGE     = { 255, 150, 30 };  /* playing dot    */
