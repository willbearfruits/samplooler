// JAM's own checks: the host's (the layers, the song, the tracks, the mixer, the FX, the cost), then each
// layer's (test_<layer>.c, written by the layer's builder; a layer without one yet runs the empty default).
#include "sim.h"
#include "jam.h"
#include <math.h>
#include <string.h>
#include <time.h>

const char *const inst_click_skip[] = { NULL };

__attribute__((weak)) void test_layer_drums(void) { }
__attribute__((weak)) void test_layer_bass(void) { }
__attribute__((weak)) void test_layer_breath(void) { }
__attribute__((weak)) void test_layer_howl(void) { }
__attribute__((weak)) void excite_drums(void) { }
__attribute__((weak)) void excite_bass(void) { }
__attribute__((weak)) void excite_breath(void) { }
__attribute__((weak)) void excite_howl(void) { }
bool sim_menu_param(int p);
void sim_menu_close(void);

void inst_excite(void)
{
    switch (host_live()) { case L_DRUMS: excite_drums(); break; case L_BASS: excite_bass(); break;
                           case L_BREATH: excite_breath(); break; default: excite_howl(); break; }
}

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static void make_live(int l) { for (int i = 0; i < 12 && host_live() != l; i++) sim_turn(host_live() < l ? 1 : -1); sim_run(0.1f); }
static void run_action(const char *t) { int p = tag(t); if (p >= 0 && sim_menu_param(p)) { sim_tap(); sim_run(0.1f); sim_menu_close(); } }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); }

// a steady tone into the mic: whatever a layer makes of it, it is not silence
static float tone_mic(float t, void *u) { (void)u; return 0.08f * sinf(6.2831853f * 220.0f * t) + 0.02f * sim_noise(); }

static float corr(int a, int b, int n)
{
    double c = 0, e0 = 0, e1 = 0;
    for (int i = 0; i < n; i++) { float x0 = sim_out[a + i], x1 = sim_out[b + i]; c += x0 * x1; e0 += x0 * x0; e1 += x1 * x1; }
    return e0 * e1 > 1e-12 ? (float)(c / sqrt(e0 * e1)) : 0.0f;
}

static void host_layers(void)
{
    section("jam: layers");
    sim_gif_begin("layers");
    for (int l = 0; l < L_N; l++) {
        make_live(l);
        sim_run(0.5f);
        char b[32]; snprintf(b, sizeof b, "layer_%d", l); sim_shot(b);
    }
    check(host_live() == L_N - 1, "turning the knob in play walks the layers (now %d)", host_live());
    sim_turn(-1); sim_run(0.08f); sim_shot("slide");
    sim_run(0.3f);
    sim_gif_end();
    check(host_live() == L_N - 2, "and back (now %d)", host_live());
    // editing a setting in the menu must not take the knob away from the layers
    int p = tag("fech");
    if (p >= 0 && sim_menu_param(p)) { sim_tap(); sim_turn(1); sim_tap(); sim_menu_close(); }
    int before = host_live();
    sim_turn(-1); sim_run(0.1f);
    check(host_live() == before - 1, "after editing a setting in the menu the knob still picks the layer");
    make_live(L_DRUMS);
}

static void host_song(void)
{
    section("jam: song");
    set("sprg", 1);                                               // I V vi IV
    set("stmp", 120); set("skey", 0); set("sscl", 0); set("sbar", 0);
    int want[4] = { 0, 4, 5, 3 }, seen[4] = { -1, -1, -1, -1 };
    for (int b = 0; b < 4; b++) {
        while ((int)(song.bar % 4) != b) sim_run(0.05f);
        sim_run(0.1f);
        seen[b] = song.degree;
    }
    check(seen[0] == want[0] && seen[1] == want[1] && seen[2] == want[2] && seen[3] == want[3],
          "CHORDS I V vi IV: the chord steps per bar (%d %d %d %d)", seen[0], seen[1], seen[2], seen[3]);
    check(song.chord[0] >= 36 && song.chord[0] <= 47, "the chord's root sits in C2..B2 (%d)", song.chord[0]);
    host_chord_override(3); sim_run(0.05f);
    int o = song.degree;
    host_chord_override(-1); sim_run(0.05f);
    check(o == 3, "a layer can set the chord live (IV: %d), and give it back", o);
    set("sprg", 0); set("schd", 0); set("stmp", 100);
}

