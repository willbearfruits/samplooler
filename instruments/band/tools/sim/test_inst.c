// JAM's own checks: the host's (the layers, the song, the tracks, the mixer, the FX, the cost), then each
// layer's (test_<layer>.c, written by the layer's builder; a layer without one yet runs the empty default).
#include "sim.h"
#include "band.h"
#include "link.h"
#include <math.h>
#include <string.h>
#include <time.h>

// the link's settings and FIRST LOOP change no sound (a drum hit landing on their change is all the check could hear);
// LEAD's RANGE only picks the octaves of the arpeggio's next notes: what the check can catch is a new note an octave up
const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", "sfst", "lrng", NULL };

__attribute__((weak)) void test_layer_drums(void) { }
__attribute__((weak)) void test_layer_bass(void) { }
__attribute__((weak)) void test_layer_breath(void) { }
__attribute__((weak)) void test_layer_howl(void) { }
__attribute__((weak)) void test_layer_lead(void) { }
__attribute__((weak)) void test_layer_keys(void) { }
__attribute__((weak)) void excite_lead(void) { }
__attribute__((weak)) void excite_keys(void) { }
__attribute__((weak)) void excite_drums(void) { }
__attribute__((weak)) void excite_bass(void) { }
__attribute__((weak)) void excite_breath(void) { }
__attribute__((weak)) void excite_howl(void) { }
bool sim_menu_param(int p);
void sim_menu_close(void);
int  mixer_cursor(void);
bool mixer_grabbed(void);
int  host_layer_state(int l);

