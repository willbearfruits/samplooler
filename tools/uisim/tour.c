// A tour of the screen for the README and the project page: drives ui_draw() through a short session at 30 frames
// a second, as the board draws it (the boot, a take that waits for its first sound, the loop, the tilt, reverse,
// delay, stutter, grains, the hand coming down, the left knob dragging the tape, the menu, flung at 8x, the crawl)
// and dumps every frame as PBM into $OUT (default out/tour). tour.sh makes the video.
#include "ui.h"
#include "oled.h"
#include "recorder.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FPS 30
static int n;
static void dump(void)
{
    const char *d = getenv("OUT"); char fn[160]; snprintf(fn, sizeof fn, "%s/%04d.pbm", d ? d : "out/tour", n++);
    FILE *f = fopen(fn, "w"); fprintf(f, "P1\n%d %d\n", OLED_W, OLED_H);
    for (int y = 0; y < OLED_H; y++) { for (int x = 0; x < OLED_W; x++) fputc(oled_get(x, y) ? '1' : '0', f); fputc('\n', f); }
    fclose(f);
}
static ui_t u;
static float target_speed, target_pitch, target_roll, target_hand, target_jog;
// one frame: ease the speed, the tilt, the hand and the knob's push toward their targets like a hand would, run the playhead, draw
static void frame(void)
{
    const float k = 20.0f / FPS;
    u.now_ms = 1000u + (uint32_t)n * 1000u / FPS;
    u.speed_cmd = target_speed;
    u.speed_now += (target_speed - u.speed_now) * 0.15f * k;
    u.tilt_pitch += (target_pitch - u.tilt_pitch) * 0.2f * k;
    u.tilt_roll += (target_roll - u.tilt_roll) * 0.2f * k;
    u.hand += (target_hand - u.hand) * 0.18f * k; u.hand_seen = target_hand > 0.01f || u.hand > 0.03f;
    if (u.hand_mode == UI_HAND_FILTER) u.filter = -u.hand;
    u.jog += (target_jog - u.jog) * 0.3f * k;
    if (u.state == REC_PLAYING) { u.pos += (u.speed_now + u.jog) / (u.loop_s * FPS); u.pos -= floorf(u.pos); }
    float t = (float)n / FPS;
    u.meter_in = 0.35f + 0.25f * fabsf(sinf(t * 2.3f)) * (0.6f + 0.4f * sinf(t * 0.7f));
    u.meter_out = u.state == REC_PLAYING ? 0.3f + 0.25f * fabsf(sinf(t * 3.1f + 1.0f)) : 0.05f;
    ui_draw(&u);
    dump();
}
static void hold(float s) { for (int i = 0; i < (int)(s * FPS); i++) frame(); }
static void go(float speed, float pitch, float roll, float s) { target_speed = speed; target_pitch = pitch; target_roll = roll; hold(s); }

// the menu, as main.c hands it over
static const struct { const char *name, *val, *group; float frac; } M[] = {
    { "Mode", "TAPE", "PLAY", 0 }, { "Pitch", "= speed", "PLAY", .5f }, { "Grain", "50 ms", "PLAY", .17f }, { "Snap", "OFF", "PLAY", 0 },
    { "Knob", "JOG", "PLAY", 0 }, { "Hand", "FILTER", "PLAY", .2f }, { "Loop start", "SOUND", "PLAY", 0 },
    { "Tilt", "ON", "TILT", 1 }, { "Speed max", "2.00x", "TILT", .23f }, { "Speed min", "0.05x", "TILT", .05f },
    { "Volume", "100%", "SOUND", .67f }, { "Mic gain", "12 dB", "SOUND", .33f },
};
#define MN ((int)(sizeof M / sizeof M[0]))
static char spd[12] = "2.00x";
static void menu_at(int sel)
{
    u.menu = true; u.menu_sel = sel; u.menu_n = 27; u.menu_group = M[sel].group;
    u.param_name = M[sel].name; u.param_val = sel == 8 ? spd : M[sel].val; u.param_frac = M[sel].frac;
    for (int r = 0; r < UI_ROWS; r++) {
        int row = sel - UI_ROWS / 2 + r;
        if (row < 0 || row >= MN) { u.row_name[r] = NULL; continue; }
        u.row_name[r] = M[row].name; u.row_act[r] = false; snprintf(u.row_val[r], UI_VAL_N, "%s", row == 8 ? spd : M[row].val);
    }
}
int main(void)
{
    oled_init(NULL);
    u = (ui_t){ .state = REC_IDLE, .mode = REC_MODE_TAPE, .spd_max = 8, .pitch_ratio = 1, .has_imu = true, .tilt_on = true,
                .max_s = 20, .cpu = 21, .has_knob = true, .has_hand = true, .knob_mode = UI_KNOB_JOG, .hand_mode = UI_HAND_FILTER };
    hold(2.3f + 1.6f);                                                // the boot, then a moment of READY
    u.state = REC_RECORDING; u.waiting = true; hold(1.0f);            // hold REC: it listens
    u.waiting = false;                                                // the first sound: the take runs
    for (int i = 0; i < 2 * FPS; i++) { u.rec_fill = (float)i / (2.0f * FPS) * 0.37f; frame(); }
    u.state = REC_PLAYING; u.loop_s = 7.3f; u.pos = 0; u.speed_now = 1.0f;   // let go: it loops
    go(1.0f, 6, 0, 2.0f);
    go(2.3f, 22, 0, 2.0f);                                            // nose down: faster
    go(-1.6f, -20, 0, 2.5f);                                          // nose up: backwards
    u.fx_delay = true; go(1.2f, 8, -30, 3.0f); u.fx_delay = false;    // roll left: tape delay
    u.fx_stutter = true; go(1.0f, 8, 35, 1.8f); u.fx_stutter = false; // roll right: stutter
    u.mode = REC_MODE_GRAIN; u.pitch_ratio = 1.5f; go(0.9f, 4, 0, 2.2f); u.mode = REC_MODE_TAPE; u.pitch_ratio = 1;   // grains
    target_hand = 1.0f; go(1.0f, 6, 0, 1.6f);                         // a hand comes down over the sensor: the filter closes
    target_hand = 0.35f; hold(1.0f); target_hand = 0.9f; hold(0.8f);
    target_hand = 0; hold(1.2f);
    target_jog = 3.5f; hold(0.5f); target_jog = -3.0f; hold(0.5f);    // the left knob: the tape dragged forwards, back, forwards
    target_jog = 2.5f; hold(0.4f); target_jog = -4.0f; hold(0.5f); target_jog = 0; hold(0.8f);
    menu_at(7); hold(0.9f);                                           // the menu: the list, down a few rows, a value changed
    for (int sel = 8; sel <= 10; sel++) { menu_at(sel); hold(0.35f); }
    menu_at(9); hold(0.3f); menu_at(8); hold(0.5f);
    u.menu_edit = true; hold(0.7f);
    for (int i = 1; i <= 8; i++) { snprintf(spd, sizeof spd, "%.2fx", 2.0 + 0.25 * i); u.param_frac = (2.0f + 0.25f * (float)i - 0.25f) / 7.75f; hold(0.12f); }
    hold(0.6f); u.menu_edit = false; hold(0.5f); u.menu = false; hold(0.6f);
    go(8.0f, 44, 0, 2.5f);                                            // all the way down: flung
    go(0.03f, 1, 0, 2.5f);                                            // flat: a slow crawl, never a stop
    go(1.0f, 6, 0, 1.5f);
    fprintf(stderr, "%d frames\n", n);
    return 0;
}
