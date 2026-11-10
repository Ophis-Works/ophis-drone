/* src/preset/schema_tab.c — the preset-format enum tables and their
 * name accessors (M2 split, PLAN_OPTIMIZATION.md O5): the ONE source of
 * the enum strings for BOTH the writer and the parser; the UI reads the
 * display forms through schema.h. */
#include "schema_priv.h"

#include <json.h>

#include <stdio.h>
#include <string.h>
/* ---------------- enum tables (PLAN §9) ---------------- */

const ent_t wave_tab[] = {
    { "sine", WAVE_SINE }, { "tri", WAVE_TRIANGLE }, { "saw", WAVE_SAW },
    { "square", WAVE_SQUARE }, { "white", WAVE_WHITE }, { "pink", WAVE_PINK }
};
/* dest ladder 13 entries (owner order 2026-11-07, PLAN_OSC_NAMING.md
 * R0e): the old "filter" string is RENAMED "flt_cut" (MOD_FILTER, id 4,
 * unchanged semantics); "flt_res" is the NEW MOD_FRES resonance sweep;
 * "flt_env" + "adsr_a/d/s/r" are the PLAN_MOD_FENV_ADSR_DIV.md MS1
 * dests (ids 0..7 frozen, new ids appended — same as MOD_FRES). New
 * format only — old presets carrying "filter" are rewritten by
 * tools/migrate_presets.c. Table order need not match the enum ids. */
const ent_t dest_tab[] = {
    { "off", MOD_OFF }, { "pitch", MOD_PITCH }, { "phase", MOD_PHASE },
    { "amp", MOD_AMP }, { "flt_cut", MOD_FILTER },
    { "flt_res", MOD_FRES }, { "ring", MOD_RING }, { "fm", MOD_FM },
    { "flt_env", MOD_FENV }, { "adsr_a", MOD_ADSR_A },
    { "adsr_d", MOD_ADSR_D }, { "adsr_s", MOD_ADSR_S },
    { "adsr_r", MOD_ADSR_R }
};
const ent_t filt_tab[] = {
    { "lp", FILT_LP }, { "hp", FILT_HP }, { "bp", FILT_BP }, { "br", FILT_BR }
};
/* src ids 1..6 are the historical B/C sources (never renumbered — old
 * presets keep their meaning); 7..9 are the A sources
 * (PLAN_MOD_ROUTING.md M2) */
const ent_t src_tab[] = {
    { "off", 0 }, { "b_osc", 1 }, { "b_sub1", 2 }, { "b_sub2", 3 },
    { "c_osc", 4 }, { "c_sub1", 5 }, { "c_sub2", 6 },
    { "a_osc", 7 }, { "a_sub1", 8 }, { "a_sub2", 9 }
};
const ent_t poles_tab[] = {
    { "12db", 12 }, { "24db", 24 }
};
/* owner-ordered modes amendment (PLAN §9): per-seq mode */
const ent_t seqmode_tab[] = {
    { "fwd", 0 }, { "pingpong", 1 }, { "oddeven", 2 },
    { "random", 3 }, { "markov", 4 }
};

const char *enum_name(const ent_t *tab, int n, int v)
{
    for (int i = 0; i < n; i++)
        if (tab[i].v == v) return tab[i].s;
    return "off"; /* unreachable for valid params; keeps writer total */
}

/* ---- enum name accessors (Phase 5 item 4, moved here from preset/json.c
 * by Phase 7): the PLAN §9 tables above are the single source of the
 * enum names — the UI reads the RAW preset strings through these
 * (schema.h) instead of keeping copies. S1 (PLAN_OPTIMIZATION.md O6):
 * the display transform (UPPERCASE, '_' -> '.') is a UI concern and
 * lives in ui/seq_items.c (ui_enum_display). ---- */

#define ENT_N(tab) ((int)(sizeof (tab) / sizeof (tab)[0]))


const char *schema_wave_name(int v)
{
    return enum_name(wave_tab, ENT_N(wave_tab), v);   /* RAW
                             preset string (S1: the UPPERCASE
                             display form is a UI concern) */
}
const char *schema_dest_name(int v)
{
    return enum_name(dest_tab, ENT_N(dest_tab), v);   /* RAW
                             preset string (S1: the UPPERCASE
                             display form is a UI concern) */
}
const char *schema_xsrc_name(int v)
{
    return enum_name(src_tab, ENT_N(src_tab), v);   /* RAW
                             preset string (S1: the UPPERCASE
                             display form is a UI concern) */
}
const char *schema_filt_name(int v)
{
    return enum_name(filt_tab, ENT_N(filt_tab), v);   /* RAW
                             preset string (S1: the UPPERCASE
                             display form is a UI concern) */
}
const char *schema_poles_name(int v)
{
    return enum_name(poles_tab, ENT_N(poles_tab), v);   /* RAW
                             preset string (S1: the UPPERCASE
                             display form is a UI concern) */
}

