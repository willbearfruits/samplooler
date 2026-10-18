// MANTRA's own checks: the host's (it sings, the hands, the knock, the sounds, a pretend BAND on the link, the cost),
// then each module's (test_voice.c: test_mod_voice(); test_score.c: test_mod_score(); test_drone.c: test_mod_drone()).
#include "sim.h"
#include "mantra.h"
#include "link.h"
#include <math.h>
#include <string.h>
#include <time.h>

const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", NULL };
__attribute__((weak)) void test_mod_voice(void) { }
__attribute__((weak)) void test_mod_score(void) { }
__attribute__((weak)) void test_mod_drone(void) { }
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
void mn_prof(float *us, int n);
int  mn_knocks(void);
void inst_excite(void) { }                                       // it sings by itself

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }
static float hum_in(float t, void *u) { (void)u; return 0.2f * sinf(6.2831853f * 196.0f * t) + 0.08f * sinf(6.2831853f * 392.0f * t); }   // a hum at G3 for HUM ALONG
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); SI.gyro = (vec3_t){ 0, 0, 0 }; }

static void sings(void)
{
    section("mantra: it sings");
    quiet(); front_midi(0xC0, 0, 0); sim_run(3.0f);
    float r = m_rms(sim_t - 2.0f, sim_t), p = m_peak(sim_t - 2.0f, sim_t);
    check(r > 0.005f && p < 0.99f, "switched on, it sings by itself (rms %.3f, peak %.3f)", (double)r, (double)p);
    sim_shot("om");
}

static void hands_check(void)
{
    section("mantra: the hands");
    quiet(); sim_run(0.3f);
    sim_tilt(0, 30); sim_run(0.4f); float j_fwd = hands.jaw;
    sim_tilt(0, -30); sim_run(0.4f); float j_back = hands.jaw;
    sim_tilt(30, 0); sim_run(0.4f); float r_right = hands.roll;
    sim_tilt(0, 0); sim_run(0.4f);
    check(j_fwd > 0.75f && j_back < 0.25f && r_right > 0.5f && fabsf(hands.jaw - 0.5f) < 0.05f,
          "tilt: the jaw %.2f forward, %.2f back, roll %.2f; flat 0.5", (double)j_fwd, (double)j_back, (double)r_right);
    float y0 = hands.yaw;
    SI.gyro = (vec3_t){ 0, 0, 180.0f }; sim_run(1.0f); SI.gyro = (vec3_t){ 0, 0, 0 }; sim_run(0.1f);
    float turned = hands.yaw - y0;
    check(fabsf(turned - 0.5f) < 0.05f, "turned flat at 180 degrees a second for a second: half a turn (%.2f)", (double)turned);
    int k0 = mn_knocks();
    for (int i = 0; i < 10; i++) SI.burst[i] = (vec3_t){ 0, 0, 1.0f + (i == 3 ? 3.0f : i == 4 ? -1.8f : 0.0f) };
    SI.nburst = 10; sim_tick(); sim_run(0.3f);
    check(mn_knocks() == k0 + 1, "a knock strikes the bowl");
}

static void sounds(void)
{
    section("mantra: the sounds");
    quiet();
    int ok = 0;
    sim_gif_begin("sounds");
    for (int k = 0; k < INST.npresets; k++) {
        front_midi(0xC0, (uint8_t)k, 0);
        SI.mic = sc_mode() == MODE_HUM ? hum_in : NULL;             // HUM ALONG sings only when someone hums
        sim_run(4.0f);
        float r = m_rms(sim_t - 3.0f, sim_t), p = m_peak(sim_t - 3.0f, sim_t);
        bool good = r > 0.003f && p < 0.99f;
        if (good) ok++; else note("%s: rms %.4f peak %.3f", INST.presets[k].name, (double)r, (double)p);
        char b[24]; snprintf(b, sizeof b, "sound_%d", k); sim_shot(b);
    }
    sim_gif_end();
    SI.mic = NULL;
    check(ok == INST.npresets, "every sound sings, under full scale (%d of %d)", ok, INST.npresets);
    front_midi(0xC0, 0, 0); sim_run(0.5f);
}

