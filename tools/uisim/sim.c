// Drives ui_draw() through every state and dumps frames as PBM for render.py's contact sheet.
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
static void frames(ui_t *u, int n) { for (int i = 0; i < n; i++) ui_draw(u); }
int main(void) {
    oled_init(NULL);
    ui_t u = { .state = REC_IDLE, .mode = REC_MODE_TAPE, .spd_max = 8, .pitch_ratio = 1, .has_imu = true, .tilt_on = true, .has_joy = true,
               .max_s = 20, .meter_in = 0.55f, .meter_out = 0.4f, .cpu = 9, .param_n = 23, .param = 4, .param_name = "Speed max", .param_val = "8.00x", .param_frac = 1.0f };
    frames(&u, 12); dump("01_splash_walk");
    frames(&u, 13); dump("02_splash_trip");
    frames(&u, 16); dump("03_splash_title");
    frames(&u, 60); dump("04_idle"); u.voice = "HARD TUNE"; frames(&u, 2); dump("04b_idle_voice"); u.voice = NULL;
    frames(&u, 2);  dump("05_idle_gag");
    u.state = REC_RECORDING; u.rec_fill = 0.35f; u.meter_in = 0.8f;
    frames(&u, 1); dump("06_rec_flash"); frames(&u, 5); dump("07_recording");
    u.closing = true; u.peers = 1; frames(&u, 2); dump("07b_rec_to_the_beat"); u.closing = false; u.peers = 0;
    u.rec_fill = 0.92f; u.clip = true; frames(&u, 4); dump("08_rec_full_clip"); u.clip = false;
    u.state = REC_PLAYING; u.loop_s = 7.3f; u.pos = 0.42f; u.speed_now = 1.0f; u.speed_cmd = 1.0f; u.tilt_pitch = 8; u.tilt_roll = 0; u.meter_in = 0.5f;
    frames(&u, 12); dump("09_play_forward");
    u.voice = "NATURAL TUNE"; frames(&u, 2); dump("09b_play_voice"); u.voice = NULL;
    u.peers = 2; u.beat_flash = 3; frames(&u, 1); dump("09c_play_linked_beat"); u.beat_flash = 0; frames(&u, 2); dump("09d_play_linked"); u.peers = 0;
    u.speed_now = -1.6f; u.speed_cmd = -1.6f; u.tilt_pitch = -25; frames(&u, 14); dump("10_play_reverse");
    u.speed_now = 2.3f; u.speed_cmd = 2.4f; u.tilt_pitch = 18; u.tilt_roll = -30; u.fx_delay = true; frames(&u, 10); dump("11_delay_ghosts");
    u.fx_delay = false; u.tilt_roll = 35; u.fx_stutter = true; frames(&u, 5); dump("12_stutter");
    u.fx_stutter = false; u.tilt_roll = 2;
    u.frozen = true; u.speed_now = -1.6f; u.speed_cmd = -1.6f; u.tilt_pitch = -25; frames(&u, 6); dump("12b_frozen"); u.frozen = false;
    u.mode = REC_MODE_GRAIN; u.pitch_ratio = 1.5f; u.speed_now = 0.9f; u.speed_cmd = 0.9f; frames(&u, 8); dump("13_grain_juggle");
    u.mode = REC_MODE_TAPE; u.pitch_ratio = 1;
    u.speed_now = 8.0f; u.speed_cmd = 8.0f; u.tilt_pitch = 44; frames(&u, 6); dump("14_fast_tumble");
    u.speed_now = 0.03f; u.speed_cmd = 0.03f; u.tilt_pitch = 1; frames(&u, 30); dump("15_crawl");
    u.speed_now = 1.0f; u.speed_cmd = 1.0f; u.tilt_pitch = 8; u.clip = true; frames(&u, 3); dump("16_clip");
    u.clip = false; u.toast_ms = 900; u.param = 0; u.param_name = "Mic gain"; u.param_val = "18 dB"; u.param_frac = 0.5f;
    frames(&u, 2); dump("17_toast");
    u.toast_ms = 0; u.menu = true; u.param = 4; u.param_name = "Speed max"; u.param_val = "8.00x"; u.param_frac = 1.0f;
    frames(&u, 2); dump("18_menu_sliding"); frames(&u, 10); dump("19_menu_list");
    u.menu_edit = true; frames(&u, 1); dump("19b_menu_edit"); u.menu_edit = false;
    u.param = 11; u.param_name = "Save"; u.param_val = "-> slot 3"; u.param_frac = -1; frames(&u, 1); dump("20_menu_save");
    u.menu = false; frames(&u, 10);
    u.busy_text = "SAVING 3"; u.busy_frac = 0.6f; frames(&u, 12); dump("21_saving");
    u.busy_text = "LOADING 5"; u.busy_frac = 0.3f; frames(&u, 3); dump("22_loading");
    u.busy_text = NULL; u.note = "SAVED 3"; frames(&u, 3); dump("23_note_saved");
    u.state = REC_IDLE; u.note = "CRASH: slot save"; frames(&u, 3); dump("24_note_crash");
    u.note = "WATCHDOG"; frames(&u, 5); dump("25_note_watchdog");
    u.note = "BROWNOUT"; frames(&u, 2); dump("26_note_brownout");
    u.note = "SLOT 4 EMPTY"; frames(&u, 2); dump("27_note_empty");
    u.note = NULL; u.idle_ms = 200000; frames(&u, 25); dump("28_screensaver");
    u.idle_ms = 0;
    ui_fault("NO PSRAM", "for the take"); dump("29_fault");
    return 0;
}
