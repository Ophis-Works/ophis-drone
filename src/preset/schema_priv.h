/* src/preset/schema_priv.h — PRIVATE interface between the preset
 * schema TUs (M2 split, PLAN_OPTIMIZATION.md O5). Nothing here is used
 * outside src/preset/. */
#ifndef OPHIS_SCHEMA_PRIV_H
#define OPHIS_SCHEMA_PRIV_H

#include "schema.h"

#include <json.h>

typedef struct { const char *s; int v; } ent_t;
#define ENT_N(tab) ((int)(sizeof (tab) / sizeof (tab)[0]))

extern const ent_t wave_tab[], dest_tab[], filt_tab[], src_tab[],
                   poles_tab[], seqmode_tab[];
const char *enum_name(const ent_t *tab, int n, int v);   /* RAW string */

#endif /* OPHIS_SCHEMA_PRIV_H */
