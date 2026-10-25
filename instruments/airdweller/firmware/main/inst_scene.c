// PLACEHOLDER: keeps the build going until the screens arrive.
#include "inst_ad.h"
#include "ui.h"
#include <stdio.h>
void scene_hit(int pad, int vel, int remote) { (void)pad; (void)vel; (void)remote; }
bool scene_splash(int frame) { oled_ftext_c(F_MED, 64, 28, "AIRDWELLER", 0); return frame < 25; }
void scene_saver(uint32_t frame) { oled_ftext_c(F_SMALL, 64 + (int)(frame % 40) - 20, 30, "AIRDWELLER", 0); }
void scene_draw(const view_t *v)
{
    (void)v;
    static const char *const rl[] = { "DRUM", "PAINT", "PUMP" };
    ui_head(rl[D.role % 3], D.cut);
    char b[48];
    snprintf(b, sizeof b, "%s %s %.0f BPM", D.hand == HAND_LEFT ? "LEFT" : D.hand == HAND_RIGHT ? "RIGHT" : "ONE", D.paired ? "PAIRED" : "", (double)D.bpm);
    oled_ftext(F_SMALL, 2, 20, b);
    int x = 2;
    for (int p = 0; p < NPAD; p++) x = oled_ftext(F_TINY, x, 32, dv_name[D.pad_voice[p]]) + 4;
}
