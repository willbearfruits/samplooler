#include "ui.h"
#include "oled.h"
#include "recorder.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void dump(const char *name) {
    const char *d = getenv("OUT"); char fn[128]; snprintf(fn, sizeof fn, "%s/%s.pbm", d ? d : "out", name);
    FILE *f = fopen(fn, "w"); fprintf(f, "P1\n%d %d\n", OLED_W, OLED_H);
    for (int y = 0; y < OLED_H; y++) { for (int x = 0; x < OLED_W; x++) fputc(oled_get(x, y) ? '1' : '0', f); fputc('\n', f); }
    fclose(f);
}
int main(void) {
    oled_init(NULL);
    ui_t u = { .state = REC_IDLE, .mode = REC_MODE_TAPE, .spd_max = 8, .pitch_ratio = 1, .has_imu = true, .tilt_on = true, .has_joy = true,
               .max_s = 20, .meter_in = 0.55f, .meter_out = 0.4f, .cpu = 9, .param_n = 13, .param = 4, .param_name = "Speed max", .param_val = "8.00x", .param_frac = 1.0f };
    for (int i = 0; i < 26; i++) ui_draw(&u);            // the wipe is done at 22 frames
    dump("1_splash");
    for (int i = 0; i < 18; i++) ui_draw(&u);
    dump("2_idle");
    u.state = REC_RECORDING; u.rec_fill = 0.35f; u.meter_in = 0.8f;
    ui_draw(&u); dump("3_rec_flash"); for (int i = 0; i < 5; i++) ui_draw(&u); dump("4_recording");
    u.state = REC_PLAYING; u.loop_s = 7.3f; u.pos = 0.42f; u.speed_now = 2.3f; u.speed_cmd = 2.4f; u.tilt_pitch = 18; u.tilt_roll = -30; u.fx_delay = true; u.meter_in = 0.5f;
    for (int i = 0; i < 6; i++) ui_draw(&u);
    dump("5_play_delay");
#if !CONFIG_IDF_TARGET_ESP32C3
    u.mode = REC_MODE_GRAIN; u.pitch_ratio = 1.5f;                                 // TAPE only on the C3
#endif
    u.speed_now = -1.2f; u.speed_cmd = -1.3f; u.tilt_pitch = -20; u.tilt_roll = 35; u.fx_delay = false; u.fx_stutter = true; u.pos = 0.8f;
    for (int i = 0; i < 5; i++) ui_draw(&u);
    dump("6_grain_stutter");
    u.fx_stutter = false; u.tilt_roll = 2; u.tilt_pitch = 5; u.speed_now = 8.0f; u.speed_cmd = 8.0f; u.clip = true;
    for (int i = 0; i < 3; i++) ui_draw(&u);
    dump("7_fast_clip");
    u.clip = false; u.speed_now = 1.0f; u.toast_ms = 900; u.param = 0; u.param_name = "Mic gain"; u.param_val = "18 dB"; u.param_frac = 0.5f;
    for (int i = 0; i < 2; i++) ui_draw(&u);
    dump("8_toast");
    u.toast_ms = 0; u.menu = true; u.param = 4; u.param_name = "Speed max"; u.param_val = "8.00x"; u.param_frac = 1.0f;
    for (int i = 0; i < 2; i++) ui_draw(&u);
    dump("9_menu_sliding"); for (int i = 0; i < 10; i++) ui_draw(&u); dump("9_menu_open");
    u.param = 11; u.param_name = "Save"; u.param_val = "-> slot 3"; u.param_frac = -1;
    ui_draw(&u); dump("9_menu_save");
    u.menu = false; u.busy_text = "SAVING 3"; u.busy_frac = 0.6f;
    for (int i = 0; i < 12; i++) ui_draw(&u);
    dump("a_saving");
    u.busy_text = NULL; u.note = "SAVED 3";
    ui_draw(&u); dump("b_note_saved");
    u.state = REC_IDLE; u.note = "CRASH: slot save";
    ui_draw(&u); dump("c_note_crash");
    ui_fault("NO PSRAM", "for the take");
    dump("d_fault");
    return 0;
}
