// WRECK 2's own checks: the host's (it plays, the axis's zones and the machine's tempo on the flock's beat, the tilt's
// speed, STOP / GO, the CATCH, the bars, your riff, a knock, a pretend BAND on the link, the sounds, the cost), then each module's
// (test_<module>.c, written by its builder: test_mod_kit() ...; a module without one yet runs the empty default).
#include "sim.h"
#include "wreck.h"
#include "link.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

// the axis is the knob's and moves the whole machine: the click check would hear a new zone's tempo as a click
const char *const inst_click_skip[] = { "xaxs", "xstp", "xchg", "nlnk", "ngrp", "npow", NULL };

__attribute__((weak)) void test_mod_kit(void) { }
__attribute__((weak)) void test_mod_beat(void) { }
__attribute__((weak)) void test_mod_amp(void) { }
__attribute__((weak)) void test_mod_fx(void) { }
bool sim_menu_param(int p);
typedef struct { uint32_t frame; int16_t slice; int8_t lane, rev; uint8_t n; float rate, lvl; } beat_trig_t;
extern beat_trig_t beat_tlog[16384];
extern int beat_tlog_n;
uint32_t beat_test_cell(int k);
void sim_menu_close(void);
void wr_prof(float *us, int n);
int  wr_zone(void);
bool wr_stopping(void);
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);

void inst_excite(void) { }                                       // it plays by itself
static float noisy_mic(float t, void *u) { (void)u; return 0.2f * sinf(6.2831853f * 180.0f * t) * (fmodf(t, 0.25f) < 0.05f ? 1.0f : 0.05f); }

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); }
static void stop_go(void) { int p = param_by_tag("xsto"); if (p >= 0 && INST.action) INST.action(p); }   // WRECK > STOP / GO
static int bar_now(void) { grid_t g; wr_grid_get(&g); return wr_bar_index(&g, audio_frames()); }
static void to_bar(int k) { for (int i = 0; i < 2000 && bar_now() != k; i++) sim_tick(); }   // run to the start of bar k of the cycle

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
    section("wreck: STOP / GO, and REC tapped (the catch)");
    quiet(); set("xaxs", 10); sim_run(1.0f);
    stop_go(); sim_run(2.5f);
    float off = m_rms(sim_t - 0.5f, sim_t);
    bool st = perf.running;
    stop_go(); sim_run(1.5f);
    float on = m_rms(sim_t - 0.8f, sim_t);
    check(!st && off < 0.1f * on && on > 0.01f, "STOP / GO stops the machine (rms %.4f), again it starts (%.3f)", (double)off, (double)on);
    // the catch: a bar with the stick up (a roll) and the dice, tapped: the grid has it and plays
    sim_run(1.0f);
    sim_tilt(40, 0); SI.jy = 0.9f; sim_run(1.6f);                    // more than a bar at x2 (1.2 s)
    sim_rec(0.1f);
    SI.jy = 0; sim_tilt(0, 0);
    bool caught = perf.caught, grid = beat_grid_on();
    int rolls = 0, picked = 0;
    for (int k = 0; k < 16; k++) { uint32_t c = beat_test_cell(k); int t = (int)((c >> 6) & 15); if (t >= 2 && t <= 7) rolls++; if ((c & 63) >= 2) picked++; }
    sim_run(1.5f);
    float r_loop = m_rms(sim_t - 1.0f, sim_t);
    sim_rec(0.1f); sim_run(0.5f);
    bool let_go = !perf.caught && !beat_grid_on();
    check(caught && grid && picked >= 8 && rolls >= 1, "REC tapped: the last bar is caught into the grid (%d steps with a slice, %d rolled) and the grid plays", picked, rolls);
    check(r_loop > 0.01f && let_go, "caught it plays on (rms %.3f); tapped again it lets go: the break as it is", (double)r_loop);
    // REC held on and on: the take ends by itself at 8 s, and REC does nothing more until it is let go
    SI.mic = noisy_mic;
    SI.rec = true; sim_run(9.0f);
    bool still = kit_recording();
    sim_run(1.0f);
    bool again = kit_recording();
    SI.rec = false; quiet(); sim_run(3.0f);
    check(!still && !again && perf.running, "REC held for 9 s: the take ends by itself at 8 s and nothing starts again while it is held");
}

