/* tests/harness.h — plain-C assert harness, prints "N/M passed".
 * Also carries the DSP measurement helpers shared
 * by the DSP tests (test-side code only). */
#ifndef OPHIS_HARNESS_H
#define OPHIS_HARNESS_H

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "dsp/exact_eq.h"   /* fexact/fexactf: the shared exact-equality
                               helpers (-Wfloat-equal convention lives there) */

static int h_total = 0, h_passed = 0;

#define CHECK(cond) do { \
    h_total++; \
    if (cond) { h_passed++; } \
    else { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define HARNESS_REPORT() do { \
    printf("%d/%d passed\n", h_passed, h_total); \
    return h_passed == h_total ? 0 : 1; \
} while (0)

#define OPHIS_PI 3.14159265358979323846

/* Amplitude estimate of frequency f in x[0..n) via Goertzel (single bin,
 * nearest-integer bin). */
static double __attribute__((unused)) h_goertzel_mag(const double *x, int n, double f, double sr)
{
    double k   = floor(n * f / sr + 0.5);
    double w   = 2.0 * OPHIS_PI * k / n;
    double c   = cos(w);
    double u   = 2.0 * c;
    double s0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; i++) {
        s0 = x[i] + u * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - u * s1 * s2) * 2.0 / n;
}

static double __attribute__((unused)) h_mean(const double *x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++) s += x[i];
    return s / n;
}

static double __attribute__((unused)) h_rms(const double *x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++) s += x[i] * x[i];
    return sqrt(s / n);
}

/* sign-change count (zero-crossings) */
static int __attribute__((unused)) h_zc(const double *x, int n)
{
    int c = 0;
    for (int i = 1; i < n; i++)
        if ((x[i-1] < 0.0 && x[i] >= 0.0) || (x[i-1] >= 0.0 && x[i] < 0.0))
            c++;
    return c;
}

/* zero-crossing rate in Hz over n samples at sample rate sr */
static double __attribute__((unused)) h_zc_rate(const double *x, int n, double sr)
{
    return (double)h_zc(x, n) / 2.0 / ((double)n / sr);
}

static double __attribute__((unused)) h_db(double v) { return 20.0 * log10(v); }

#endif