void inst_excite(void)
{
    switch (host_live()) { case L_DRUMS: excite_drums(); break; case L_BASS: excite_bass(); break;
                           case L_LEAD: excite_lead(); break; case L_KEYS: excite_keys(); break;
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
    sim_run(0.05f);                                               // the new tempo moves the beat: let the song catch up
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
    run_action("xcla");
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
    // muted, then REC held: a new take over the old one (here a silent one: the old take is gone)
    sim_rec(0.1f); sim_run(0.3f);
    int st_m = host_layer_state(L_BREATH);
    SI.rec = true; sim_run(0.8f); SI.rec = false; sim_tick();
    make_live(L_DRUMS); sim_run(len + 0.3f);
    float r0 = sim_t; sim_run(len);
    float gone = m_rms(r0, sim_t);
    check(st_m == LOOP_OFF && host_layer_state(L_BREATH) == LOOP_ON && (take < 0.002f || gone < 0.02f * take + 0.0005f),
          "muted, then REC held: a new take replaces the old one (now rms %.4f, the old %.3f)", (double)gone, (double)take);
    // and a new take with sound in it plays
    make_live(L_BREATH); sim_rec(0.1f); sim_run(0.2f);
    excite_breath(); if (!SI.mic) SI.mic = tone_mic;
    SI.rec = true; sim_run(len + 0.3f); SI.rec = false; sim_tick();
    quiet(); make_live(L_DRUMS); sim_run(len + 0.3f);
    r0 = sim_t; sim_run(len);
    float again2 = m_rms(r0, sim_t);
    check(take < 0.002f || again2 > 0.3f * take, "the new take plays, pass after pass (rms %.3f)", (double)again2);
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
    run_action("xclh"); sim_run(0.5f);
    run_action("xclw"); sim_run(0.5f);
    float a1 = sim_t; sim_run(len);
    check(m_rms(a1, sim_t) < 0.004f, "CLEAR BREATH and CLEAR HOWL: silence (rms %.4f)", (double)m_rms(a1, sim_t));
    run_action("xcla");
    check(song.loop_len == 0 && song.beats == 0, "CLEAR ALL: no loop, the tempo free again");
    // the silence before a take's first sound is cut: REC held over a second of nothing, then 2.3 s of BREATH
    make_live(L_BREATH); quiet(); sim_run(1.0f);
    SI.rec = true; sim_run(1.0f);
    excite_breath(); if (!SI.mic) SI.mic = tone_mic;
    sim_run(2.3f); SI.rec = false; sim_tick();
    quiet(); sim_run(0.1f);
    make_live(L_DRUMS); sim_run(2.0f * (float)song.loop_len / AUDIO_SR);
    float half = (float)song.loop_len / AUDIO_SR * 0.5f, h1 = m_rms(sim_t - 2.0f * half, sim_t - half), h2 = m_rms(sim_t - half, sim_t);
    check(song.beats == 4 && h1 > 0.01f && h2 > 0.01f, "a take begun in silence starts at its sound: one bar, sound all through it (rms %.3f, %.3f)", (double)h1, (double)h2);
    run_action("xcla"); quiet(); sim_run(0.5f);
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

// ---------------------------------------------------------------- the mixer: the knob's place past HOWL
static float P_(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : -1.0f; }
static void fresh(void);
static void host_mixer(void)
{
    section("jam: the mixer screen");
    fresh();
    // two tracks to erase: BREATH and HOWL
    make_live(L_BREATH); excite_breath(); if (!SI.mic) SI.mic = tone_mic;
    SI.rec = true; sim_run(2.4f); SI.rec = false; sim_tick();
    make_live(L_HOWL); excite_howl(); if (!SI.mic) SI.mic = tone_mic;
    SI.rec = true; sim_run(1.0f); SI.rec = false; sim_tick();
    quiet();
    make_live(L_HOWL);
    sim_gif_begin("mixer");
    sim_turn(1); sim_run(0.6f);
    check(host_live() == L_N, "the knob turned past HOWL: the mixer (%d)", host_live());
    sim_shot("mixer");
    // the stick: right picks the next strip, up turns it up
    int c0 = mixer_cursor();
    SI.jx = 1.0f; sim_run(0.05f); SI.jx = 0; sim_run(0.1f);
    check(mixer_cursor() == c0 + 1, "the stick right: the next strip (%d -> %d)", c0, mixer_cursor());
    while (mixer_cursor() != L_BASS) { SI.jx = mixer_cursor() < L_BASS ? 1.0f : -1.0f; sim_run(0.05f); SI.jx = 0; sim_run(0.1f); }
    float b0 = P_("mlvb");
    SI.jy = 1.0f; sim_run(0.05f); SI.jy = 0; sim_run(0.1f);
    float b1 = P_("mlvb");
    SI.jy = -1.0f; sim_run(1.0f); SI.jy = 0; sim_run(0.3f);
    float b2 = P_("mlvb");
    check(b1 == b0 + 5.0f && b2 < b1 - 30.0f, "the stick up: BASS %.0f -> %.0f; held down: %.0f", (double)b0, (double)b1, (double)b2);
    // REC: a tap mutes the strip, another brings it back
    sim_rec(0.1f); sim_run(0.1f);
    float m1 = P_("mmub");
    sim_shot("mixer_mute");
    sim_rec(0.1f); sim_run(0.1f);
    check(m1 == 1.0f && P_("mmub") == 0.0f, "tap REC: BASS muted, again: on");
    // the knob held: it is the mixer's
    sim_hold_knob(); sim_run(0.2f);
    bool g = mixer_grabbed();
    sim_turn(1); sim_run(0.1f);
    int cg = mixer_cursor();
    float l0 = P_("mlvl");
    SI.pressed = true; sim_run(0.05f); SI.enc = -2; sim_tick(); sim_run(0.05f); SI.pressed = false; sim_tick(); sim_run(0.1f);
    float l1 = P_("mlvl");
    sim_shot("mixer_knob");
    check(g && cg == L_LEAD && l1 == l0 - 10.0f && mixer_cursor() == L_LEAD,
          "the knob held: turn picks LEAD (%d), press and turn sets its level (%.0f -> %.0f)", cg, (double)l0, (double)l1);
    sim_tap(); sim_run(0.1f);
    float lm = P_("mmul");
    sim_tap(); sim_run(0.1f);
    check(lm == 1.0f && P_("mmul") == 0.0f, "a tap on the knob mutes LEAD, another brings it back");
    sim_hold_knob(); sim_run(0.2f);
    bool freed = !mixer_grabbed();
    sim_turn(-1); sim_run(0.5f);
    check(freed && host_live() == L_HOWL, "the knob held again: free, and it picks the layers again (%d)", host_live());
    // erasing: REC held 1 s on a strip erases that layer's loop; 2 s every loop
    sim_turn(1); sim_run(0.4f);
    while (mixer_cursor() != L_BREATH) { SI.jx = mixer_cursor() < L_BREATH ? 1.0f : -1.0f; sim_run(0.05f); SI.jx = 0; sim_run(0.1f); }
    int sb = host_layer_state(L_BREATH), sh = host_layer_state(L_HOWL);
    SI.rec = true; sim_run(1.3f); SI.rec = false; sim_tick(); sim_run(0.4f);
    sim_shot("mixer_erased");
    check(sb == LOOP_ON && sh == LOOP_ON && host_layer_state(L_BREATH) == LOOP_NONE && host_layer_state(L_HOWL) == LOOP_ON && song.loop_len,
          "REC held 1 s on BREATH: its loop is erased, HOWL's stays");
    SI.rec = true; sim_run(2.3f); SI.rec = false; sim_tick(); sim_run(0.5f);
    sim_gif_end();
    check(host_layer_state(L_HOWL) == LOOP_NONE && song.loop_len == 0, "REC held 2 s: every loop erased, the tempo free");
    set("mlvb", 80); set("mlvl", 70);
    make_live(L_DRUMS); sim_run(0.3f);
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
    metric("band_us_per_block", us);
    check(us < 30.0, "the whole band takes %.1f us per 64-frame block at its busiest (budget 30)", us);
    sim_gif_begin("together");
    for (int l = L_N - 1; l >= 0; l--) { make_live(l); quiet(); sim_run(1.2f); }
    sim_shot("jam_playing");
    sim_gif_end();
    run_action("xcla"); quiet();
    sim_run(0.5f);
}

static void fresh(void) { quiet(); make_live(L_DRUMS); run_action("xcla"); sim_run(0.3f); }

// ---------------------------------------------------------------- the link: a pretend SING in the room
static int H_param(const char *t) { return (int)pv[tag(t)]; }
static void host_link(void)
{
    section("band: link");
    fresh();
    set("stmp", 100);
    sim_run(0.2f);
    int k = sim_peer_add("SING", 1, -25.0, 4.0e9);               // its crystal 25 ppm slow, booted long before
    sim_run(1.5f);
    sim_peer_use(-1);
    check(link_is_clock(), "BAND is the clock of the two (it has the higher priority)");
    sim_peer_use(k);
    bool synced = link_synced(), took = link_time()->fpb == 19200;
    sim_peer_use(-1);
    check(synced && took, "the pretend SING is synced to BAND and runs its 100 BPM");
    sim_run(4.0f);
    float g = sim_peer_gap_ms(k);
    check(fabsf(g) < 1.0f, "their beats are %.3f ms apart", (double)g);
    sim_shot("link_two_boards");
    // a tempo set on the other board, no loop anywhere: BAND follows, its TEMPO shows it
    sim_peer_use(k);
    {
        uint32_t o; float f; link_origin(&o, &f);
        link_set_time(16000, o, 4, 0);                           // 120 BPM (beat 0 kept)
    }
    sim_peer_use(-1);
    sim_run(0.5f);
    check(song.fpb == 16000 && H_param("stmp") == 120, "a tempo from the other board: BAND plays 120 BPM (TEMPO %d)", H_param("stmp"));
    sim_run(3.0f);
    g = sim_peer_gap_ms(k);
    check(fabsf(g) < 1.0f, "still on the same beat (%.3f ms)", (double)g);
    // a tempo set here goes to the other board
    set("stmp", 90); sim_run(0.5f);
    sim_peer_use(k); uint32_t pf = link_time()->fpb; sim_peer_use(-1);
    check(pf == (uint32_t)(60.0f * AUDIO_SR / 90.0f + 0.5f), "BAND's TEMPO 90 reaches the other board (fpb %u)", (unsigned)pf);
    // the harmony both ways
    sim_peer_use(k); link_set_harm(7, 1, 3, 0, 0, -1); sim_peer_use(-1);
    sim_run(0.4f);
    check(H_param("skey") == 7 && H_param("sscl") == 1 && H_param("sprg") == 3, "key, scale and chords from the other board (KEY %d SCALE %d CHORDS %d)",
          H_param("skey"), H_param("sscl"), H_param("sprg"));
    set("skey", 2); sim_run(0.4f);
    sim_peer_use(k); int pk = link_harm()->key; sim_peer_use(-1);
    check(pk == 2, "KEY D here reaches the other board (%d)", pk);
    host_chord_override(4); sim_run(0.4f);
    sim_peer_use(k); int pl = link_harm()->live; sim_peer_use(-1);
    host_chord_override(-1); sim_run(0.3f);
    sim_peer_use(k); int pl2 = link_harm()->live; sim_peer_use(-1);
    check(pl == 4 && pl2 == -1, "a chord played live (the bass's lean) reaches it, and its release (%d, %d)", pl, pl2);
    set("skey", 0); set("sscl", 0); set("sprg", 1); sim_run(0.3f);
    // the first loop fits the tempo: whole bars, on the session's beat 0
    set("stmp", 100); sim_run(0.3f);
    make_live(L_BREATH); excite_breath(); if (!SI.mic) SI.mic = tone_mic;
    sim_run(0.37f);
    SI.rec = true; sim_run(2.1f); SI.rec = false; sim_tick();
    check(song.beats == 4 && song.loop_len == 4u * 19200u, "a 2.1 s take at 100 BPM makes a one-bar loop (%d beats)", song.beats);
    check(song_mod((int32_t)(song.loop_start - song.origin), (int32_t)song.loop_len) == 0, "the loop starts on the session's beat 0");
    sim_run(0.3f);
    sim_peer_use(k); int pb = link_time()->beats; bool pl3 = link_flags_any() & LINKF_LOOP; sim_peer_use(-1);
    check(pb == 4 && pl3, "the other board knows the session's loop: %d beats, and that BAND has one", pb);
    // a loop keeps the tempo
    set("stmp", 130); sim_run(0.2f);
    check(H_param("stmp") == 100 && song.fpb == 19200, "with a loop, TEMPO stays (%d)", H_param("stmp"));
    sim_peer_use(k); { uint32_t o; float f; link_origin(&o, &f); link_set_time(15000, o, 4, 0); } sim_peer_use(-1);
    sim_run(0.5f);
    check(song.fpb == 19200, "and a tempo from the other board does not move it (fpb %u)", (unsigned)song.fpb);
    // the track waits for the bar's end, then plays in time; over 20 s of drifting clocks it stays with the other board
    quiet(); make_live(L_DRUMS);
    sim_run(20.0f);
    g = sim_peer_gap_ms(k);
    check(fabsf(g) < 1.0f, "after 20 s of drift the beats are %.3f ms apart", (double)g);
    // clear everything: the session's loop goes, the tempo is free again
    run_action("xcla"); sim_run(0.5f);
    sim_peer_use(k); int pb2 = link_time()->beats; sim_peer_use(-1);
    check(pb2 == 0, "CLEAR ALL: the session has no loop any more (%d)", pb2);
    // the other board makes the session's loop: BAND's first loop takes its length
    sim_peer_use(k); { uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 8); link_set_flags(LINKF_LOOP); } sim_peer_use(-1);
    sim_run(0.5f);
    make_live(L_BREATH); excite_breath(); if (!SI.mic) SI.mic = tone_mic;
    SI.rec = true; sim_run(1.0f); SI.rec = false; sim_tick();
    check(song.beats == 8, "the other board's 8-beat loop sets BAND's (a 1 s take: %d beats)", song.beats);
    run_action("xcla"); quiet();
    sim_peer_use(k); link_set_flags(0); sim_peer_use(-1);
    // LINK off: alone, the clock again
    set("nlnk", 0); sim_run(0.3f);
    check(!link_radio() && link_is_clock(), "LINK OFF: the radio stops, BAND keeps its own time");
    set("nlnk", 1); sim_run(1.5f);
    link_peer_t pr[4];
    check(link_radio() && link_peers(pr, 4) == 1, "LINK ON: the other board is back");
    sim_peer_power(k, false);
    sim_run(2.0f);
    check(link_peers(pr, 4) == 0, "the other board switched off: BAND is alone");
    fresh();
}

void inst_tests(void)
{
    host_layers();
    host_song();
    host_tracks();
    host_mix_fx();
    host_mixer();
    host_link();
    // each layer's own: it starts with no loop, DRUMS live, the mic off, the stick there
    section("drums"); fresh(); test_layer_drums();
    section("bass"); fresh(); test_layer_bass();
    section("lead"); fresh(); test_layer_lead();
    section("keys"); fresh(); test_layer_keys();
    section("breath"); fresh(); test_layer_breath();
    section("howl"); fresh(); test_layer_howl();
    fresh();
    host_cost();
}
