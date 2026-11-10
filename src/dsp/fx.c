/* src/dsp/fx.c — master-path FX + safety + master level + gate.
 *
 * Per sample: delay -> reverb -> safety -> master level -> gate.
 *   delay : stereo ring buffers (fixed 2 s @44100), feedback loop with a
 *           one-pole tone LP inside; the toned tail is both the wet output
 *           and what feeds back, so TONE shapes echoes at any FEEDBACK.
 *   reverb: Freeverb-class comb+allpass bank (8 combs + 4 allpasses per
 *           channel), damping one-pole LP inside the comb loop,
 *           SIZE -> comb feedback (tail length), WIDTH -> mid/side spread
 *           of the wet pair. Wet is ADDITIVE with the dry path
 *           (out = dry + mix*delaywet + mix*reverbwet).
 *   safety: two cascaded RBJ biquads each for HP 60 Hz (Q 0.707) and
 *           LP 18 kHz (Q 0.707) = 4th-order rolloff; safety = 0 bypasses.
 *   then  : master level (percent/100), then the transport gate.
 *
 * Pure, deterministic; fixed preallocated buffers only (no malloc in the
 * RT path). PANIC resets via fx_reset. */
#include <math.h>
#include <string.h>

#include "fx.h"

#define FX_PI 3.14159265358979323846

/* Freeverb tunings (sample-fixed, 44.1 kHz class) */
static const int COMB_TUNING[8] = { 1116, 1188, 1277, 1356,
                                    1422, 1491, 1557, 1617 };
static const int AP_TUNING[4]   = { 556, 441, 341, 225 };
#define FX_STEREO_SPREAD 23

/* wet gain of the full reverb bank at mix 100 */
#define FX_WET_GAIN 0.3f
/* input scale into the comb bank */
#define FX_IN_SCALE 0.5f
#define FX_AP_GAIN  0.5f

void fx_reset(fx_state_t *fx)
{
    memset(fx, 0, sizeof *fx);
    for (int i = 0; i < 8; i++) {
        fx->combL[i].len = COMB_TUNING[i];
        fx->combR[i].len = COMB_TUNING[i] + FX_STEREO_SPREAD;
    }
    for (int i = 0; i < 4; i++) {
        fx->apL[i].len = AP_TUNING[i];
        fx->apR[i].len = AP_TUNING[i] + FX_STEREO_SPREAD;
    }
    fx->last_tone  = -1;      /* force coefficient computation */
    fx->last_size  = -1;
    fx->last_damp  = -1;
    fx->safety_dirty = 1;
}

static void update_tone_coef(fx_state_t *fx, int tone, double sr)
{
    if (tone == fx->last_tone) return;
    float fc = (float)tone;
    if (fc < 20.0f)   fc = 20.0f;
    if (fc > 20000.0f) fc = 20000.0f;
    /* one-pole LP: y += a*(x - y), a = 1 - exp(-2*pi*fc/sr) */
    fx->tone_a = (float)(1.0 - exp(-2.0 * FX_PI * (double)fc / sr));
    fx->last_tone = tone;
}

static void update_reverb_coefs(fx_state_t *fx, int size, int damp)
{
    if (size == fx->last_size && damp == fx->last_damp) return;
    float s = (float)size / 100.0f;
    float d = (float)damp / 100.0f;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    fx->rv_fb    = s * 0.98f;         /* SIZE -> tail length */
    fx->rv_damp1 = d * 0.4f;          /* DAMP -> LP in the comb loop */
    fx->last_size = size;
    fx->last_damp = damp;
}

static void update_safety(fx_state_t *fx, double sr)
{
    if (!fx->safety_dirty) return;
    /* 4th-order: two cascaded RBJ biquads per band, Q 0.707 */
    biquad_cascade_set(&fx->hp1, &fx->hp2, FILT_HP, 60.0f,  sr, 0.7071f);
    biquad_cascade_set(&fx->lp1, &fx->lp2, FILT_LP, 18000.0f, sr, 0.7071f);
    fx->safety_dirty = 0;
}

/* one Freeverb comb: the damping one-pole LP smooths the buffer output
 * and the SMOOTHED value is what recirculates — loop gain = fb x LP
 * <= 0.98 < 1 for EVERY size/damp combination (stability regression:
 * the test_fx grid; the shipped presets crashed the sound system with
 * the old structure that recirculated y unattenuated) */
static float comb_next(fx_comb_t *c, float in, float fb, float damp1)
{
    float y = c->buf[c->idx];
    c->store = y * (1.0f - damp1) + c->store * damp1;  /* damping LP */
    c->buf[c->idx] = in + c->store * fb;
    if (++c->idx >= c->len) c->idx = 0;
    return y;
}

