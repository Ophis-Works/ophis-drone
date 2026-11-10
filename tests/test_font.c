/* tests/test_font.c — font metrics + 8x8->10x10 scaler invariants
 * (letters +2 pts, a "line" =
 * 10 px). OPHIS_FONT_W/H == 10. font_glyph10() is a faithful per-pixel
 * scale of the embedded 8x8 face: (1) every set scaled pixel at (dx,dy)
 * implies the source bit ((dy*8)/10, (dx*8)/10) is set; (2) every set
 * source bit (r,c) has at least one set scaled pixel; (3) scaled rows
 * use only bits 0..9. Out-of-range chars: the space glyph or NULL —
 * consistently. SDL-free: reads only src/ui/font8x8.h. TDD: written
 * BEFORE font8x8.h exists (strict TDD, watched failing first). */
#include <string.h>
#include "harness.h"
#include "../src/ui/font8x8.h"

int main(void)
{
    /* ---- metrics: glyphs render in 10x10 cells ---- */
    CHECK(OPHIS_FONT_W == 10);
    CHECK(OPHIS_FONT_H == 10);

    /* ---- scaler invariants over ALL 96 glyphs via font_glyph10() ---- */
    for (int i = 0; i < 96; i++) {
        unsigned char ch = (unsigned char)(i + 0x20);
        const unsigned char *g = font_glyph10(ch);
        CHECK(g != NULL);
        if (!g)
            continue;

        /* (3) scaled rows use only bits 0..9 */
        for (int dy = 0; dy < OPHIS_FONT_H; dy++)
            CHECK((g[dy] & ~((1u << 10) - 1u)) == 0);

        /* (1) every set scaled pixel implies its source bit is set */
        for (int dy = 0; dy < OPHIS_FONT_H; dy++)
            for (int dx = 0; dx < OPHIS_FONT_W; dx++)
                if (g[dy] & (1u << dx))
                    CHECK((glyph8x8[i][(dy * 8) / 10] &
                           (1u << ((dx * 8) / 10))) != 0);

        /* (2) every set source bit has at least one set scaled pixel */
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (glyph8x8[i][r] & (1u << c)) {
                    int hit = 0;
                    for (int dy = 0; dy < OPHIS_FONT_H && !hit; dy++)
                        for (int dx = 0; dx < OPHIS_FONT_W; dx++)
                            if ((dy * 8) / 10 == r &&
                                (dx * 8) / 10 == c &&
                                (g[dy] & (1u << dx))) {
                                hit = 1;
                                break;
                            }
                    CHECK(hit);
                }
    }

    /* ---- range sanity: ' ' is blank, 'a' is not ---- */
    const unsigned char *sp = font_glyph10(' ');
    CHECK(sp != NULL);
    {
        int blank = 1;
        for (int dy = 0; dy < OPHIS_FONT_H; dy++)
            if (sp[dy] != 0)
                blank = 0;
        CHECK(blank);
    }
    {
        const unsigned char *ga = font_glyph10('a');
        CHECK(ga != NULL);
        int any = 0;
        for (int dy = 0; dy < OPHIS_FONT_H; dy++)
            if (ga[dy] != 0)
                any = 1;
        CHECK(any);
    }

    /* ---- out of range (ch < 0x20 or ch > 0x7E): the space glyph or
     * NULL — the SAME answer every time, consistently on both branches */
    {
        const unsigned char *lo = font_glyph10(0);
        const unsigned char *hi = font_glyph10((unsigned char)0xFF);
        CHECK(lo == NULL || memcmp(lo, sp, OPHIS_FONT_H) == 0);
        CHECK(hi == NULL || memcmp(hi, sp, OPHIS_FONT_H) == 0);
        for (int ch = 0; ch < 0x20; ch++)
            CHECK(font_glyph10((unsigned char)ch) == lo);
        for (int ch = 0x80; ch <= 0xFF; ch++)
            CHECK(font_glyph10((unsigned char)ch) == hi);
        CHECK(lo == hi);
    }
    /* 0x7F (DEL, the 96th glyph): blank — content-identical to space */
    {
        const unsigned char *g = font_glyph10((unsigned char)0x7F);
        CHECK(g != NULL);
        CHECK(memcmp(g, sp, OPHIS_FONT_H) == 0);
    }

    HARNESS_REPORT();
}
