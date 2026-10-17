// WRECK's own checks: the host's (it plays, the axis's zones and the machine's tempo on the flock's beat, REC tapped
// stops and starts it, a knock, a pretend BAND on the link, the sounds, the cost), then each module's
// (test_<module>.c, written by its builder: test_mod_kit() ...; a module without one yet runs the empty default).
#include "sim.h"
#include "wreck.h"
#include "link.h"
#include <math.h>
#include <string.h>
#include <time.h>

// the axis is the knob's and moves the whole machine: the click check would hear a new zone's tempo as a click
const char *const inst_click_skip[] = { "xaxs", "xstp", "xchg", "nlnk", "ngrp", "npow", NULL };

__attribute__((weak)) void test_mod_kit(void) { }
__attribute__((weak)) void test_mod_beat(void) { }
__attribute__((weak)) void test_mod_amp(void) { }
__attribute__((weak)) void test_mod_fx(void) { }
bool sim_menu_param(int p);
void sim_menu_close(void);
void wr_prof(float *us, int n);
int  wr_zone(void);

void inst_excite(void) { }                                       // it plays by itself
static float noisy_mic(float t, void *u) { (void)u; return 0.2f * sinf(6.2831853f * 180.0f * t) * (fmodf(t, 0.25f) < 0.05f ? 1.0f : 0.05f); }

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); }

// a knock: a sharp jolt in the 1 kHz accelerometer stream for one tick
static void knock(float g)
{
    for (int i = 0; i < 10; i++) SI.burst[i] = (vec3_t){ 0, 0, 1.0f + (i == 3 ? g : i == 4 ? -0.6f * g : 0.0f) };
    SI.nburst = 10;
    sim_tick();
}

// ---------------------------------------------------------------- the host
static void plays(void)
{
    section("wreck: it plays");
    quiet(); set("xaxs", 10); sim_run(2.0f);
    float r = m_rms(sim_t - 1.0f, sim_t), p = m_peak(sim_t - 1.0f, sim_t);
    check(r > 0.01f && p < 0.99f, "switched on, it plays by itself (rms %.3f, peak %.3f)", (double)r, (double)p);
    sim_shot("breakcore");
}

static void zones(void)
{
    section("wreck: the axis");
    quiet(); set("stmp", 100); set("xstp", 0); set("xchg", 1); sim_run(0.2f);
    static const float ax[4] = { 10, 37, 62, 90 };
    static const int want_q[4] = { 1, 0, -1, -2 };
    int ok = 0;
    sim_gif_begin("axis");
    for (int z = 0; z < 4; z++) {
        set("xaxs", ax[z]); sim_run(1.5f);
        grid_t g; wr_grid_get(&g);
        if (wr_zone() == z && g.feel_q == want_q[z]) ok++;
        else note("axis %.0f: zone %d feel %d", (double)ax[z], wr_zone(), g.feel_q);
        char b[24]; snprintf(b, sizeof b, "zone_%d", z); sim_shot(b);
    }
    sim_gif_end();
    check(ok == 4, "the axis walks the zones and the tempo: x2, x1, x1/2, x1/4 of the song (%d of 4)", ok);
    // the machine's steps land on the song's beats: a step starts exactly on every song beat, at any feel
    int bad = 0;
    for (int q = 1; q >= -2; q--) {
        grid_t g = { q, song.fpb, song.origin };
        for (int b = 0; b < 64; b++) {
            uint32_t f = song.origin + (uint32_t)b * song.fpb;
            int32_t s = wr_step_at(&g, f), s1 = wr_step_at(&g, f - 1);
            int spb = wr_steps_per_beat(q);
            if (s != b * spb || s1 != b * spb - 1 + (b == 0 ? (int32_t)(SONG_WRAP_BEATS * spb) : 0)) bad++;
        }
    }
    check(bad == 0, "the machine's steps fall exactly on the song's beats at every feel (%d off)", bad);
    // TAPE STOP: the change waits for the tape's brake, then the next beat
    set("xchg", 0); set("xaxs", 10); sim_run(2.0f);
    grid_t g0; wr_grid_get(&g0);
    set("xaxs", 40); sim_run(1.5f);
    grid_t g1; wr_grid_get(&g1);
    check(g0.feel_q == 1 && g1.feel_q == 0, "with CHANGE: TAPE STOP the new tempo comes in after the brake (%d -> %d)", g0.feel_q, g1.feel_q);
    set("xaxs", 10); sim_run(1.5f);
}