// REC down from power-on (a stuck pin, a finger on BOOT while plugging in): nothing is sampled, the machine runs on;
// let go, and a hold samples as it should
void wr_test_rec_as_at_boot(void);
static void rec_at_boot(void)
{
    section("wreck: REC held from power-on");
    quiet(); front_midi(0xC0, 0, 0); set("xaxs", 10); sim_run(1.5f);
    const brk_t *b0 = kit_break(); uint32_t s0 = b0->serial;
    SI.mic = noisy_mic;
    wr_test_rec_as_at_boot(); SI.rec = true;
    bool armed = false;
    for (int i = 0; i < 300; i++) { sim_tick(); if (kit_recording()) armed = true; }
    bool running = perf.running && !wr_stopping();
    SI.rec = false; sim_run(0.5f);
    check(!armed && running && kit_break()->serial == s0, "REC down for 3 s from power-on: no take, no stop, the break stays (%s)", kit_break()->name);
    SI.rec = true; sim_run(1.5f);
    bool took = kit_recording();
    SI.rec = false; sim_run(2.0f);
    check(took && kit_break()->serial != s0 && !strcmp(kit_break()->name, "SAMPLED"), "let go and held again: it samples (%s)", kit_break()->name);
    quiet(); front_midi(0xC0, 0, 0); sim_run(0.5f);
}

