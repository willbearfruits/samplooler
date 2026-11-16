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

// the menu as main.c hands it over: the chosen row and three either side
static const struct { const char *name, *val, *group; bool act; float frac; } M[] = {
    { "Mode", "TAPE", "PLAY", 0, 0 }, { "Pitch", "= speed", "PLAY", 0, .5f }, { "Grain", "50 ms", "PLAY", 0, .17f }, { "Snap", "OFF", "PLAY", 0, 0 },
    { "Knob", "JOG", "PLAY", 0, 0 }, { "Hand", "FILTER", "PLAY", 0, .2f }, { "Loop start", "SOUND", "PLAY", 0, 0 },
    { "Tilt", "ON", "TILT", 0, 1 }, { "Speed max", "8.00x", "TILT", 0, 1 }, { "Speed min", "0.05x", "TILT", 0, .05f },
    { "Volume", "100%", "SOUND", 0, .67f }, { "Mic gain", "18 dB", "SOUND", 0, .5f }, { "Input", "MIC", "SOUND", 0, 0 },
    { "Monitor", "WHILE REC", "SOUND", 0, 0 }, { "Speaker", "ON", "SOUND", 0, 1 },
    { "Voice", "NATURAL TUNE", "VOICE", 0, .3f }, { "Key", "C", "VOICE", 0, 0 }, { "Scale", "HARM MINOR", "VOICE", 0, 1 },
    { "Slot", "3 7.3s", "SLOTS", 0, .3f }, { "Save", "-> slot 3", "SLOTS", 1, -1 }, { "Load", "<- slot 3", "SLOTS", 1, -1 },
    { "Link", "ON", "LINK", 0, 1 }, { "Group", "1", "LINK", 0, 0 }, { "Range", "ROOM", "LINK", 0, .5f }, { "Who is there", "2 here", "LINK", 1, -1 },
    { "Rec button", "ON", "SYSTEM", 0, 1 }, { "Board check", "look", "SYSTEM", 1, -1 },
};
#define MN ((int)(sizeof M / sizeof M[0]))
static void menu_at(ui_t *u, int sel)
{
    u->menu = true; u->menu_sel = sel; u->menu_n = MN; u->menu_group = M[sel].group; u->param = sel; u->param_n = MN;
    u->param_name = M[sel].name; u->param_val = M[sel].val; u->param_frac = M[sel].frac;
    for (int r = 0; r < UI_ROWS; r++) {
        int row = sel - UI_ROWS / 2 + r;
        if (row < 0 || row >= MN) { u->row_name[r] = NULL; continue; }
        u->row_name[r] = M[row].name; u->row_act[r] = M[row].act; snprintf(u->row_val[r], UI_VAL_N, "%s", M[row].val);
    }
}
int main(void) {
    oled_init(NULL);
    ui_t u = { .state = REC_IDLE, .mode = REC_MODE_TAPE, .spd_max = 8, .pitch_ratio = 1, .has_imu = true, .tilt_on = true,
               .max_s = 20, .meter_in = 0.55f, .meter_out = 0.4f, .cpu = 9, .param_n = MN, .param = 4, .param_name = "Speed max", .param_val = "8.00x", .param_frac = 1.0f,
               .has_knob = true, .has_hand = true, .knob_mode = UI_KNOB_JOG, .hand_mode = UI_HAND_FILTER, .version = "v0.2.0" };
    frames(&u, 10); dump("01_splash_walk");
    frames(&u, 10); dump("02_splash_throw");
    frames(&u, 10); dump("03_splash_rings");
    frames(&u, 14); dump("03b_splash_title");
    frames(&u, 62); dump("04_idle"); u.voice = "HARD TUNE"; frames(&u, 2); dump("04b_idle_voice"); u.voice = NULL;
    frames(&u, 2);  dump("05_idle_gag");
    u.state = REC_RECORDING; u.waiting = true; u.rec_fill = 0; frames(&u, 6); dump("06_rec_listening");
    u.waiting = false; u.rec_fill = 0.35f; u.meter_in = 0.8f;
    frames(&u, 1); dump("06b_rec_flash"); frames(&u, 5); dump("07_recording");
    u.closing = true; u.peers = 1; frames(&u, 2); dump("07b_rec_to_the_beat"); u.closing = false; u.peers = 0;
    u.rec_fill = 0.92f; u.clip = true; frames(&u, 4); dump("08_rec_full_clip"); u.clip = false;
    u.state = REC_PLAYING; u.loop_s = 7.3f; u.pos = 0.42f; u.speed_now = 1.0f; u.speed_cmd = 1.0f; u.tilt_pitch = 8; u.tilt_roll = 0; u.meter_in = 0.5f;
    frames(&u, 12); dump("09_play_forward");
    u.voice = "NATURAL TUNE"; frames(&u, 2); dump("09b_play_voice"); u.voice = NULL;
    u.peers = 2; u.beat_flash = 3; frames(&u, 1); dump("09c_play_linked_beat"); u.beat_flash = 0; frames(&u, 2); dump("09d_play_linked"); u.peers = 0;
    u.speed_now = -1.6f; u.speed_cmd = -1.6f; u.tilt_pitch = -25; u.pos = 0.8f; frames(&u, 14); dump("10_play_reverse");
    u.speed_now = 2.3f; u.speed_cmd = 2.4f; u.tilt_pitch = 18; u.tilt_roll = -30; u.fx_delay = true; u.pos = 0.15f; frames(&u, 10); dump("11_delay_ghosts");
    u.fx_delay = false; u.tilt_roll = 35; u.fx_stutter = true; frames(&u, 5); dump("12_stutter");
    u.fx_stutter = false; u.tilt_roll = 2;
    u.frozen = true; u.speed_now = -1.6f; u.speed_cmd = -1.6f; u.tilt_pitch = -25; frames(&u, 6); dump("12b_frozen"); u.frozen = false;
    u.mode = REC_MODE_GRAIN; u.pitch_ratio = 1.5f; u.speed_now = 0.9f; u.speed_cmd = 0.9f; frames(&u, 8); dump("13_grain_juggle");
    u.mode = REC_MODE_TAPE; u.pitch_ratio = 1;
    u.speed_now = 8.0f; u.speed_cmd = 8.0f; u.tilt_pitch = 44; frames(&u, 6); dump("14_fast_tumble");
    u.speed_now = 0.03f; u.speed_cmd = 0.03f; u.tilt_pitch = 1; frames(&u, 30); dump("15_crawl");
    u.speed_now = 1.0f; u.speed_cmd = 1.0f; u.tilt_pitch = 8; u.clip = true; frames(&u, 3); dump("16_clip"); u.clip = false;
    // rev E: the hand, the left knob
    u.hand_seen = true; u.hand = 0.3f; u.filter = -0.3f; u.hand_mm = 190; frames(&u, 10); dump("16b_hand_coming");
    u.hand = 1.0f; u.filter = -1.0f; u.hand_mm = 40; frames(&u, 10); dump("16c_hand_down");
    u.hand_mode = UI_HAND_BRAKE; u.speed_now = 0.2f; u.hand = 0.8f; u.filter = 0; frames(&u, 8); dump("16d_hand_brake");
    u.hand_mode = UI_HAND_FILTER; u.hand_seen = false; u.hand = 0; u.speed_now = 1.0f; frames(&u, 12);
    u.jog = 3.2f; frames(&u, 4); dump("16e_jog_forward"); u.jog = -2.6f; frames(&u, 6); dump("16f_jog_back"); u.jog = 0;
    u.knob_mode = UI_KNOB_FILTER; u.filter = 0.5f; frames(&u, 4); dump("16g_knob_filter_high"); u.filter = 0; u.knob_mode = UI_KNOB_SPEED;
    u.knob_st = 7; u.speed_now = 1.5f; frames(&u, 4); dump("16h_knob_speed"); u.knob_st = 0; u.speed_now = 1.0f; u.knob_mode = UI_KNOB_JOG;
    u.toast_ms = 900; u.param = 0; u.param_name = "Mic gain"; u.param_val = "18 dB"; u.param_frac = 0.5f;
    frames(&u, 2); dump("17_toast");
    u.toast_ms = 900; u.param_name = "Filter"; u.param_val = "LOW 9"; u.param_frac = 0.31f; frames(&u, 2); dump("17b_toast_knob");
    u.toast_ms = 0; menu_at(&u, 8);
    frames(&u, 2); dump("18_menu_sliding"); frames(&u, 12); dump("19_menu_list");
    menu_at(&u, 9); frames(&u, 1); dump("19a_menu_gliding"); frames(&u, 10);
    menu_at(&u, 8); frames(&u, 10); u.menu_edit = true; frames(&u, 1); dump("19b_menu_edit_opening"); frames(&u, 8); dump("19c_menu_edit"); u.menu_edit = false; frames(&u, 8);
    menu_at(&u, 15); frames(&u, 12); u.menu_edit = true; frames(&u, 10); dump("19d_menu_edit_word"); u.menu_edit = false; frames(&u, 8);
    menu_at(&u, 5); frames(&u, 12); u.menu_edit = true; frames(&u, 10); dump("19e_menu_edit_hand"); u.menu_edit = false; frames(&u, 8);
    menu_at(&u, 19); frames(&u, 12); dump("20_menu_save");
    menu_at(&u, 0); frames(&u, 12); dump("20b_menu_top"); menu_at(&u, MN - 1); frames(&u, 12); dump("20c_menu_bottom");
    u.state = REC_RECORDING; u.rec_fill = 0.2f; frames(&u, 6); dump("20d_menu_while_rec"); u.state = REC_PLAYING;
    u.menu = false; frames(&u, 12);
    u.busy_text = "SAVING 3"; u.busy_frac = 0.6f; frames(&u, 12); dump("21_saving");
    u.busy_text = "LOADING 5"; u.busy_frac = 0.3f; frames(&u, 3); dump("22_loading");
    u.busy_text = NULL; u.note = "SAVED 3"; frames(&u, 3); dump("23_note_saved");
    u.note = "WRECK 72B1, MANTRA 00D0, HUFF FBD4"; frames(&u, 2); dump("23b_note_who");
    u.state = REC_IDLE; u.note = "CRASH: slot save"; frames(&u, 3); dump("24_note_crash");
    u.note = "WATCHDOG"; frames(&u, 5); dump("25_note_watchdog");
    u.note = "BROWNOUT"; frames(&u, 2); dump("26_note_brownout");
    u.note = "SLOT 4 EMPTY"; frames(&u, 2); dump("27_note_empty");
    u.note = NULL; u.idle_ms = 200000; frames(&u, 25); dump("28_screensaver");
    u.idle_ms = 0; u.state = REC_PLAYING; u.still_ms = 700000; u.speed_now = -1.3f; frames(&u, 9); dump("28b_saver_playing"); u.still_ms = 0;
    u.check = true; u.chk_enc = 3; u.chk_knob = -12; u.chk_rec = true; u.hand_mm = 132; u.peers = 1; frames(&u, 2); dump("28c_board_check");
    u.has_hand = false; u.has_knob = false; u.has_joy = true; u.has_imu = false; u.chk_rec = false; frames(&u, 2); dump("28d_board_check_rev_c"); u.check = false;
    ui_fault("NO PSRAM", "for the take"); dump("29_fault");
    return 0;
}
