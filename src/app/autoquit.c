/* src/app/autoquit.c — see autoquit.h (P3, PLAN_CODE_REVIEW_FIXES.md
 * Phase 6): strtol with full-consumption validation replacing the bare
 * atoi() on OPHIS_AUTOQUIT_MS. */
#include "autoquit.h"

#include <ctype.h>
#include <stdlib.h>

#define AUTOQUIT_MAX_MS 86400000UL          /* 24 h: sane ceiling */

int autoquit_parse(const char *s, unsigned long *out_ms)
{
    if (s == NULL || !isdigit((unsigned char)s[0]))
        return 0;                           /* NULL/empty/sign/space: junk */
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return 0;                           /* no digits / trailing junk */
    unsigned long ms = (v <= 0) ? 0UL : (unsigned long)v;
    if (ms > AUTOQUIT_MAX_MS)
        ms = AUTOQUIT_MAX_MS;               /* clamp (covers ERANGE too) */
    *out_ms = ms;
    return 1;
}
