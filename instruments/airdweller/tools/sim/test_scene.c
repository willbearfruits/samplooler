// AIRDWELLER's screens on the host: every role, played; a pair; CUT; a loop recorded; the splash and the saver; the
// squeeze under the core's strip; what a frame costs. Shots go to out/airdweller/, clips to out/airdweller-*.gif.
#include "sim.h"
#include "inst_ad.h"
#include "link.h"
#include "oled.h"
#include <math.h>
#include <string.h>
#include <time.h>

#define STRIP_Y_CHECK 46                // the first row the core may cover

float st_mic(float t, void *u);
void  st_reset(void);
void  st_swing(float t, vec3_t dir, float peak);
void  st_hold(vec3_t dir, float dps);
extern const vec3_t ST_FRONT, ST_BACK, ST_LEFT, ST_RIGHT, ST_TWIST;
bool  sim_menu_param(int p);
void  sim_menu_close(void);

static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static int lit(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) n += oled_get(x, y);
    return n;
}
static void role(int r) { param_set(param_by_tag("role"), (float)r); }

// the scene alone, as the core would call it (cover: the rows the core's strip takes)
static void draw_alone(int cover)
{
    static uint32_t f = 100000;
    view_t v = { .frame = ++f, .t = (float)f / 25.0f, .cover = cover, .out_level = 0.3f, .in_level = 0.0f };
    oled_clear();
    scene_draw(&v);
}
static double cost_us(int n)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < n; i++) draw_alone(i % 7 == 0 ? 18 : 0);
    clock_gettime(CLOCK_MONOTONIC, &b);
    return ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3) / n;
}

// a pretend second box: its link bytes as instrument.c's app_update() writes them
static int peer = -1;
static uint8_t pseq;
static struct { int64_t tick; uint8_t voice, vel; } psent[3];
static void peer_say(int prole, int phand, bool hit, int voice, int vel)
{
    if (hit) {
        grid_t g; eng_get_grid(&g);
        psent[0] = psent[1]; psent[1] = psent[2];
        psent[2].tick = eng_tick_at(&g, audio_frames()) + PPB / 8;   // a little ahead, as a loop's hits are sent
        psent[2].voice = (uint8_t)voice; psent[2].vel = (uint8_t)vel;
        pseq++;
    }
    uint8_t a[16] = { 3, (uint8_t)(prole | phand << 2), 0, 36, pseq };
    for (int k = 0; k < 3; k++) {
        uint16_t lo = (uint16_t)(psent[k].tick & 0xFFFF);
        a[5 + 3 * k] = (uint8_t)(lo & 0xFF); a[6 + 3 * k] = (uint8_t)(lo >> 8);
        a[7 + 3 * k] = (uint8_t)((psent[k].voice & 15) << 4 | (psent[k].vel >> 3));
    }
    sim_peer_use(peer); link_set_app(a, 16); link_poke(); sim_peer_use(-1);
}

static void shake(float s)
{
    int n = (int)(s * 100.0f);
    for (int i = 0; i < n; i++) { SI.acc = (vec3_t){ (i & 1) ? 0.5f : -0.5f, 0.0f, (i & 2) ? 1.6f : 0.3f }; sim_tick(); }
    sim_tilt(0, 0);
}

