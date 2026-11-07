/* src/ui/font8x8.h — embedded bitmap font (PLAN.md §8.6 AMENDED, owner
 * order: letters +2 pts). Glyphs render in 10x10 cells — a per-pixel
 * scale of the embedded 8x8 face; a "line" = 10 px. SDL-free on purpose
 * so the test harness (tests/test_font.c) needs no SDL. */
#ifndef OPHIS_FONT8X8_H
#define OPHIS_FONT8X8_H

#include <stddef.h>

#define OPHIS_FONT_W 10
#define OPHIS_FONT_H 10

/* the embedded 8x8 face: glyph8x8[ch - 0x20][row], bit 0 = leftmost */
extern const unsigned char glyph8x8[96][8];

/* the 10x10 scaled glyph for ch (0x20..0x7F): 10 byte rows, bit dx =
 * column dx; NULL outside the glyph set. */
const unsigned char *font_glyph10(unsigned char ch);

#endif /* OPHIS_FONT8X8_H */
