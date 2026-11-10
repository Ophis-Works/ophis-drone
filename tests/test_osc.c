/* tests/test_osc.c —
 * zero-crossing rate ±1 for sine/tri/saw/square; white: DC≈0, RMS≈1/√3,
 * flat spectrum (Goertzel), seeded determinism (xoshiro256**, fixed seed
 * per instance); pink: DC≈0, ≈−3 dB/octave slope (Goertzel, ±1.5 dB);
 * no |x|>1 for any waveform.
 * square DUTY — pulse width
 * percent 5..95 default 50; measured as the high-time fraction of one
 * period at the zero-crossing level; duty is ignored by all other waves. */
#include <string.h>
#include "harness.h"
#include "dsp/osc.h"
#include "dsp/prng.h"

#define SR 44100.0
#define N  44100

/* ---- naive reference + structural contracts ----
 * The test mirrors the PRE-PolyBLEP osc_next math (naive formulas + the
 * identical phase accumulation and PRNG path) so the contracts below are
 * pins against the naive output, not self-comparisons. */

#define MS1_F  220.0                         /* ~220 Hz per the plan     */
#define MS1_DT (MS1_F / SR)                  /* phase step per sample    */
#define MS1_N  1000                          /* ~4.99 periods >= 4       */
#define MS1_PERIOD (SR / MS1_F)              /* 200.45 samples/period    */

typedef struct {
    double   phase;
    int      duty;
    uint64_t rng[4];
    double   pink[3];
} naive_t;

static void naive_init(naive_t *n, uint64_t seed, int duty)
{
    n->phase = 0.0;
    n->duty  = duty;
    prng_seed(n->rng, seed);
    n->pink[0] = n->pink[1] = n->pink[2] = 0.0;
}

/* naive per-sample value, phase accumulation identical to osc_next */
static double naive_next(naive_t *n, wave_t w, double f, double sr)
{
    n->phase += f / sr;
    if (n->phase >= 1.0)
        n->phase -= floor(n->phase);
    double x = 0.0;
    switch (w) {
    case WAVE_SINE:
        x = sin(2.0 * OPHIS_PI * n->phase);
        break;
    case WAVE_TRIANGLE:
        x = 4.0 * fabs(n->phase - 0.5) - 1.0;
        break;
    case WAVE_SAW:
        x = 2.0 * n->phase - 1.0;
        break;
    case WAVE_SQUARE: {
        int d = n->duty;
        if (d < 5)  d = 5;
        if (d > 95) d = 95;
        x = (n->phase < (double)d / 100.0) ? 1.0 : -1.0;
        break;
    }
    case WAVE_WHITE:
        x = (double)(prng_next(n->rng) >> 11)
            * (2.0 / 9007199254740992.0) - 1.0;
        break;
    case WAVE_PINK: {
        double white = (double)(prng_next(n->rng) >> 11)
            * (2.0 / 9007199254740992.0) - 1.0;
        n->pink[0] = 0.99765 * n->pink[0] + white * 0.0990460;
        n->pink[1] = 0.96300 * n->pink[1] + white * 0.2965164;
        n->pink[2] = 0.57000 * n->pink[2] + white * 1.0526913;
        x = (n->pink[0] + n->pink[1] + n->pink[2] + white * 0.1848) * 0.2;
        if (x > 1.0)  x = 1.0;
        if (x < -1.0) x = -1.0;
        break;
    }
    }
    return x;
}

/* ---- 1x reference
 * mirror — the pre-2x osc_next (1x phase path + PolyBLEP window dt, the
 * single-kernel PolyBLEP) as the comparison arm of the alias probe ---- */

/* 1x PolyBLEP kernel: window dt, edges {0} (saw, A=-2) /
 * {0, duty/100} (square, A=+2 wrap, A=-2 duty) */