static void host_tracks(void)
{
    section("jam: tracks");
    run_action("lcla");
    check(song.loop_len == 0, "CLEAR ALL: no loop");
    // the jam's first loop, made on BREATH (whatever it makes of a tone in the mic is recorded)
    make_live(L_BREATH);
    excite_breath();
    if (!SI.mic) SI.mic = tone_mic;
    sim_run(0.5f);
    float t0 = sim_t;
    SI.rec = true; sim_run(2.4f); SI.rec = false; sim_tick();
    float len = (float)song.loop_len / AUDIO_SR;
    check(song.loop_len && fabsf(len - 2.4f) < 0.03f, "REC held 2.4 s on BREATH: the jam's loop is 2.4 s (%.3f s)", (double)len);
    check(song.beats >= 1 && song.bpm >= 75.0f && song.bpm <= 165.0f, "the tempo comes from it: %d beats at %.1f BPM", song.beats, (double)song.bpm);
    float take = m_rms(t0 + 0.4f, t0 + 2.3f);
    // BREATH goes quiet once DRUMS is live: what is heard now is the track
    quiet();
    make_live(L_DRUMS);
    sim_run(len + 0.5f);
    float a0 = sim_t;
    sim_run(2.0f * len);
    float played = m_rms(a0, a0 + len);
    int L = (int)(song.loop_len), s0 = (int)(a0 * AUDIO_SR);
    float c = corr(s0, s0 + L, L - 64);
    check(take < 0.002f || (played > 0.3f * take && c > 0.95f), "the BREATH track plays it back, pass after pass (rms %.3f of %.3f, pass to pass %.3f)", (double)played, (double)take, (double)c);
    sim_shot("track_playing");
    // a tap on REC with BREATH live mutes its track; another brings it back
    make_live(L_BREATH); quiet();
    sim_run(0.5f);
    sim_rec(0.1f); sim_run(2.5f);                                   // its echo and reverb die away too
    float muted = m_rms(sim_t - 0.3f, sim_t);
    sim_rec(0.1f); sim_run(0.5f);
    float back = m_rms(sim_t - 0.3f, sim_t);
    check(take < 0.002f || (muted < 0.1f * back && back > 0.2f * take), "a tap mutes the BREATH track (rms %.4f), another brings it back (%.3f)", (double)muted, (double)back);
    // adding to it: HOWL records its own track in the same loop
    make_live(L_HOWL);
    excite_howl();
    if (!SI.mic) SI.mic = tone_mic;
    sim_run(0.5f);
    SI.rec = true; sim_run(1.2f); SI.rec = false; sim_tick();
    check(song.loop_len && fabsf((float)song.loop_len / AUDIO_SR - len) < 0.001f, "a later take keeps the loop's length (%.3f s)", (double)((float)song.loop_len / AUDIO_SR));
    quiet(); make_live(L_DRUMS); sim_run(len);
    sim_shot("two_tracks");
    // clear one, then all
    run_action("lclh"); sim_run(0.5f);
    run_action("lclw"); sim_run(0.5f);
    float a1 = sim_t; sim_run(len);
    check(m_rms(a1, sim_t) < 0.004f, "CLEAR BREATH and CLEAR HOWL: silence (rms %.4f)", (double)m_rms(a1, sim_t));
    run_action("lcla");
    check(song.loop_len == 0 && song.beats == 0, "CLEAR ALL: no loop, the tempo free again");
}