static void rec_tap(void)
{
    section("wreck: REC tapped");
    quiet(); set("xaxs", 10); sim_run(1.0f);
    sim_rec(0.1f); sim_run(2.5f);
    float off = m_rms(sim_t - 0.5f, sim_t);
    bool st = perf.running;
    sim_rec(0.1f); sim_run(1.5f);
    float on = m_rms(sim_t - 0.8f, sim_t);
    check(!st && off < 0.1f * on && on > 0.01f, "a tap stops the machine (rms %.4f), another starts it (%.3f)", (double)off, (double)on);
    // REC held on and on: the take ends by itself at 8 s, and REC does nothing more until it is let go
    SI.mic = noisy_mic;
    SI.rec = true; sim_run(9.0f);
    bool still = kit_recording();
    sim_run(1.0f);
    bool again = kit_recording();
    SI.rec = false; quiet(); sim_run(3.0f);
    check(!still && !again && perf.running, "REC held for 9 s: the take ends by itself at 8 s and nothing starts again while it is held");
}

int wr_knocks(void);
static void knocks(void)
{
    section("wreck: the hands and knocks");
    quiet(); set("xaxs", 10); set("xkno", 1); sim_run(0.5f);
    sim_tilt(0, 30); sim_run(0.5f);
    float ch = perf.chaos;
    sim_tilt(0, -30); sim_run(0.5f);
    float dr = perf.drag;
    sim_tilt(-35, 0); sim_run(0.5f);
    float sm = perf.smear;
    sim_tilt(35, 0); sim_run(0.5f);
    float stt = perf.stutter;
    sim_tilt(0, 0); sim_run(0.5f);
    check(ch > 0.5f && dr > 0.5f && sm > 0.5f && stt > 0.5f && perf.chaos == 0.0f && perf.stutter == 0.0f,
          "tipped forward: chaos %.2f, back: drag %.2f; rolled left: smear %.2f, right: stutter %.2f; flat: none", (double)ch, (double)dr, (double)sm, (double)stt);
    SI.jy = 1.0f; sim_run(0.1f); float ro = perf.roll;
    SI.jy = -1.0f; sim_run(0.1f); float dv = perf.dive;
    SI.jy = 0; SI.jx = -1.0f; sim_run(0.1f); float cr = perf.crush;
    SI.jx = 1.0f; sim_run(0.1f); float dri = perf.drive;
    SI.jx = 0; sim_run(0.1f);
    check(ro > 0.9f && dv > 0.9f && cr > 0.9f && dri > 0.9f && perf.roll == 0.0f, "the stick: roll %.2f, dive %.2f, crush %.2f, drive %.2f", (double)ro, (double)dv, (double)cr, (double)dri);
    int k0 = wr_knocks();
    knock(3.0f); sim_run(0.3f);
    int k1 = wr_knocks();
    for (int i = 0; i < 30; i++) { sim_tilt(0, (float)(i % 2 ? 8 : -8)); sim_tick(); }   // carried about: no knock
    sim_tilt(0, 0); sim_run(0.3f);
    int k2 = wr_knocks();
    set("xkno", 0); knock(3.0f); sim_run(0.3f);
    int k3 = wr_knocks();
    set("xkno", 1);
    check(k1 == k0 + 1 && k2 == k1 && k3 == k2, "a knock is a knock (%d), carrying it about is not (%d), KNOCKS OFF: none (%d)", k1 - k0, k2 - k1, k3 - k2);
}

// ---------------------------------------------------------------- the link: a pretend BAND
static void with_band(void)
{
    section("wreck: linked to a BAND");
    quiet(); set("stmp", 100); sim_run(0.1f);
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); link_set_harm(4, 1, 6, 0, 0, -1); }   // 120 BPM, E minor
    sim_peer_use(-1);
    sim_run(2.0f);
    check(!link_is_clock() && link_synced(), "WRECK takes the BAND as the clock and syncs to it");
    check(song.fpb == 16000 && (int)get("stmp") == 120 && (int)get("skey") == 4, "the BAND's 120 BPM and its key (E: %d)", (int)get("skey"));
    set("xaxs", 10); sim_run(2.0f);
    grid_t g; wr_grid_get(&g);
    float gap = sim_peer_gap_ms(k);
    check(g.fpb == 16000 && g.origin == song.origin && fabsf(gap) < 1.0f,
          "the machine's grid is the BAND's beat (x2: 240 BPM; the beats %.3f ms apart)", (double)gap);
    sim_peer_power(k, false);
    sim_run(1.5f);
    check(link_is_clock(), "the BAND switched off: WRECK keeps its time alone");
    set("skey", 9); set("sscl", 4); set("stmp", 100); sim_run(0.2f);
}

