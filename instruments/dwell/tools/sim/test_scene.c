// DWELL's screens on the host: shots of every state and the animations (no sound checks here). Called from
// inst_tests() after the boot check. The engine here may be a placeholder: where it reports nothing (the take's level,
// the difference tone, a room pass running) the test sets the engine's reported variables itself for the frame.
#include "sim.h"
#include "inst_dwell.h"
#include "link.h"
#include "ui.h"
#include <math.h>
#include <string.h>
#include <time.h>

float room_mic(float t, void *u);                        // test_engine.c: the pretend room
void  room_mic_reset(void);

static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static void draw_now(void) { view_t v = { .frame = 0, .t = sim_t, .cover = 0 }; oled_clear(); scene_draw(&v); }

static int lit_pixels(int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++) for (int x = 0; x < 128; x++) n += oled_get(x, y);
    return n;
}

static void knock(void)
{
    vec3_t keep = SI.acc;
    SI.acc = (vec3_t){ keep.x, keep.y, keep.z + 1.2f };
    sim_tick();
    SI.acc = keep;
}

void test_scene(void)
{
    section("screens");
    sim_tilt(0, 0);
    run(1.0f);
    sim_shot("pure");

    // the walk: forward one cell every 2 s, then a roll to the right
    sim_gif_begin("walk");
    for (int k = 1; k <= 3; k++) { sim_tilt(0, -10.0f * (float)k); run(2.0f); }
    sim_tilt(10, -30); run(2.0f);
    sim_gif_end();
    sim_shot("trail");
    check(D.lead_b == 3 && D.lead_c == 1, "the walk reached (3, 1): lead at (%d, %d) %s", D.lead_b, D.lead_c, D.lead_name);
    int on = 0; for (int i = 0; i < NV; i++) on += D.slot[i].on;
    check(on >= 3, "a trail is sounding (%d slots on)", on);

    // the knob: pure to dense over 4 s
    sim_tilt(0, -10); run(1.0f);
    sim_gif_begin("dense");
    for (int k = 0; k < 50; k++) { sim_turn(1); run(0.07f); }
    sim_gif_end();
    run(2.0f);
    check(D.dense > 0.95f, "DENSE at %.0f %%", (double)(D.dense * 100.0f));
    sim_shot("dense");
    eng_diff_hz = 82.0f; run(0.1f); sim_shot("diff"); eng_diff_hz = 0.0f;

    // a knock: the ripple and the head
    sim_gif_begin("knock");
    run(0.4f); knock(); run(1.2f);
    sim_gif_end();
    param_set(P_DENSE, 0.0f);

    // a rest
    sim_rec(0.1f); run(1.0f);
    check(D.rest, "a tap on REC rests");
    sim_shot("rest");
    sim_rec(0.1f); run(0.5f);
    check(!D.rest, "a second tap returns");

    // REC held: SING ONE NOTE
    SI.rec = true; run(0.8f);
    check(D.rec, "REC held records");
    eng_take_level = 0.25f; run(0.05f);
    sim_shot("rec");
    SI.rec = false; sim_tick(); run(0.5f);

    // the link: a second DWELL in the room, SPREAD on: the other board's tones hollow
    sim_peer_add("DWELL", 1, 10.0, 0.0);
    param_set(P_DRIFT, 0.5f);
    param_set(P_SPREAD, 1.0f);
    run(3.0f);
    check(D.peers >= 1 && D.spread, "a peer is there (%d) and SPREAD is on", D.peers);
    sim_shot("spread");
    param_set(P_SPREAD, 0.0f);
    run(1.5f);

    // ROOM PASS in the pretend room (test_engine.c): three passes of 2 s, drawn as they run
    room_mic_reset();
    SI.mic = room_mic;
    param_set(P_PASSES, 3.0f); param_set(P_PASSLEN, 2.0f);
    INST.action(P_PASS);
    sim_tick();                                                        // the audio task takes the request
    sim_gif_begin("room");
    for (int k = 0; k < 700 && eng_room_state == ROOM_PASSING; k++) {
        sim_tick();
        if (k == 330) sim_shot("room_pass");
    }
    sim_gif_end();
    run(1.0f);
    check(D.room_state == ROOM_DONE && D.source == SRC_ROOM, "after the passes the source is the room (state %d)", D.room_state);
    SI.mic = NULL;
    sim_shot("room_done");
    param_set(P_SOURCE, SRC_SINE);

    // the splash and the saver as stills (the sim's boot made the splash animation)
    oled_clear(); scene_splash(30); sim_shot("splash_open");
    oled_clear(); scene_splash(85); sim_shot("splash");
    oled_clear(); scene_saver(400); sim_shot("saver");
    int a = lit_pixels(0, 64);
    uint8_t keep[128 * 64 / 8] = { 0 };
    for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) if (oled_get(x, y)) keep[(y * 128 + x) >> 3] |= (uint8_t)(1 << (x & 7));
    oled_clear(); scene_saver(425);
    int moved = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) if (oled_get(x, y) != ((keep[(y * 128 + x) >> 3] >> (x & 7)) & 1)) moved++;
    check(a > 10 && moved > 10, "the saver draws (%d px) and moves (%d px changed in a second)", a, moved);

    // the scene with nothing in D (boot, no IMU), and with nonsense: it draws and stays sane
    dw_ui_t saved = D;
    memset(&D, 0, sizeof D);
    draw_now();
    int lit = lit_pixels(16, 64);
    check(lit > 50 && lit < 128 * 48 * 6 / 10, "an empty D draws a sane frame (%d px lit)", lit);
    D.cur_b = 9.0f; D.cur_c = -9.0f; D.lead_b = 3; D.lead_c = 2; D.limit = LIM_7; D.dense = 1.0f; D.strum = 5.0f; D.peers = 20; D.spread = 1; D.rank = 3;
    for (int i = 0; i < NV; i++) { D.slot[i].on = 1; D.slot[i].b = 3; D.slot[i].c = 2; D.slot[i].lvl = 0.5f; snprintf(D.slot[i].name, sizeof D.slot[i].name, "1323/1024"); }
    snprintf(D.lead_name, sizeof D.lead_name, "1323/1024");
    draw_now();
    lit = lit_pixels(16, 64);
    check(lit > 50 && lit < 128 * 48 * 6 / 10, "a nonsense D draws a sane frame (%d px lit)", lit);
    D.limit = LIM_3; draw_now(); sim_shot("limit3");
    D = saved;

    // the cost of a frame
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < 400; i++) draw_now();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec)) / 400.0 / 1000.0;
    metric("scene_draw_us", us);
    check(us < 1000.0, "a frame of the scene costs %.0f us on the host", us);
    run(0.2f);
}