static double ms1_kernel(double x, wave_t w, int duty, double p, double dt)
{
    double e0 = 0.0, a0 = -2.0, e1 = -1.0, a1 = 0.0;
    int ne = 1;
    if (w == WAVE_SQUARE) {
        int d = duty;
        if (d < 5)  d = 5;
        if (d > 95) d = 95;
        e1 = (double)d / 100.0;
        a0 = 2.0;
        a1 = -2.0;
        ne = 2;
    }
    const double es[2] = { e0, e1 };
    const double as[2] = { a0, a1 };
    for (int i = 0; i < ne; i++) {
        double d = p - es[i];
        d -= round(d);               /* wrap to (-0.5, 0.5] */
        double ad = fabs(d);
        if (ad < dt) {
            double s = ad / dt, u = 1.0 - s;
            if (d < 0.0)
                x += as[i] * 0.5 * u * u;   /* pre-side  */
            else
                x -= as[i] * 0.5 * s * s;   /* post-side */
        }
    }
    return x;
}

/* one 1x output sample (mirror of the pre-2x osc_next tonal path) */
static double ms1_ref_next(naive_t *n, wave_t w, double f, double sr)
{
    double dt = f / sr;
    n->phase += dt;
    if (n->phase >= 1.0)
        n->phase -= floor(n->phase);
    double x;
    switch (w) {
    case WAVE_SINE:
        x = sin(2.0 * OPHIS_PI * n->phase);
        break;
    case WAVE_TRIANGLE:
        x = 4.0 * fabs(n->phase - 0.5) - 1.0;
        break;
    case WAVE_SAW:
        x = 2.0 * n->phase - 1.0;
        break;
    case WAVE_SQUARE: {
        int d = n->duty;
        if (d < 5)  d = 5;
        if (d > 95) d = 95;
        x = (n->phase < (double)d / 100.0) ? 1.0 : -1.0;
        break;
    }
    case WAVE_WHITE:
    case WAVE_PINK:
    default:
        x = 0.0;
        break;
    }
    if (w == WAVE_SAW || w == WAVE_SQUARE)
        x = ms1_kernel(x, w, n->duty, n->phase, dt);
    return x;
}

/* is the sample with post-wrap phase p within the 2*dt correction window
 * of any edge of w (SAW: {0}; SQUARE: {0, duty/100})? */
static int near_edge(wave_t w, double p, int duty, double dt)
{
    double e0 = 0.0, e1 = -1.0;
    if (w == WAVE_SQUARE) e1 = (double)duty / 100.0;
    const double es[2] = { e0, e1 };
    for (int i = 0; i < 2; i++) {
        if (es[i] < 0.0) continue;
        double d = p - es[i];
        d -= round(d);                   /* wrap to (-0.5, 0.5] */
        if (fabs(d) < 2.0 * dt) return 1;
    }
    return 0;
}

/* far-from-edge deviation bound: with 2x oversampling the output is
 * the box average of two dt/2 sub-steps, so even far from an edge it can
 * deviate from the naive point value by at most slope*dt/2 (square slope
 * 0 far from edges -> still bit-exact). */
static double far_bound(wave_t w)
{
    switch (w) {
    case WAVE_SINE:     return 2.0 * OPHIS_PI * MS1_DT / 2.0;
    case WAVE_TRIANGLE: return 4.0 * MS1_DT / 2.0;
    case WAVE_SAW:      return 2.0 * MS1_DT / 2.0;
    case WAVE_SQUARE:
    case WAVE_WHITE:
    case WAVE_PINK:
    default:            return 0.0;   /* WAVE_SQUARE: bit-exact far */
    }
}

