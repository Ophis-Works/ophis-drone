/* src/ui/range.h — pure ladder conversions. The frequency
 * ladder (pos_to_hz/hz_to_pos) is gone — the freq
 * rows are integer-Hz odometer boxes, helpers in seq_items.h; the ADSR
 * seconds ladder stays, SDL-free and unit-testable (tests/test_range.c). */
#ifndef OPHIS_RANGE_H
#define OPHIS_RANGE_H

/* ADSR seconds ladder: pos 0 = 0 s; 1..50 = 0.001*60000^((pos-1)/49) */
double adsr_pos_to_s(int pos);
int    adsr_s_to_pos(double t);

#endif /* OPHIS_RANGE_H */
