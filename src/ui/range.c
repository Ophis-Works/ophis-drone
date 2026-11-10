/* src/ui/range.c — pure ladder conversions. The 1/12-octave
 * frequency ladder (pos_to_hz/hz_to_pos) is DELETED:
 * the frequency rows are integer-Hz odometer boxes now, helpers in
 * seq_items.c); only the ADSR seconds ladder remains. */
#include "range.h"
/* clampi comes from dsp/types.h via ui.h consumers; keep this TU
 * dependency-light: inline the two clamp bounds locally */
#include "../dsp/types.h"

#include <math.h>

double adsr_pos_to_s(int pos)   /* 0..50 */
{
    if (pos <= 0) return 0.0;
    return 0.001 * pow(60000.0, (double)(pos - 1) / 49.0);
}

int adsr_s_to_pos(double t)     /* -> 0..50 */
{
    int p;
    if (t <= 0.0) return 0;
    p = 1 + (int)lround(49.0 * log10(t / 0.001) / log10(60000.0));
    return clampi(p, 1, 50);
}
