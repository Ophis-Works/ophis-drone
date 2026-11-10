/* src/dsp/subosc.c — sub-oscillator: an AUDIO voice at parent_f / div
 * (integer subharmonic divider). Pure,
 * deterministic. No SDL/ALSA. */
#include "subosc.h"

double subosc_out(subosc_state_t *s, const subosc_params_t *p,
                  double main_freq_hz, double sr)
{
    int div = (p->div >= 1 && p->div <= 16) ? p->div : 1;
    double f = main_freq_hz / (double)div;
    s->o.duty = p->duty;   /* square pulse width */
    return osc_next(&s->o, f, sr);
}
