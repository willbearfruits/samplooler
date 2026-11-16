// The screen against nonsense: ui_draw() fed random states, with every number allowed to be anything (NaN, infinity,
// far out of range) and every text as long as main.c can make it, the clock jumping about. Built with the address
// and undefined-behaviour sanitizers (run.sh fuzz): anything written outside the framebuffer, any overflow, any
// float that cannot become an int stops it. It also checks that nothing is ever drawn outside the list's window
// while the menu is open over the header.   fuzz [frames]
#include "ui.h"
#include "oled.h"
#include "recorder.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t seed = 0xf00d;
static uint32_t r32(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static float rf(void) { return (float)(r32() % 1000000u) / 1e6f; }
static float wild(float lo, float hi)
{
    switch (r32() % 12) {
    case 0: return NAN;
    case 1: return INFINITY;
    case 2: return -INFINITY;
    case 3: return 1e30f * (rf() - 0.5f);
    case 4: return lo; case 5: return hi;
    default: return lo + (hi - lo) * rf() * (r32() % 8 == 0 ? 3.0f : 1.0f);
    }
}
static int wildi(int lo, int hi) { return r32() % 10 == 0 ? (int)r32() : lo + (int)(r32() % (uint32_t)(hi - lo + 1)); }
static const char *text(int max)
{
    static char pool[8][64]; static int k;
    static const char *const some[] = { "Speed max", "8.00x", "-> slot 3", "NATURAL TUNE", "WHILE REC", "OFF (line out)", "", "+24 st", "= speed",
                                        "CRASH: slot save", "WATCHDOG", "BROWNOUT", "SAVED 3", "LOAD FAILED", "SLOT 4 EMPTY", "NOTHING TO SAVE", "SAFE MODE: 3 crashes",
                                        "WRECK + MANTRA + HUFF + BAND + PIXEL", "LINKED: SAMPLOOL", "12345.678", "-", ":", "100%", "\x81\x82\x83\x84\x80" };
    if (r32() % 3) return some[r32() % (sizeof some / sizeof some[0])];
    char *b = pool[k++ & 7]; int n = (int)(r32() % (uint32_t)max);
    for (int i = 0; i < n; i++) b[i] = (char)(1 + r32() % 255);       // any byte but the end
    b[n] = 0; return b;
}
int main(int argc, char **argv)
{
    long n = argc > 1 ? atol(argv[1]) : 300000;
    oled_init(NULL);
    static ui_t u;
    uint32_t now = 0;
    long drawn = 0;
    for (long f = 0; f < n; f++) {
        if (r32() % 4 == 0) {                                          // a new state, wholly random
            u.state = wildi(-1, 3); u.mode = wildi(0, 1);
            u.speed_now = wild(-8, 8); u.speed_cmd = wild(-8, 8); u.spd_max = wild(0, 8); u.pitch_ratio = wild(0, 4);
            u.tilt_pitch = wild(-90, 90); u.tilt_roll = wild(-90, 90);
            u.has_imu = r32() & 1; u.tilt_on = r32() & 1; u.has_joy = r32() & 1; u.frozen = r32() % 5 == 0;
            u.pos = wild(0, 1); u.rec_fill = wild(0, 1); u.loop_s = wild(0, 20); u.max_s = wildi(0, 20);
            u.meter_in = wild(0, 1.5f); u.meter_out = wild(0, 1.5f); u.clip = r32() % 6 == 0;
            u.fx_delay = r32() % 4 == 0; u.fx_stutter = r32() % 4 == 0; u.cpu = wildi(0, 100);
            u.menu = r32() % 3 == 0; u.menu_edit = r32() % 3 == 0; u.param = wildi(0, 30); u.param_n = wildi(0, 30);
            u.param_name = r32() % 20 ? text(32) : NULL; u.param_val = r32() % 20 ? text(24) : NULL; u.param_frac = wild(-1, 1);
            u.toast_ms = r32() % 3 ? 0 : wildi(0, 3000);
            u.busy_text = r32() % 8 ? NULL : (r32() & 1 ? "SAVING 3" : text(32)); u.busy_frac = wild(0, 1);
            u.note = r32() % 6 ? NULL : text(40);
            u.idle_ms = r32() % 10 ? 0 : wildi(0, 400000); u.still_ms = r32() % 10 ? 0 : wildi(0, 900000);
            u.voice = r32() % 3 ? NULL : text(16); u.peers = wildi(0, 8); u.beat_flash = wildi(0, 3); u.closing = r32() % 8 == 0;
            u.waiting = r32() % 4 == 0; u.has_knob = r32() & 1; u.has_hand = r32() & 1; u.knob_mode = wildi(0, UI_KNOB_N - 1); u.hand_mode = wildi(0, UI_HAND_N - 1);
            u.jog = wild(-8, 8); u.knob_st = wildi(-24, 24); u.hand = wild(0, 1); u.hand_seen = r32() & 1; u.filter = wild(-1, 1);
            u.menu_n = r32() % 4 ? wildi(0, 40) : 0; u.menu_sel = wildi(0, 40); u.menu_group = r32() % 10 ? text(12) : NULL;
            for (int r = 0; r < UI_ROWS; r++) {
                u.row_name[r] = r32() % 6 ? text(20) : NULL; u.row_act[r] = r32() % 5 == 0;
                snprintf(u.row_val[r], UI_VAL_N, "%s", text(UI_VAL_N));
                if (r32() % 30 == 0) memset(u.row_val[r], 'W', UI_VAL_N);  // not even ended
            }
            u.dim = r32() % 9 == 0; u.check = r32() % 25 == 0; u.chk_sw = r32() & 1; u.chk_rec = r32() & 1; u.chk_enc = wildi(-99, 99); u.chk_knob = wildi(-99, 99);
            u.hand_mm = wildi(0, 1300); u.version = r32() % 8 ? "v0.2.0-12-gabcdef0-dirty" : NULL;
        }
        // the clock: a steady 33 ms, a stall, a step back, or none at all (the old sims)
        uint32_t k = r32() % 40;
        now += k == 0 ? 5000 : k == 1 ? (uint32_t)-200 : k == 2 ? 0 : 33;
        u.now_ms = r32() % 500 == 0 ? 0 : now;
        ui_draw(&u);
        drawn++;
    }
    printf("%ld frames of nonsense drawn, nothing broke\n", drawn);
    return 0;
}