void test_scene(void)
{
    section("screens");
    SI.mic = st_mic; st_reset(); sim_tilt(0, 0);
    role(ROLE_DRUM);
    run(0.5f);
    sim_shot("drum");
    check(lit(0, 16, 80, 45) > 120, "DRUM: the compass is drawn (%d px)", lit(0, 16, 80, 45));

    // ---- DRUM: swings, each pad
    sim_gif_begin("drum");
    const vec3_t *dirs[4] = { &ST_LEFT, &ST_RIGHT, &ST_FRONT, &ST_BACK };
    int popped = 0;
    for (int r = 0; r < 3; r++) for (int d = 0; d < 4; d++) {
        st_swing(sim_t + 0.05f, *dirs[d], 650.0f + 150.0f * (float)r);
        int before = lit(0, 16, 80, 63), most = before;
        bool shot = r != 1 || (d != 0 && d != 2);
        for (int i = 0; i < 30; i++) {
            sim_tick();
            int n = lit(0, 16, 80, 63);
            if (n > most) most = n;
            if (!shot && n > before + 40 && i % 4 == 3) { sim_tick(); sim_shot(d == 0 ? "drum_hit_left" : "drum_hit_front"); shot = true; }
        }
        if (most > before + 25) popped++;
        run(0.15f);
    }
    sim_gif_end();
    check(popped >= 9, "DRUM: a hit pops its pad (%d of 12 swings lit up the compass)", popped);

    // ---- DRUM: a loop of two bars, played while recording
    sim_gif_begin("loop");
    bool saw_wait = false, saw_rec = false;
    for (int i = 0; i < 300 && !(D.beat_in_bar == 2 && D.beat_phase > 0.3f); i++) sim_tick();   // REC down late in a bar: a count-in
    SI.rec = true;
    for (int i = 0; i < 520; i++) {
        if (i % 50 == 10) st_swing(sim_t + 0.02f, (i / 50) & 1 ? *dirs[1] : *dirs[0], 700.0f);
        sim_tick();
        if (D.rec == 1 && !saw_wait) { saw_wait = true; run(0.1f); sim_shot("drum_count_in"); }
        if (D.rec == 2 && !saw_rec && D.beat_in_bar == 2) { saw_rec = true; sim_shot("drum_recording"); }
    }
    SI.rec = false;
    for (int i = 0; i < 400; i++) { if (i % 50 == 10) st_swing(sim_t + 0.02f, ST_FRONT, 600.0f); sim_tick(); }
    sim_shot("drum_loop_on");
    sim_gif_end();
    check(saw_rec && (D.loop_bars[0] > 0 || D.loop_bars[1] > 0), "a loop recorded: REC on the header, an arc, then an orbit (%d bars)", D.loop_bars[0]);
    if (!saw_wait) note("no count-in seen (REC went down inside the bar's first beat)");

    // ---- CUT
    sim_gif_begin("cut");
    run(0.5f);
    sim_rec(0.1f);
    run(0.1f);
    sim_shot("cut");
    draw_alone(0);                                                     // the scene by itself: no hint or toast strip
    int cut_px = lit(0, 16, 127, 63);
    run(1.0f);
    check(D.cut && cut_px < 260, "CUT: the scene goes dark (%d px lit)", cut_px);
    sim_rec(0.1f);
    run(1.0f);
    sim_gif_end();
    check(!D.cut, "CUT again: back in");

    // ---- covered: the scene squeezes above the core's strip
    sim_menu_param(param_by_tag("role"));
    run(0.4f);
    sim_shot("drum_menu");
    draw_alone(18);
    check(lit(0, STRIP_Y_CHECK, 127, 63) == 0, "DRUM with the strip up: nothing of the scene in the bottom 18 rows");
    sim_menu_close();

    // ---- PAINT: struck by the other hand's kick, by this hand's swings; tilt morphs it, twist leans it
    role(ROLE_PAINT);
    run(0.3f);
    sim_gif_begin("paint");
    for (int m = 0; m < NMAT; m++) {
        param_set(param_by_tag("matl"), (float)m);
        for (int k = 0; k < 3; k++) {
            eng_strike(0, 110, DV_KICK); scene_hit(-1, 110, 1);   // as peer_tick() does with a hit from the other box
            run(0.12f);
            if (k == 0) { char b[24]; snprintf(b, sizeof b, "paint_%s", mat_name[m]); sim_shot(b); }
            run(0.3f);
        }
        st_swing(sim_t + 0.05f, m & 1 ? ST_RIGHT : ST_LEFT, 750.0f);
        run(0.5f);
    }
    check(lit(0, 16, 80, 63) > 80, "PAINT: the material is drawn");
    param_set(param_by_tag("matl"), MAT_METAL);
    for (int i = 0; i <= 20; i++) { sim_tilt(0.0f, -4.5f * (float)i); if (i % 5 == 0) { eng_strike(0, 100, DV_SNAP); scene_hit(-1, 100, 1); } run(0.1f); }
    sim_shot("paint_tilted");
    float morph_hi = D.morph;
    st_hold(ST_TWIST, 280.0f);
    eng_strike(0, 120, DV_KICK); scene_hit(-1, 120, 1);
    run(0.6f);
    sim_shot("paint_twist");
    float bent = D.bend;
    st_hold(ST_TWIST, 0.0f);
    sim_tilt(0, 0);
    run(1.0f);
    sim_gif_end();
    check(morph_hi > 0.6f, "tilting moves through the materials (morph %.2f from METAL)", (double)morph_hi);
    check(fabsf(bent) > 1.0f, "twisting bends it (%.1f st)", (double)bent);
    draw_alone(18);
    check(lit(0, STRIP_Y_CHECK, 127, 63) == 0, "PAINT with the strip up: nothing in the bottom 18 rows");

    // ---- PUMP: moving fills it, stillness lets it fall, a shake rattles it
    role(ROLE_PUMP);
    param_set(param_by_tag("matl"), MAT_RUBBER);
    run(0.3f);
    sim_shot("pump_still");
    sim_gif_begin("pump");
    st_hold(ST_TWIST, 300.0f);
    run(3.0f);
    sim_shot("pump_half");
    run(3.0f);
    shake(0.6f);
    sim_shot("pump_shake");
    float e_full = D.energy;
    st_hold(ST_TWIST, 0.0f);
    run(3.0f);
    sim_shot("pump_falling");
    sim_gif_end();
    check(e_full > 0.5f, "PUMP: moving fills it (energy %.2f)", (double)e_full);

    // ---- a pair: another AIRDWELLER, in DRUM, on the left
    peer = sim_peer_add("AIRDWELL", 1, 5.0, 0.0);
    peer_say(ROLE_DRUM, HAND_LEFT, false, 0, 0);
    run(1.5f);
    check(D.paired, "the other box is heard (paired, this one is %s)", D.hand == HAND_LEFT ? "LEFT" : D.hand == HAND_RIGHT ? "RIGHT" : "ONE");
    role(ROLE_PAINT);
    param_set(param_by_tag("matl"), MAT_GLASS);
    run(0.3f);
    sim_gif_begin("pair");
    static const int pv_[8] = { DV_KICK, DV_HAT, DV_SNAP, DV_HAT, DV_KICK, DV_KICK, DV_SNAP, DV_WHIP };
    float flash_max = 0.0f;
    for (int i = 0; i < 16; i++) {
        peer_say(ROLE_DRUM, HAND_LEFT, true, pv_[i & 7], 90 + (i & 3) * 10);
        for (int k = 0; k < 25; k++) { sim_tick(); if (D.peer_flash > flash_max) flash_max = D.peer_flash; if (i == 4 && k == 3) sim_shot("pair_paint_struck"); }
        if (i == 7) { param_set(param_by_tag("matl"), MAT_LATEX); }
    }
    role(ROLE_DRUM);
    for (int i = 0; i < 12; i++) {
        peer_say(ROLE_PAINT, HAND_LEFT, true, pv_[i & 7], 100);
        if (i % 3 == 1) st_swing(sim_t + 0.1f, ST_RIGHT, 700.0f);
        for (int k = 0; k < 25; k++) { sim_tick(); if (i == 5 && k == 3) sim_shot("pair_drum"); }
    }
    sim_gif_end();
    check(flash_max > 0.5f, "the other box's hits reach the screen (peer flash %.2f)", (double)flash_max);
    sim_peer_power(peer, false);

    // ---- what a frame costs (host): 400 frames of each role
    double us[NROLE];
    for (int r = 0; r < NROLE; r++) {
        role(r);
        eng_strike(0, 120, DV_KICK); scene_hit(-1, 120, 1); scene_hit(PD_LEFT, 120, 0);
        run(0.2f);
        us[r] = cost_us(400);
    }
    metric("scene_us_drum", us[0]); metric("scene_us_paint", us[1]); metric("scene_us_pump", us[2]);
    check(us[0] < 1000.0 && us[1] < 1000.0 && us[2] < 1000.0, "a frame costs %.0f / %.0f / %.0f us (DRUM / PAINT / PUMP) on the host", us[0], us[1], us[2]);
    role(ROLE_DRUM);

    // ---- the splash and the saver, drawn alone
    static const int sf[] = { 6, 13, 30, 62, 90 };
    for (int i = 0; i < 5; i++) {
        for (int f = 0; f <= sf[i]; f++) { oled_clear(); scene_splash(f); }
        char b[24]; snprintf(b, sizeof b, "splash_%02d", sf[i]); sim_shot(b);
    }
    bool more = true; int last = 0;
    for (int f = 0; f < 100 && more; f++) { oled_clear(); more = scene_splash(f); last = f; }
    check(!more && last < 100, "the splash ends by itself (frame %d)", last);
    oled_clear(); scene_saver(500); int s0 = lit(0, 0, 127, 63); sim_shot("saver");
    uint8_t a[128 * 64]; for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) a[y * 128 + x] = oled_get(x, y);
    oled_clear(); scene_saver(1500); int moved = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) moved += a[y * 128 + x] != oled_get(x, y);
    check(s0 < 400 && moved > 40, "the saver: little lit (%d px), and it moves (%d px changed in 40 s)", s0, moved);
    param_set(CP(CP_SAVER), 1);
    SI.mic = NULL;
    run(61.0f);
    sim_gif_begin("saver");
    run(6.0f);
    sim_gif_end();
    sim_turn(1); run(0.2f);
    param_set(CP(CP_SAVER), param(CP(CP_SAVER))->def);
    SI.mic = st_mic;
}
