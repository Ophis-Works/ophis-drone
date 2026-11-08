/* tests/test_osc.c — PLAN.md §10 (M1):
 * zero-crossing rate ±1 for sine/tri/saw/square; white: DC≈0, RMS≈1/√3,
 * flat spectrum (Goertzel), seeded determinism (xoshiro256**, fixed seed
 * per instance); pink: DC≈0, ≈−3 dB/octave slope (Goertzel, ±1.5 dB);
 * no |x|>1 for any waveform.
 * Approved plan change (commits eacb368+fix): square DUTY — pulse width
 * percent 5..95 default 50; measured as the high-time fraction of one
 * period at the zero-crossing level; duty is ignored by all other waves. */
#include <string.h>
#include "harness.h"
#include "dsp/osc.h"

#define SR 44100.0
#define N  44100

static double buf[N];

/* magnitude estimate for freq f: Goertzel averaged over 10 equal segments
 * of the buffer (single-bin estimates are Rayleigh-distributed; averaging
 * stabilizes the estimator without changing what is asserted). */
static double avg_mag(const double *x, int n, double f, double sr)
{
    const int K = 10;
    int seg = n / K;
    double s = 0.0;
    for (int k = 0; k < K; k++)
        s += h_goertzel_mag(x + k * seg, seg, f, sr);
    return s / K;
}

static void render(wave_t w, uint64_t seed, double f)
{
    osc_t o;
    osc_init(&o, w, seed);
    for (int i = 0; i < N; i++) buf[i] = osc_next(&o, f, SR);
}

int main(void)
{
    /* ---- tonal waves: zero-crossing rate ±1 (440 Hz over 1 s => 880
     * crossings; ±1 Hz on the rate => ±2 crossings) ---- */
    const wave_t tonal[4] = { WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_SQUARE };
    for (int i = 0; i < 4; i++) {
        render(tonal[i], 42, 440.0);
        CHECK(h_zc(buf, N) >= 878 && h_zc(buf, N) <= 882);
    }

    /* ---- white noise ---- */
    render(WAVE_WHITE, 12345, 440.0);
    CHECK(fabs(h_mean(buf, N)) < 0.05);                        /* DC ≈ 0    */
    CHECK(fabs(h_rms(buf, N) - 1.0 / sqrt(3.0)) < 0.02);       /* RMS≈1/√3  */

    /* flat spectrum (Goertzel): octave-spaced bins within 6 dB */
    static const double wf[8] = { 110, 220, 440, 880, 1760, 3520, 7040, 14080 };
    double wmag[8];
    for (int i = 0; i < 8; i++) wmag[i] = avg_mag(buf, N, wf[i], SR);
    double wmax = wmag[0], wmin = wmag[0];
    for (int i = 1; i < 8; i++) {
        if (wmag[i] > wmax) wmax = wmag[i];
        if (wmag[i] < wmin) wmin = wmag[i];
    }
    CHECK(h_db(wmax / wmin) < 6.0);

    /* seeded determinism: same seed identical, different seed different */
    render(WAVE_WHITE, 777, 440.0);
    double ref[N];
    memcpy(ref, buf, sizeof buf);
    render(WAVE_WHITE, 777, 440.0);
    CHECK(memcmp(ref, buf, sizeof buf) == 0);
    render(WAVE_WHITE, 778, 440.0);
    CHECK(memcmp(ref, buf, sizeof buf) != 0);

    /* ---- pink noise ---- */
    render(WAVE_PINK, 999, 440.0);
    CHECK(fabs(h_mean(buf, N)) < 0.05);                        /* DC ≈ 0    */

    static const double pf[7] = { 100, 200, 400, 800, 1600, 3200, 6400 };
    double pmag[7];
    for (int i = 0; i < 7; i++) pmag[i] = avg_mag(buf, N, pf[i], SR);
    double slope = 0.0;
    for (int i = 0; i < 6; i++)
        slope += h_db(pmag[i + 1] / pmag[i]);
    slope /= 6.0;
    CHECK(fabs(fabs(slope) - 3.01) <= 1.5);                    /* −3 dB/oct */

    /* ---- no |x| > 1 for ANY waveform ---- */
    for (int w = 0; w < 6; w++) {
        render((wave_t)w, 31337, 100.0);
        int over = 0;
        for (int i = 0; i < N; i++) if (fabs(buf[i]) > 1.0) over++;
        CHECK(over == 0);
    }

    /* ---- square DUTY (PLAN §5, approved change) ----
     * f = 20 Hz at 44100 Hz -> one period = 2205 samples, sampled densely.
     * duty% == fraction of samples > 0 (the zero-crossing level) over one
     * period, ±1 percentage-point tolerance. */
    {
        const double f = 20.0;
        const int P = (int)(SR / f);           /* 2205 samples / period */

        /* high-time fraction over one period starting at sample offset
         * `skip`, for an osc whose duty was set to `duty_set` */
        #define DUTY_PCT(duty_set, skip) do { \
            osc_t o; \
            osc_init(&o, WAVE_SQUARE, 42); \
            o.duty = (duty_set); \
            for (int s = 0; s < (skip); s++) (void)osc_next(&o, f, SR); \
            int hi = 0; \
            for (int s = 0; s < P; s++) \
                if (osc_next(&o, f, SR) > 0.0) hi++; \
            duty_meas = 100.0 * (double)hi / (double)P; \
        } while (0)

        double duty_meas;

        /* measured duty ≈ set duty across the range */
        static const struct { int set; double expect; } dcases[] = {
            {5, 5.0}, {25, 25.0}, {50, 50.0}, {75, 75.0}, {95, 95.0},
        };
        for (int i = 0; i < 5; i++) {
            DUTY_PCT(dcases[i].set, 0);
            CHECK(fabs(duty_meas - dcases[i].expect) < 1.0);
        }

        /* duty 5..95 clamps: out-of-range values behave as 5 / 95 */
        DUTY_PCT(0, 0);   CHECK(fabs(duty_meas - 5.0)  < 1.0);
        DUTY_PCT(-7, 0);  CHECK(fabs(duty_meas - 5.0)  < 1.0);
        DUTY_PCT(100, 0); CHECK(fabs(duty_meas - 95.0) < 1.0);
        DUTY_PCT(250, 0); CHECK(fabs(duty_meas - 95.0) < 1.0);

        /* default duty is 50: a fresh init without touching duty at all */
        {
            osc_t o;
            osc_init(&o, WAVE_SQUARE, 42);
            int hi = 0;
            for (int s = 0; s < P; s++)
                if (osc_next(&o, f, SR) > 0.0) hi++;
            CHECK(fabs(100.0 * (double)hi / (double)P - 50.0) < 1.0);
        }

        /* other waves ignore duty: identical output with duty 5 vs 95 */
        for (int w = 0; w < 6; w++) {
            if ((wave_t)w == WAVE_SQUARE) continue;
            osc_t a, b;
            osc_init(&a, (wave_t)w, 4242); a.duty = 5;
            osc_init(&b, (wave_t)w, 4242); b.duty = 95;
            int differ = 0;
            for (int s = 0; s < N; s++)
                if (!fexact(osc_next(&a, 440.0, SR),
                            osc_next(&b, 440.0, SR)))
                    { differ = 1; break; }
            CHECK(differ == 0);
        }
        #undef DUTY_PCT
    }

    HARNESS_REPORT();
}
