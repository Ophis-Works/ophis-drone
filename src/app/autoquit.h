/* src/app/autoquit.h — OPHIS_AUTOQUIT_MS smoke auto-quit parsing.
 * SDL-free so the unit test can
 * link it without the window stack. */
#ifndef OPHIS_AUTOQUIT_H
#define OPHIS_AUTOQUIT_H

/* Parse a millisecond value from s. Returns 1 and stores the result in
 * *out_ms when s is a non-negative decimal integer consumed in full
 * (strtol + endptr; leading/trailing whitespace, signs and trailing
 * junk are rejected). Values above 24 h are clamped to 86400000;
 * overflow clamps the same way. Returns 0 for anything else (caller
 * keeps the default: no auto-quit). */
int autoquit_parse(const char *s, unsigned long *out_ms);

#endif /* OPHIS_AUTOQUIT_H */