// ---------------------------------------------------------------- every sound plays, at both ends
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
static void sounds(void)
{
    section("wreck: the sounds");
    quiet();
    int ok = 0;
    sim_gif_begin("sounds");
    for (int k = 0; k < INST.npresets; k++) {
        front_midi(0xC0, (uint8_t)k, 0);
        set("xaxs", 15); sim_run(1.5f);
        float r0 = m_rms(sim_t - 1.0f, sim_t), p0 = m_peak(sim_t - 1.0f, sim_t);
        set("xaxs", 85); sim_run(3.0f);
        float r1 = m_rms(sim_t - 2.0f, sim_t), p1 = m_peak(sim_t - 2.0f, sim_t);
        bool good = r0 > 0.005f && r1 > 0.005f && p0 < 0.99f && p1 < 0.99f;
        if (good) ok++; else note("%s: breakcore rms %.4f peak %.3f, drone rms %.4f peak %.3f", INST.presets[k].name, (double)r0, (double)p0, (double)r1, (double)p1);
        char b[24]; snprintf(b, sizeof b, "sound_%d", k); sim_shot(b);
    }
    sim_gif_end();
    check(ok == INST.npresets, "every sound plays at both ends, under full scale (%d of %d)", ok, INST.npresets);
    front_midi(0xC0, 0, 0); set("xaxs", 10); sim_run(0.5f);
}

// ---------------------------------------------------------------- the cost
static void cost(void)
{
    section("wreck: cost");
    quiet();
    float worst = 0; int wz = 0;
    static const float ax[4] = { 10, 37, 62, 90 };
    for (int z = 0; z < 4; z++) {
        set("xaxs", ax[z]); sim_run(1.5f);
        SI.jy = 0.8f; sim_tilt(20, 25);                             // everything pushed: a roll, chaos, the stutter
        float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
        struct timespec a, b;
        double tsum = 0; int nblk = 2000;
        { float junk[8]; wr_prof(junk, 8); }
        for (int k = 0; k < nblk; k++) {
            memset(in, 0, sizeof in);
            if (k % 5 == 0) sim_tick();
            clock_gettime(CLOCK_MONOTONIC, &a);
            front_audio(in, out, AUDIO_BLOCK);
            clock_gettime(CLOCK_MONOTONIC, &b);
            tsum += (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
        }
        float parts[8]; wr_prof(parts, 8);
        double us = tsum / nblk;
        note("%s: %.1f us per block (beat %.1f amp %.1f fx %.1f kit %.1f mix %.1f)", host_zone_name(z), us,
             (double)(parts[M_BEAT] * 0.96f), (double)(parts[M_AMP] * 0.96f), (double)(parts[M_FX] * 0.96f),
             (double)(parts[M_KIT] * 0.96f), (double)(parts[M_N] * 0.96f));
        if (us > worst) { worst = (float)us; wz = z; }
        quiet();
    }
    metric("wreck_us_per_block", worst);
    check(worst < 17.0f, "the busiest zone (%s) takes %.1f us per 64-frame block (budget 17: about 60 %% of the board)", host_zone_name(wz), (double)worst);
}

static void sweep(void)
{
    section("wreck: from breakcore to doom");
    quiet(); set("xchg", 0); set("xaxs", 0); sim_run(2.0f);
    float t0 = sim_t;
    sim_gif_begin("sweep");
    for (int a = 0; a <= 100; a += 2) { set("xaxs", (float)a); sim_run(0.4f); }
    sim_run(4.0f);
    sim_gif_end();
    sim_wav("-sweep", t0, sim_t);
    for (int a = 100; a >= 10; a -= 10) { set("xaxs", (float)a); sim_run(0.1f); }
    sim_run(1.0f);
    check(m_bad() == 0, "a sweep from 0 to 100 and back: no NaN or inf");
}

void inst_tests(void)
{
    plays();
    zones();
    rec_tap();
    knocks();
    with_band();
    sounds();
    section("kit"); quiet(); set("xaxs", 10); sim_run(0.5f); test_mod_kit();
    section("beat"); quiet(); set("xaxs", 10); sim_run(0.5f); test_mod_beat();
    section("amp"); quiet(); set("xaxs", 10); sim_run(0.5f); test_mod_amp();
    section("fx"); quiet(); set("xaxs", 10); sim_run(0.5f); test_mod_fx();
    sweep();
    cost();
}
