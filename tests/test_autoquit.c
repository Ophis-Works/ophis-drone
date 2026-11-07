/* tests/test_autoquit.c — PLAN_CODE_REVIEW_FIXES.md Phase 6 P3: the
 * OPHIS_AUTOQUIT_MS parse helper (SDL-free) — strtol with validation:
 * full-consumption decimal, junk rejected, sane range (24 h) clamped. */
#include "harness.h"
#include "app/autoquit.h"

int main(void)
{
    unsigned long ms = 12345;                /* sentinel: overwritten on ok */

    /* plain value */
    CHECK(autoquit_parse("2000", &ms) == 1 && ms == 2000);

    /* 0 is valid and means "disabled" */
    ms = 7;
    CHECK(autoquit_parse("0", &ms) == 1 && ms == 0);

    /* junk: rejected, out untouched */
    ms = 7;
    CHECK(autoquit_parse("12abc", &ms) == 0 && ms == 7);
    ms = 7;
    CHECK(autoquit_parse("", &ms) == 0 && ms == 7);
    ms = 7;
    CHECK(autoquit_parse(NULL, &ms) == 0 && ms == 7);
    ms = 7;
    CHECK(autoquit_parse("-1", &ms) == 0 && ms == 7);
    ms = 7;
    CHECK(autoquit_parse("+100", &ms) == 0 && ms == 7);
    ms = 7;
    CHECK(autoquit_parse(" 100", &ms) == 0 && ms == 7);
    ms = 7;
    CHECK(autoquit_parse("100 ", &ms) == 0 && ms == 7);

    /* sane range: 24 h cap clamps, does not reject */
    CHECK(autoquit_parse("86400000", &ms) == 1 && ms == 86400000UL);
    CHECK(autoquit_parse("86400001", &ms) == 1 && ms == 86400000UL);
    /* overflow (strtol ERANGE -> LONG_MAX) clamps, does not reject */
    CHECK(autoquit_parse("99999999999999999999", &ms) == 1 &&
          ms == 86400000UL);

    HARNESS_REPORT();
}