static void with_band(void)
{
    section("mantra: linked to a BAND");
    quiet(); sim_run(0.1f);
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); link_set_harm(9, 1, 6, 0, 0, -1); }
    sim_peer_use(-1);
    sim_run(2.0f);
    check(!link_is_clock() && link_synced(), "MANTRA takes the BAND as the clock");
    check(song.fpb == 16000 && (int)get("skey") == 9 && (int)get("sscl") == 1, "the BAND's 120 BPM and its A minor");
    sim_peer_power(k, false); sim_run(1.5f);
    set("skey", 7); set("sscl", 0); set("stmp", 72); sim_run(0.2f);
}

static void cost(void)
{
    section("mantra: cost");
    quiet();
    double worst = 0; int wk = 0;
    for (int k = 0; k < INST.npresets; k++) {
        front_midi(0xC0, (uint8_t)k, 0); sim_run(1.5f);
        float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
        struct timespec a, b; double t = 0; int nb = 1500;
        for (int i = 0; i < nb; i++) {
            for (int j = 0; j < AUDIO_BLOCK; j++) in[j] = 0.05f * sinf((float)(i * AUDIO_BLOCK + j) * 0.03f);
            if (i % 5 == 0) sim_tick();
            clock_gettime(CLOCK_MONOTONIC, &a); front_audio(in, out, AUDIO_BLOCK); clock_gettime(CLOCK_MONOTONIC, &b);
            t += (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
        }
        double us = t / nb;
        if (us > worst) { worst = us; wk = k; }
    }
    metric("mantra_us_per_block", worst);
    check(worst < 20.0, "the busiest sound (%s) takes %.1f us per 64-frame block (budget 20)", INST.presets[wk].name, worst);
    front_midi(0xC0, 0, 0); sim_run(0.3f);
}

// the knob from the monks' chant to Daisy over a chorus, and Daisy's last line (a bicycle built for two)
static void blend_sweep(void)
{
    section("mantra: from the chant to Daisy");
    quiet(); sim_turn(1); sim_turn(-1);                       // a touch wakes the screen (the saver came on in the long tests)
    front_midi(0xC0, 1, 0); set("hbld", 0); sim_run(3.0f);
    float t0 = sim_t;
    sim_gif_begin("blend");
    for (int a = 0; a <= 100; a += 2) { set("hbld", (float)a); for (int k = 0; k < 8; k++) { ui_awake(); sim_run(0.1f); } }
    sim_gif_end();
    sim_wav("-blend", t0, sim_t);
    check(m_bad() == 0 && m_rms(sim_t - 5.0f, sim_t) > 0.003f, "the knob from 0 to 100: it sings all the way, no NaN");
    // Daisy alone until her last line, then that line as an animation
    sc_view_t v; int guard = 0;
    do { ui_awake(); sim_run(0.1f); sc_view(&v); } while (!(v.line && strstr(v.line, "BICYCLE")) && ++guard < 900);
    sim_gif_begin("daisy");
    for (int k = 0; k < 70; k++) { ui_awake(); sim_run(0.1f); }
    sim_gif_end();
    sim_shot("daisy_bicycle");
    check(guard < 900, "Daisy reaches her last line (a bicycle built for two)");
    set("hbld", 0); front_midi(0xC0, 0, 0); sim_run(0.5f);
}

void inst_tests(void)
{
    sings();
    hands_check();
    sounds();
    with_band();
    section("voice"); quiet(); front_midi(0xC0, 0, 0); sim_run(0.5f); test_mod_voice();
    section("score"); quiet(); front_midi(0xC0, 0, 0); sim_run(0.5f); test_mod_score();
    section("drone"); quiet(); front_midi(0xC0, 0, 0); sim_run(0.5f); test_mod_drone();
    blend_sweep();
    cost();
}
