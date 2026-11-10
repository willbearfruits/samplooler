// (a first stand-in: the screen comes next)
#include "pixel.h"
#include "ui.h"
#include <stdio.h>
void scene_draw(const view_t *v)
{
    (void)v;
    ui_head(px_console()->name, px_pos.running);
    char b[32];
    snprintf(b, sizeof b, "%c %d/%d  STEP %d", 'A' + px_pos.pattern, px_pos.slot + 1, px_song.order_n, px_pos.step + 1);
    oled_ftext_ex(F_SMALL, 2, 20, b, 1);
    px_vview_t vv[PX_VOICES_MAX];
    int n = px_console()->view(vv, PX_VOICES_MAX);
    for (int i = 0; i < n; i++) ui_vbar(4 + i * 12, 32, 8, 28, vv[i].kind == VW_OFF ? 0 : vv[i].level);
}
bool scene_splash(int f) { oled_ftext_c(F_MED, 64, 24, "PIXEL", 2); return f < 30; }
void scene_saver(uint32_t f) { oled_ftext_ex(F_SMALL, (int)(f % 90), 30, "PIXEL", 1); }
