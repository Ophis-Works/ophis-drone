/* src/ui/valrepeat.c — the value-key edit + synthesized hold-repeat
 * state machine. Value keys NEVER trigger —
 * no retrig, no TRIG flash; the single trigger path is ACT_TRIGGER.
 * val_edit applies the value and arms the repeat; ui_tick drives
 * valrepeat_tick. No state of its own — the hold lives in ui_t
 * (val_held_dir / val_next_repeat), cleared by keymap_val_release
 * and the focus/device-lost paths in ui.c. */
#include <SDL.h>

#include "ui_internal.h"
#include "seq_items.h"

#define VAL_REPEAT_DELAY_MS 400
#define VAL_REPEAT_RATE_MS  120

void val_edit(ui_t *ui, int dir, Uint32 now)
{
    if (is_osc_tab(ui) && !ui->focus[ui->tab])
        return;                        /* sub-tab row: inert */
    item_t it = content_item(ui, *cur_cursor(ui));
    if (item_dim(ui, it) || it.id == IT_SEQ_STEPSEL)
        return;
    edit_item(ui, it, dir);
    ui->val_held_dir = dir;
    ui->val_next_repeat = now + VAL_REPEAT_DELAY_MS;
}

void valrepeat_tick(ui_t *ui, Uint32 now)
{
    /* value-key hold: pad buttons never auto-repeat, so the repeat is
     * synthesized — value change every VAL_REPEAT_RATE_MS until
     * release. Dimmed/inert items just advance the clock (retry when
     * they become editable). */
    if (ui->val_held_dir && now >= ui->val_next_repeat) {
        val_edit(ui, ui->val_held_dir, now);
        ui->val_next_repeat = now + VAL_REPEAT_RATE_MS;
    }
}