int wr_knocks(void);
static void knocks(void)
{
    section("wreck: the hands and knocks");
    quiet(); set("xaxs", 10); set("xkno", 1); sim_run(0.5f);
    sim_tilt(0, 35); sim_run(0.8f);
    float fast = perf.speed; int qf = perf.speed_q;
    sim_tilt(0, -35); sim_run(0.8f);
    float slow = perf.speed; int qs = perf.speed_q;
    sim_tilt(-35, 0); sim_run(0.5f);
    float sm = perf.smear;
    sim_tilt(35, 0); sim_run(0.5f);
    float ch = perf.chaos;
    sim_tilt(0, 0); sim_run(0.8f);
    check(fast > 1.5f && qf == 1 && slow < 0.4f && qs == -2 && sm > 0.5f && ch > 0.5f && perf.chaos == 0.0f && perf.speed == 1.0f && perf.speed_q == 0,
          "tipped forward: speed %.2f (a step up), back: %.2f (two down); rolled left: smear %.2f, right: chaos %.2f; flat: x1, none", (double)fast, (double)slow, (double)sm, (double)ch);
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

// ---------------------------------------------------------------- the tilt: the break's speed in whole steps on the beat
static void speed(void)
{
    section("wreck: the tilt is the speed");
    quiet(); set("xaxs", 10); set("xchg", 1); set("xtlt", 0); sim_run(1.0f);
    grid_t g0; wr_grid_get(&g0);
    sim_tilt(0, -35); sim_run(1.5f);
    grid_t g1; wr_grid_get(&g1);
    float bend_back = perf.bend;
    sim_tilt(0, 35); sim_run(1.5f);
    grid_t g2; wr_grid_get(&g2);
    sim_tilt(0, 0); sim_run(1.5f);
    grid_t g3; wr_grid_get(&g3);
    check(g0.feel_q == 1 && g1.feel_q == -1 && g2.feel_q == 2 && g3.feel_q == 1 && fabsf(bend_back - 1.0f) < 0.05f,
          "at BREAKCORE (x2) tipped back the machine runs at x1/2 (feel %d), forward at x4 (%d), flat x2 again (%d); a whole step: no bend (%.2f)",
          g1.feel_q, g2.feel_q, g3.feel_q, (double)bend_back);
    sim_tilt(0, -10); sim_run(1.5f);                                // between the steps: the break bends
    grid_t g4; wr_grid_get(&g4);
    float bend = perf.bend;
    sim_tilt(0, 0); sim_run(1.0f);
    check(g4.feel_q == 1 && bend < 0.95f && bend > 0.7f, "tipped a little: the grid stays (feel %d) and the break is bent slower (%.2f)", g4.feel_q, (double)bend);
    check(m_bad() == 0, "no NaN through the speed changes");
}

// ---------------------------------------------------------------- the bars: one treatment per bar of the cycle
static void bars(void)
{
    section("wreck: the bars");
    quiet(); set("xaxs", 10); set("xchg", 1); set("mamp", 0); sim_run(0.5f);
    int pb = tag("xbar");
    param_set(pb, (float)((TR_OUT << 3) | (TR_REV << 6) | (TR_CRUSH << 9) | (TR_HALF << 12)));   // bar 2 OUT, 3 REV, 4 CRUSH, 5 HALF
    sim_run(0.5f);
    float rms[8] = { 0 }, rate[8] = { 0 }; int rev[8] = { 0 }, hits[8] = { 0 }, half = 0;
    to_bar(0);
    int b0 = beat_tlog_n;
    for (int k = 0; k < 6; k++) {
        float t0 = sim_t;
        int h0 = beat_tlog_n;
        while (bar_now() == k) sim_tick();
        rms[k] = m_rms(t0, sim_t);
        for (int e = h0; e < beat_tlog_n && e < 16384; e++) { hits[k]++; if (beat_tlog[e].rev) rev[k]++; rate[k] += fabsf(beat_tlog[e].rate); }
        if (hits[k]) rate[k] /= (float)hits[k];
        if (k == 4) for (int e = h0; e < beat_tlog_n && e < 16384; e++) if (fabsf(beat_tlog[e].rate) < 0.6f * rate[0]) half++;
    }
    (void)b0;
    beat_view_t bv; beat_view(&bv);
    check(rms[1] < 0.15f * rms[0] && rms[2] > 0.5f * rms[0], "bar 2 DRUMS OUT is quiet (rms %.4f against %.3f), bar 3 plays (%.3f)", (double)rms[1], (double)rms[0], (double)rms[2]);
    check(hits[2] > 4 && rev[2] >= hits[2] * 9 / 10 && rev[0] == 0, "bar 3 REVERSED: every slice backwards (%d of %d; bar 1: %d)", rev[2], hits[2], rev[0]);
    check(hits[4] > 2 && hits[4] < hits[0] && half >= hits[4] - 1, "bar 5 HALF SPEED: fewer hits (%d against %d), %d of them at half the rate (%.2f)", hits[4], hits[0], half, (double)rate[0]);
    param_set(pb, 0); set("mamp", 70); sim_run(0.5f);
    check(m_bad() == 0, "no NaN through the bars");
}

// ---------------------------------------------------------------- your riff: scale steps from the chord's root
static void riff(void)
{
    section("wreck: your riff");
    quiet(); set("xaxs", 62); set("xchg", 1); set("anot", 1); set("adrn", 0); set("mdrm", 0); sim_run(0.5f);
    int p0 = tag("xrf0"), p1 = tag("xrf1");
    uint32_t root = 2 + RIFF_LOW, fifth = root + 4, oct = root + 7;      // eighths: root . . . a fifth . an octave hold
    param_set(p0, (float)(root | (RIFF_HOLD << 6)));
    param_set(p1, (float)(fifth | (RIFF_REST << 6) | (oct << 12) | (RIFF_HOLD << 18)));
    sim_run(1.0f);
    int seen[8]; for (int k = 0; k < 8; k++) seen[k] = -1;
    float t0 = sim_t;
    while (sim_t - t0 < 6.0f) {
        sim_tick();
        beat_view_t bv; beat_view(&bv);
        amp_view_t av; amp_view(&av);
        int e = (bv.step / 2) & 7;
        if (av.note >= 0) seen[e] = av.note;
    }
    int d5 = seen[4] >= 0 && seen[0] >= 0 ? ((seen[4] - seen[0]) % 12 + 12) % 12 : -1;
    int d8 = seen[6] >= 0 && seen[0] >= 0 ? ((seen[6] - seen[0]) % 12 + 12) % 12 : -1;
    check(seen[0] >= 0 && d5 == 7 && d8 == 0 && seen[6] > seen[0], "NOTES: RIFF at SLUDGE: the root (%d), four scale steps up is the fifth (%d), seven an octave (%d)", seen[0], seen[4], seen[6]);
    int key0 = seen[0];
    set("skey", 2); sim_run(3.0f);                                    // the flock's key moves: the riff with it
    int k2 = -1;
    t0 = sim_t;
    while (sim_t - t0 < 3.0f) { sim_tick(); beat_view_t bv; beat_view(&bv); amp_view_t av; amp_view(&av); if (((bv.step / 2) & 7) == 0 && av.note >= 0) k2 = av.note; }
    check(k2 >= 0 && ((k2 - key0) % 12 + 12) % 12 == 5, "a new key (A to D): the riff's root moved with it (%d -> %d)", key0, k2);
    set("skey", 9); set("anot", 2); set("adrn", 2); set("mdrm", 85); param_set(p0, 0); param_set(p1, 0); set("xaxs", 10); sim_run(0.5f);
}

// ---------------------------------------------------------------- screens: the grid's three pages, the catch
static void screens(void)
{
    section("wreck: screens");
    quiet(); set("xaxs", 10); set("xchg", 1); sim_run(1.0f);
    int pb = tag("xbar");
    param_set(pb, (float)((TR_CRUSH << 3) | (TR_REV << 9) | (TR_STOP << 21)));
    int p0 = tag("xrf0"), p1 = tag("xrf1");
    param_set(p0, (float)((2 + RIFF_LOW) | ((2 + RIFF_LOW + 2) << 6) | (RIFF_HOLD << 12) | ((2 + RIFF_LOW + 4) << 18)));
    param_set(p1, (float)(RIFF_REST | ((2 + RIFF_LOW - 3) << 6) | ((2 + RIFF_LOW + 7) << 12) | (RIFF_HOLD << 18)));
    sim_run(1.5f);
    sim_shot("bars_strip");
    sim_tilt(40, 0); SI.jy = 0.9f; sim_run(1.6f); sim_rec(0.1f); SI.jy = 0; sim_tilt(0, 0); sim_run(0.6f);
    sim_shot("caught");
    sim_gif_begin("grid-pages");
    sim_hold_knob(); sim_run(1.2f);
    sim_shot("grid_drums");
    for (int i = 0; i < 16; i++) { sim_turn(1); sim_run(0.08f); }
    sim_run(0.8f); sim_shot("grid_riff");
    for (int i = 0; i < 8; i++) { sim_turn(1); sim_run(0.08f); }
    sim_run(0.8f); sim_shot("grid_bars");
    sim_hold_knob(); sim_run(0.5f);
    sim_gif_end();
    sim_rec(0.1f); sim_run(0.3f);
    sim_tilt(0, -35); sim_run(1.5f); sim_shot("tilt_slow"); sim_tilt(0, 0); sim_run(1.0f);
    param_set(pb, 0); param_set(p0, 0); param_set(p1, 0); sim_run(0.3f);
    check(m_bad() == 0, "the screens drawn: the bars strip, the catch, the grid's three pages, the tilt");
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

// ---------------------------------------------------------------- a long time in each zone
// Each zone for WRECK_SOAK seconds (45 unless the environment says otherwise; 3600 is an hour each, about 20 minutes
// on the host) with everything happening at once: the knob swept within the zone, a sound every bar, the tempo moved
// from a linked board every few seconds, takes back to back, the hands and knocks now and then, a stop and a start.
// At BREAKCORE the song's beat count passes its wrap (SONG_WRAP_BEATS) early in the run. After each zone: no NaN,
// under full scale, still playing, in the zone it was left in at the feel it should have.
static void soak(void)
{
    section("wreck: a long time in each zone");
    const char *e = getenv("WRECK_SOAK");
    float per = e ? (float)atof(e) : 45.0f;
    if (per < 10.0f) per = 10.0f;
    quiet(); set("xchg", 0); set("xkno", 1); set("stmp", 100); front_midi(0xC0, 0, 0); set("xaxs", 10); sim_run(0.5f);
    int k = sim_peer_add("SING", 2, -20.0, 3.0e9);                // a board that leads the tempo
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 0); }
    sim_peer_use(-1);
    sim_run(1.0f);
    // the wrap: beat 0 moved back so the count passes SONG_WRAP_BEATS in 8 beats
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(song.fpb, o - (uint32_t)(SONG_WRAP_BEATS - 8) * song.fpb, 4, 0); }
    sim_peer_use(-1);
    sim_run(0.3f);
    uint32_t b_before = (uint32_t)wr_mod((int32_t)(audio_frames() - song.origin), (int32_t)(song.fpb * SONG_WRAP_BEATS)) / song.fpb;
    sim_run(6.0f);
    uint32_t b_after = (uint32_t)wr_mod((int32_t)(audio_frames() - song.origin), (int32_t)(song.fpb * SONG_WRAP_BEATS)) / song.fpb;
    float r_wrap = m_rms(sim_t - 2.0f, sim_t);
    check(b_before >= (uint32_t)SONG_WRAP_BEATS - 8 && b_after < 8 && r_wrap > 0.01f && perf.running && m_bad() == 0,
          "the song's beat count wraps (beat %u -> %u) and the machine plays on (rms %.3f)", b_before, b_after, (double)r_wrap);
    static const float lo[4] = { 2, 28, 53, 78 }, hi[4] = { 22, 47, 72, 98 };
    static const int want_q[4] = { 1, 0, -1, -2 };
    int ok = 0;
    for (int z = 0; z < 4; z++) {
        float t0 = sim_t, pk = 0, worst_rms_gap = 0; int quiet_runs = 0;
        set("xaxs", (lo[z] + hi[z]) * 0.5f); sim_run(2.0f);
        int step = 0, sound = 0, takes = 0, tempos = 0;
        while (sim_t - t0 < per) {
            // the knob: a slow sweep within the zone, 0.3 s a step
            float ph = (float)(step % 40) / 40.0f, a = lo[z] + (hi[z] - lo[z]) * (ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f);
            set("xaxs", a);
            if (step % 8 == 0) front_midi(0xC0, (uint8_t)(sound++ % INST.npresets), 0);   // a sound about every bar
            if (step % 23 == 0) {                                   // the tempo from the linked board
                tempos++;
                uint32_t fpb = 60u * AUDIO_SR / (uint32_t)(90 + (tempos * 17) % 50);
                sim_peer_use(k);
                uint32_t o; float f; link_origin(&o, &f); link_set_time(fpb, o, 4, 0);
                sim_peer_use(-1);
            }
            if (step % 11 == 3) sim_tilt((float)((step % 3) - 1) * 30.0f, (float)((step % 5) - 2) * 15.0f);
            if (step % 17 == 5) { SI.jx = (float)((step % 3) - 1); SI.jy = (float)((step % 5) - 2) * 0.5f; }
            if (step % 29 == 7) knock(3.0f);
            if (step % 50 == 20) { SI.mic = noisy_mic; sim_rec(1.2f); sim_rec(1.6f); SI.mic = NULL; takes += 2; }   // back to back
            if (step % 97 == 40) { stop_go(); sim_run(1.5f); stop_go(); }   // a stop and a start
            if (step % 41 == 9) sim_rec(0.1f);                          // a catch, or letting go
            sim_run(0.3f);
            float r = m_rms(sim_t - 0.3f, sim_t), p = m_peak(sim_t - 0.3f, sim_t);
            if (p > pk) pk = p;
            if (r < 0.002f) quiet_runs++; else quiet_runs = 0;
            if ((float)quiet_runs > worst_rms_gap) worst_rms_gap = (float)quiet_runs;
            step++;
        }
        quiet(); sim_tilt(0, 0); SI.jx = SI.jy = 0;
        front_midi(0xC0, 0, 0);                                     // (sounds set TEMPO STEPS: WRECK's is x2-x1/4)
        set("xaxs", (lo[z] + hi[z]) * 0.5f); sim_run(3.0f);
        grid_t g; wr_grid_get(&g);
        float r = m_rms(sim_t - 2.0f, sim_t);
        bool good = m_bad() == 0 && pk < 0.99f && perf.running && !wr_stopping() && wr_zone() == z && g.feel_q == want_q[z] && r > 0.005f;
        if (good) ok++;
        note("%s for %.0f s: %d sounds, %d takes, %d tempos; peak %.3f, longest quiet %.1f s; now zone %d feel %d rms %.3f%s",
             host_zone_name(z), (double)(sim_t - t0), sound, takes, tempos, (double)pk, (double)(worst_rms_gap * 0.3f), wr_zone(), g.feel_q, (double)r, good ? "" : " BAD");
    }
    check(ok == 4, "%.0f s in each zone with everything going: no NaN, under full scale, still playing, the right feel (%d of 4)", (double)per, ok);
    sim_peer_power(k, false);
    quiet(); set("stmp", 100); front_midi(0xC0, 0, 0); set("xaxs", 10); sim_run(2.0f);
}

void inst_tests(void)
{
    plays();
    zones();
    rec_tap();
    rec_at_boot();
    knocks();
    speed();
    bars();
    riff();
    screens();
    with_band();
    sounds();
    section("kit"); quiet(); set("xaxs", 10); sim_run(0.5f); test_mod_kit();
    section("beat"); quiet(); set("xaxs", 10); sim_run(0.5f); test_mod_beat();
    section("amp"); quiet(); set("xaxs", 10); sim_run(0.5f); test_mod_amp();
    section("fx"); quiet(); set("xaxs", 10); sim_run(0.5f); test_mod_fx();
    sweep();
    soak();
    cost();
}