/* render MS1_N samples of w through the real osc_next with duty set */
static void ms1_render(wave_t w, int duty, double out[MS1_N])
{
    osc_t o;
    osc_init(&o, w, 42);
    o.duty = duty;
    for (int i = 0; i < MS1_N; i++) out[i] = osc_next(&o, MS1_F, SR);
}


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

    /* ---- amplitude bound: saw/square may
     * overshoot +-1 up to a hard |x| <= 2.0 (PolyBLEP edge smoothing;
     * downstream tanh + master safety filter contain it); sine/tri/
     * white/pink stay within +-1 ---- */
    for (int w = 0; w < 6; w++) {
        double bound = ((wave_t)w == WAVE_SAW ||
                        (wave_t)w == WAVE_SQUARE) ? 2.0 : 1.0;
        render((wave_t)w, 31337, 100.0);
        int over = 0;
        for (int i = 0; i < N; i++) if (fabs(buf[i]) > bound) over++;
        CHECK(over == 0);
    }

    /* ---- square DUTY ----
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

    /* ==== PolyBLEP contracts ========================================== */

    /* -- 1. far-from-edge pin + golden-pin rule (2x
     * oversampling box-averages the tonal waves, so far from an edge the
     * output may deviate from the naive point value by at most
     * slope*dt/2 — square far region stays bit-exact); the ONLY samples
     * allowed a larger deviation are within 2*dt of an edge (saw +
     * square 50/5/95) -- */
    {
        static const struct { wave_t w; int duty; } edge_cases[] = {
            { WAVE_SAW, 50 }, { WAVE_SQUARE, 50 },
            { WAVE_SQUARE, 5 }, { WAVE_SQUARE, 95 },
        };
        for (int c = 0; c < 4; c++) {
            double out[MS1_N];
            ms1_render(edge_cases[c].w, edge_cases[c].duty, out);
            naive_t n;
            naive_init(&n, 42, edge_cases[c].duty);
            double fb = far_bound(edge_cases[c].w) + 1e-9;
            int far_diff = 0, near_diff = 0;
            for (int i = 0; i < MS1_N; i++) {
                double nv = naive_next(&n, edge_cases[c].w, MS1_F, SR);
                if (!fexact(out[i], nv)) {
                    /* the half-band FIR mixes +-3h (1.5*dt) taps
                     * around each output sample, so corrected sub-values
                     * influence samples up to 2*dt + 1.5*dt of an edge */
                    if (near_edge(edge_cases[c].w, n.phase,
                                  edge_cases[c].duty, 3.5 * MS1_DT))
                        near_diff++;
                    else if (fabs(out[i] - nv) > fb) far_diff++;
                }
            }
            CHECK(far_diff == 0);      /* far-from-edge within bound   */
            CHECK(near_diff > 0);      /* the edge region IS corrected  */
        }
    }

    /* -- 2. continuity (the alias killer): every per-sample jump strictly
     * < 2.0 over >= 4 periods (replacing the
     * earlier 4*dt bound, which no band-limited saw can meet; the naive
     * SQUARE jumps exactly 2.0 at the wrap and duty edge and must fail
     * this loudly) -- */
    {
        static const struct { wave_t w; int duty; } cont_cases[] = {
            { WAVE_SAW, 50 }, { WAVE_SQUARE, 50 },
            { WAVE_SQUARE, 5 }, { WAVE_SQUARE, 95 },
        };
        for (int c = 0; c < 4; c++) {
            double out[MS1_N];
            ms1_render(cont_cases[c].w, cont_cases[c].duty, out);
            int over = 0;
            for (int i = 1; i < MS1_N; i++)
                if (fabs(out[i] - out[i - 1]) >= 2.0) over++;
            CHECK(over == 0);
        }
    }

    /* -- 3. square duty mean preserved: mean over the window ~= 2*d/100-1,
     * tolerance 1/period, both edges corrected (duty 50 + 5/95 edges) -- */
    {
        static const int duties[3] = { 50, 5, 95 };
        for (int c = 0; c < 3; c++) {
            double out[MS1_N];
            ms1_render(WAVE_SQUARE, duties[c], out);
            double mean = 0.0;
            for (int i = 0; i < MS1_N; i++) mean += out[i];
            mean /= (double)MS1_N;
            CHECK(fabs(mean - (2.0 * (double)duties[c] / 100.0 - 1.0))
                  <= 1.0 / MS1_PERIOD);
        }
    }

    /* -- 4. untouched waves: white/pink (noise, stays 1x)
     * bit-exact to the naive reference over >= 4 periods; sine/tri
     * (tonal, now 2x oversampled) within the averaging deviation bound
     * slope*dt/2 of naive -- */
    {
        static const wave_t noise_waves[2] = { WAVE_WHITE, WAVE_PINK };
        for (int c = 0; c < 2; c++) {
            double out[MS1_N];
            ms1_render(noise_waves[c], 50, out);
            naive_t n;
            naive_init(&n, 42, 50);
            int diff = 0;
            for (int i = 0; i < MS1_N; i++)
                if (!fexact(out[i], naive_next(&n, noise_waves[c],
                                               MS1_F, SR)))
                    diff++;
            CHECK(diff == 0);
        }
        static const struct { wave_t w; double slope; } tonal_waves[2] = {
            { WAVE_SINE,     2.0 * OPHIS_PI },
            { WAVE_TRIANGLE, 4.0 },
        };
        for (int c = 0; c < 2; c++) {
            double out[MS1_N];
            ms1_render(tonal_waves[c].w, 50, out);
            naive_t n;
            naive_init(&n, 42, 50);
            double dev = 0.0;
            for (int i = 0; i < MS1_N; i++) {
                double d = fabs(out[i]
                                - naive_next(&n, tonal_waves[c].w,
                                             MS1_F, SR));
                if (d > dev) dev = d;
            }
            CHECK(dev <= tonal_waves[c].slope * MS1_DT / 2.0 + 1e-9);
        }
    }

    /* -- 5. saw DC bias unchanged: mean ~= 0 +/- 1/(2*period) -- */
    {
        double out[MS1_N];
        ms1_render(WAVE_SAW, 50, out);
        double mean = 0.0;
        for (int i = 0; i < MS1_N; i++) mean += out[i];
        mean /= (double)MS1_N;
        CHECK(fabs(mean) <= 1.0 / (2.0 * MS1_PERIOD));
    }

    /* ==== 2x-oversampling contracts =================================== */

    /* -- 6. alias-floor probes: Goertzel
     * ENERGY at two harmonic folds of 220 Hz, compared against the
     * 1x PolyBLEP reference (mirror above). Renders are 44100 samples =
     * exactly 220 periods, so every true harmonic lands on a bin k*220
     * and leaks ~nothing into the non-harmonic probe bins.
     *
     * PRIMARY (ruling option a): folds well above the half-band cutoff
     * (22050), outside its transition band. SAW: the 150th harmonic,
     * 33000 Hz -> 44100 - 33000 = 11100 Hz, |H(33000)| = 0.0157
     * (bc-verified, ~4042x predicted). SQUARE: odd harmonics only, so
     * the 149th, 32780 Hz -> 11320 Hz, |H(32780)| = 0.0177 (~3183x).
     * RED threshold: the 2x path must drop the bin's energy >= 10x vs
     * the 1x reference. Negative controls (verified against temporary
     * naive and 1x builds): the naive
     * wave carries the full harmonic and the 1x path keeps it
     * ~unattenuated — both fail the 10x drop.
     *
     * SECONDARY (kept informational at its floor): the 101st fold
     * (22220 Hz -> 21880 Hz) sits at cutoff+170 Hz, inside the
     * transition band of any decimator reaching the output Nyquist, so
     * its achievable drop is the FIR's passband-edge attenuation:
     * asserted at >= 2x energy (box gave 2.02x, FIR 3.50x; naive
     * ~1.7x and 1x 1.0x still fail it). -- */
    {
        static const double probe_bins[2] = { 11100.0, 11320.0 };
        const double probe101 = 21880.0;    /* 44100 - 101*220 */
        double m2x[2], m1x[2], n2x[2], n1x[2];
        const wave_t waves[2] = { WAVE_SAW, WAVE_SQUARE };
        for (int c = 0; c < 2; c++) {
            render(waves[c], 42, MS1_F);
            m2x[c] = h_goertzel_mag(buf, N, probe_bins[c], SR);
            n2x[c] = h_goertzel_mag(buf, N, probe101, SR);
            naive_t n;
            naive_init(&n, 42, 50);
            for (int i = 0; i < N; i++)
                ref[i] = ms1_ref_next(&n, waves[c], MS1_F, SR);
            m1x[c] = h_goertzel_mag(ref, N, probe_bins[c], SR);
            n1x[c] = h_goertzel_mag(ref, N, probe101, SR);
        }
        /* primary: 150th fold, ruled >= 10x energy */
        CHECK(10.0 * m2x[0] * m2x[0] <= m1x[0] * m1x[0]);   /* saw    */
        CHECK(10.0 * m2x[1] * m2x[1] <= m1x[1] * m1x[1]);   /* square */
        /* secondary: 101st fold, transition-band floor >= 2x energy */
        CHECK(2.0 * n2x[0] * n2x[0] <= n1x[0] * n1x[0]);    /* saw    */
        CHECK(2.0 * n2x[1] * n2x[1] <= n1x[1] * n1x[1]);    /* square */
    }

    HARNESS_REPORT();
}