/* one Schroeder allpass */
static float ap_next(fx_ap_t *a, float in)
{
    float bufout = a->buf[a->idx];
    float out = -in + bufout;
    a->buf[a->idx] = in + bufout * FX_AP_GAIN;
    if (++a->idx >= a->len) a->idx = 0;
    return out;
}

static float reverb_bank(fx_state_t *fx, float in,
                         fx_comb_t *comb, fx_ap_t *ap)
{
    float x = in * FX_IN_SCALE;
    float acc = 0.0f;
    for (int i = 0; i < 8; i++)
        acc += comb_next(&comb[i], x, fx->rv_fb, fx->rv_damp1);
    for (int i = 0; i < 4; i++)
        acc = ap_next(&ap[i], acc);
    return acc * FX_WET_GAIN;
}

void fx_process(fx_state_t *fx, const fx_params_t *p,
                float in_l, float in_r, float *out_l, float *out_r,
                double sr)
{
    update_tone_coef(fx, p->delay.tone, sr);
    update_reverb_coefs(fx, p->reverb.size, p->reverb.damp);

    /* --- 1. delay --- */
    int tl = clampi((int)(p->delay.time_l * (float)sr + 0.5f),
                    0, FX_MAX_DELAY_SAMPLES - 1);
    int tr = clampi((int)(p->delay.time_r * (float)sr + 0.5f),
                    0, FX_MAX_DELAY_SAMPLES - 1);
    /* ring read: the value (wpos - t) ago; t = 0 -> the current input */
    float tailL = (tl > 0)
        ? fx->dl[(fx->wpos - tl + FX_MAX_DELAY_SAMPLES) % FX_MAX_DELAY_SAMPLES]
        : in_l;
    float tailR = (tr > 0)
        ? fx->dr[(fx->wpos - tr + FX_MAX_DELAY_SAMPLES) % FX_MAX_DELAY_SAMPLES]
        : in_r;

    /* one-pole tone LP inside the loop: the toned tail is the wet
     * signal AND what feeds back */
    fx->tone_z1_l += fx->tone_a * (tailL - fx->tone_z1_l);
    fx->tone_z1_r += fx->tone_a * (tailR - fx->tone_z1_r);
    float fb = (float)p->delay.feedback / 100.0f;
    if (fb < 0.0f)  fb = 0.0f;
    if (fb > 0.99f) fb = 0.99f;
    fx->dl[fx->wpos] = in_l + fb * fx->tone_z1_l;
    fx->dr[fx->wpos] = in_r + fb * fx->tone_z1_r;
    fx->wpos++;
    if (fx->wpos >= FX_MAX_DELAY_SAMPLES) fx->wpos = 0;

    /* delay mix: linear wet/dry crossfade (mix 100 = full wet) */
    float mixd = (float)p->delay.mix / 100.0f;
    float sd_l = in_l * (1.0f - mixd) + fx->tone_z1_l * mixd;
    float sd_r = in_r * (1.0f - mixd) + fx->tone_z1_r * mixd;

    /* --- 2. reverb: WIDTH = mid/side spread of the wet pair (docs:
     * "width -> mid/side"); mix is a linear wet/dry crossfade --- */
    float wet_l = reverb_bank(fx, sd_l, fx->combL, fx->apL);
    float wet_r = reverb_bank(fx, sd_r, fx->combR, fx->apR);
    float mid  = 0.5f * (wet_l + wet_r);
    float side = 0.5f * (wet_l - wet_r);
    float wamt = (float)p->reverb.width / 100.0f;       /* 0..1 */
    float xl = mid + wamt * side;
    float xr = mid - wamt * side;
    float mixr = (float)p->reverb.mix / 100.0f;
    float s_l = sd_l * (1.0f - mixr) + xl * mixr;
    float s_r = sd_r * (1.0f - mixr) + xr * mixr;

    /* --- 3. safety filter (or bypass) --- */
    if (p->safety) {
        update_safety(fx, sr);
        s_l = (float)biquad_next(&fx->lp2,
                 biquad_next(&fx->lp1,
                   biquad_next(&fx->hp2,
                     biquad_next(&fx->hp1, (double)s_l))));
        s_r = (float)biquad_next(&fx->lp2,
                 biquad_next(&fx->lp1,
                   biquad_next(&fx->hp2,
                     biquad_next(&fx->hp1, (double)s_r))));
    }

    /* --- 4. master level, 5. gate (gate AFTER safety) --- */
    float g = (float)p->master / 100.0f * p->gate;
    *out_l = s_l * g;
    *out_r = s_r * g;
}