static void host_mix_fx(void)
{
    section("jam: mixer and FX");
    // a drum hit through the echo: repeats every 3/4 of a beat (ECHO TIME 1/8 DOT) at 100 BPM
    make_live(L_DRUMS); quiet();
    set("stmp", 100); set("fetm", 1); set("fech", 80); set("fefb", 50); set("frev", 0);
    set("dsnd", 100);
    sim_run(1.0f);
    float th = sim_t;                                               // the hit lands at the next block
    if (layer_drums.midi_in) layer_drums.midi_in(0x90, 37, 120);  // a short hit
    sim_run(1.6f);
    float d = 0.75f * 60.0f / 100.0f;
    float e1 = m_peak(th + d - 0.005f, th + d + 0.03f), mid = m_peak(th + 0.25f, th + d - 0.05f);
    check(e1 > 2.0f * mid && e1 > 0.01f, "ECHO in time: the hit comes back 3/4 of a beat later (%.3f against %.3f between)", (double)e1, (double)mid);
    set("fech", 35); set("fefb", 40); set("frev", 35); set("dsnd", 15);
    // the mixer: a level to 0 and a mute silence the drums
    float tp = sim_t + 0.02f;
    if (layer_drums.midi_in) layer_drums.midi_in(0x90, 36, 120);
    sim_run(0.4f);
    float loud = m_peak(tp, tp + 0.3f);
    set("mmud", 1); sim_run(2.5f);                                  // the echo and reverb of the first hit die away
    tp = sim_t + 0.02f;
    if (layer_drums.midi_in) layer_drums.midi_in(0x90, 36, 120);
    sim_run(0.4f);
    float mute = m_peak(tp, tp + 0.3f);
    set("mmud", 0);
    check(loud > 0.05f && mute < 0.02f * loud + 0.001f, "MIX: the drums muted (peak %.4f, %.3f before)", (double)mute, (double)loud);
}

static void host_cost(void)
{
    section("jam: cost");
    // the busiest jam: a drums loop, the bass, BREATH live with its track, the HOWL track, the FX
    make_live(L_DRUMS); quiet();
    excite_drums();
    SI.rec = true; sim_run(2.0f); SI.rec = false; sim_tick();
    excite_bass();
    make_live(L_BASS); SI.rec = true; sim_run(1.0f); SI.rec = false; sim_tick();
    make_live(L_HOWL); excite_howl(); if (!SI.mic) SI.mic = tone_mic;
    SI.rec = true; sim_run(1.0f); SI.rec = false; sim_tick();
    make_live(L_BREATH); excite_breath(); if (!SI.mic) SI.mic = tone_mic;
    SI.rec = true; sim_run(1.0f); SI.rec = false; sim_tick();
    sim_run(0.5f);
    float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
    struct timespec a, b;
    double tsum = 0; int nblk = 3000;
    float tt = sim_t;
    for (int k = 0; k < nblk; k++) {
        for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = SI.mic ? SI.mic(tt + (float)i / (float)AUDIO_SR, SI.mic_u) : 0.0f;
        tt += (float)AUDIO_BLOCK / (float)AUDIO_SR;
        clock_gettime(CLOCK_MONOTONIC, &a);
        front_audio(in, out, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &b);
        tsum += (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
    }
    double us = tsum / nblk;
    metric("jam_us_per_block", us);
    check(us < 25.0, "the whole jam takes %.1f us per 64-frame block at its busiest (budget 25)", us);
    sim_gif_begin("jam");
    for (int l = L_N - 1; l >= 0; l--) { make_live(l); quiet(); sim_run(1.2f); }
    sim_shot("jam_playing");
    sim_gif_end();
    run_action("lcla"); quiet();
    sim_run(0.5f);
}

static void fresh(void) { quiet(); make_live(L_DRUMS); run_action("lcla"); sim_run(0.3f); }

void inst_tests(void)
{
    host_layers();
    host_song();
    host_tracks();
    host_mix_fx();
    // each layer's own: it starts with no loop, DRUMS live, the mic off, the stick there
    section("drums"); fresh(); test_layer_drums();
    section("bass"); fresh(); test_layer_bass();
    section("breath"); fresh(); test_layer_breath();
    section("howl"); fresh(); test_layer_howl();
    fresh();
    host_cost();
}
