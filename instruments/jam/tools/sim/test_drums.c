// DRUMS (KNOCK as a layer) on the host: knocks into the mic, each with the push it gives the box in the
// 1 kHz accelerometer stream. Each side its drum; the jam's first loop made on the drums (the song's tempo
// and beats from it); the loop on time, on the grid, added to, muted with a tap, undone; the kicks published
// for the bass; the stick's roll and repeat; a BY SIDE body playing the song's chord; knocks ignored while
// another layer is live while the drums' loop plays on; the layer's own cost.
#include "sim.h"
#include "jam.h"
#include <math.h>
#include <string.h>
#include <time.h>

enum { K_DULL, K_SHARP, K_RUB };
typedef struct { float t; vec3_t push; int kind; float amp, dur; } knock_t;
static knock_t script[512];
static int nscript, first;
static float lp_state, hp_prev, rub_lp, excite_period;

static void knock(float t, vec3_t push, int kind, float amp) { if (nscript < 512) script[nscript++] = (knock_t){ t, push, kind, amp, 0.06f }; }

static float mic(float t, void *u)
{
    (void)u;
    float x = 0.001f * sim_noise();
    while (first < nscript && t - script[first].t > script[first].dur + 0.2f) first++;
    for (int i = first; i < nscript; i++) {
        float dt = t - script[i].t;
        if (dt < 0 || dt > script[i].dur) continue;
        float n = sim_noise(), a = script[i].amp;
        if (script[i].kind == K_SHARP) { float h = n - hp_prev; hp_prev = n; x += 0.5f * a * h * expf(-dt / 0.006f); }
        else if (script[i].kind == K_DULL) { lp_state += 0.05f * (n - lp_state); x += 2.0f * a * lp_state * expf(-dt / 0.02f); }
        else { rub_lp += 0.3f * (n - rub_lp); x += a * (n - rub_lp); }
    }
    if (excite_period > 0) {
        float ph = fmodf(t, excite_period);
        int k = (int)(t / excite_period);
        float n = sim_noise();
        if (k & 1) { float h = n - hp_prev; hp_prev = n; x += 0.4f * h * expf(-ph / 0.006f); }
        else { lp_state += 0.05f * (n - lp_state); x += 1.6f * lp_state * expf(-ph / 0.02f); }
    }
    return x;
}

static vec3_t accel(float t)
{
    vec3_t a = SI.acc;
    for (int i = first; i < nscript; i++) {
        if (script[i].kind == K_RUB) continue;
        float dt = t - script[i].t;
        if (dt < 0 || dt > 0.03f) continue;
        float shape = dt < 0.003f ? 1.0f : -0.5f * expf(-(dt - 0.003f) / 0.006f) * cosf(6.2831853f * 180.0f * (dt - 0.003f));
        a.x += 2.0f * script[i].push.x * shape; a.y += 2.0f * script[i].push.y * shape; a.z += 2.0f * script[i].push.z * shape;
    }
    return a;
}

static void step(void)
{
    for (int i = 0; i < 10; i++) SI.burst[i] = accel(sim_t + (float)(i + 1) * 0.001f);
    SI.nburst = 10;
    sim_tick();
}
static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) step(); }
static void rec_hold(float s) { SI.rec = true; run(s); SI.rec = false; step(); }

static const vec3_t PFACE = { 0, 0, -1 }, PLEFT = { 1, 0, 0 }, PRIGHT = { -1, 0, 0 }, PTOP = { 0, 1, 0 }, PBOT = { 0, -1, 0 };

static int drum_notes(float t0, float t1, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < midi_n && n < max; i++)
        if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t0 && midi_log[i].t < t1) out[n++] = midi_log[i].d1;
    return n;
}

static int tag(const char *t) { return param_by_tag(t); }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static void make_live(int l) { for (int i = 0; i < 12 && host_live() != l; i++) sim_turn(host_live() < l ? 1 : -1); run(0.1f); }

void excite_drums(void)
{
    nscript = 0; first = 0;
    excite_period = 0.125f;
    SI.mic = mic; SI.jx = SI.jy = 0;
    sim_tilt(0, 0);
}

void test_layer_drums(void)
{
    excite_period = 0; nscript = 0; first = 0;
    SI.mic = mic; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true;
    sim_tilt(0, 0);
    make_live(L_DRUMS);
    set("dkit", 0); set("dgrd", 0);
    run(3.0f);                                                       // whatever rang before dies away
    float quiet = m_rms(sim_t - 0.5f, sim_t);
    check(quiet < 0.004f, "DRUMS live, nobody knocking: quiet (rms %.4f)", (double)quiet);

    // ---- each side is its own drum
    sim_gif_begin("drums");
    const vec3_t pushes[5] = { PFACE, PLEFT, PRIGHT, PTOP, PBOT };
    const int want[5] = { 36, 38, 42, 45, 39 };
    float t0 = sim_t + 0.3f;
    for (int i = 0; i < 5; i++) knock(t0 + 0.45f * (float)i, pushes[i], K_DULL, 1.0f);
    run(0.3f + 0.45f * 2.0f + 0.05f); sim_shot("drums_knocked");
    run(1.3f);
    int got[16], n = drum_notes(t0 - 0.05f, t0 + 2.4f, got, 16), right = 0;
    for (int i = 0; i < 5 && i < n; i++) right += got[i] == want[i];
    check(n == 5 && right == 5, "STREET: face, left, right, top, bottom give kick, snare, hat, tom, clap on channel 10 (%d heard, %d right)", n, right);

    // ---- the jam's first loop, made on the drums: REC down, kick, snare, kick, REC up after 2.4 s
    song.loop_len = 0;
    float ts = sim_t + 0.1f;
    const float kt[4] = { 0.2f, 0.8f, 1.4f, 2.0f };
    knock(ts + kt[0], PFACE, K_DULL, 1.0f); knock(ts + kt[1], PLEFT, K_DULL, 1.0f);
    knock(ts + kt[2], PFACE, K_DULL, 1.0f); knock(ts + kt[3], PLEFT, K_DULL, 1.0f);
    run(0.1f);
    SI.rec = true; run(1.0f); sim_shot("drums_recording"); run(1.4f); SI.rec = false; step();
    float len = (float)song.loop_len / AUDIO_SR;
    check(song.loop_len > 0 && fabsf(len - 2.4f) < 0.02f, "REC held 2.4 s on the drums: the jam's loop is 2.4 s (%.3f s)", (double)len);
    float bpm_fit = (float)song.beats * 60.0f / len;
    check(song.beats >= 1 && song.bpm >= 75.0f && song.bpm <= 165.0f && fabsf(bpm_fit - song.bpm) < 0.5f,
          "it sets the song: %d beats at %.1f BPM (whole beats, between 75 and 165)", song.beats, (double)song.bpm);
    run(0.2f);
    sim_shot("drums_looping");
    {   // the knocks are kept where they were knocked
        float t1 = sim_t;
        run(len * 3.0f);
        int nn[64]; int nl = drum_notes(t1, sim_t, nn, 64);
        check(nl >= 11 && nl <= 13, "the loop plays its 4 hits every pass (%d notes in 3 passes)", nl);
    }
    sim_gif_end();
    {   // GRID 1/16 at full strength: every hit on a sixteenth of the song's beats
        set("dgrd", 4); set("dstr", 100); set("dswg", 0); run(0.1f);
        float g = (float)song.loop_len / (float)(song.beats * 4);
        int on = 0, total = 0;
        float t1 = sim_t; run(len * 2.0f);
        // the loop's notes land on sixteenths of the song: their times since the loop's start
        for (int i = 0; i < midi_n; i++) {
            if (midi_log[i].st != 0x99 || midi_log[i].d2 == 0 || midi_log[i].t < t1) continue;
            float pos = fmodf((midi_log[i].t * (float)AUDIO_SR - (float)song.loop_start), (float)song.loop_len);
            float r = fmodf(pos, g); if (r > g / 2) r -= g;
            total++; if (fabsf(r) < 0.012f * AUDIO_SR) on++;              // MIDI is stamped per 10 ms tick
        }
        check(total >= 6 && on == total, "GRID 1/16: the hits land on sixteenths of the song (%d of %d)", on, total);
        set("dgrd", 0);
    }
    // ---- the kicks, for the bass
    run(0.2f);
    {
        int bits = __builtin_popcountll(song.kicks), steps = song.beats * 4;
        int want0 = (int)(kt[0] / len * (float)steps + 0.5f) % steps;
        check(bits == 2 && (song.kicks >> want0 & 1), "song.kicks: the loop's 2 kicks as sixteenths (%d set, bit %d %s)", bits, want0, song.kicks >> want0 & 1 ? "on" : "off");
    }
    // ---- add a hat, mute with a tap, undo
    knock(sim_t + 0.3f, PRIGHT, K_DULL, 1.0f);
    rec_hold(0.7f); run(0.2f);
    int added[64]; run(len + 0.1f);
    int na = drum_notes(sim_t - len, sim_t, added, 64), hats = 0;
    for (int i = 0; i < na; i++) hats += added[i] == 42;
    check(hats >= 1 && na == 5, "REC held again: a hat added to the loop (%d notes a pass, %d hat)", na, hats);
    rec_hold(0.1f); run(0.3f);
    float tq = sim_t; run(len);
    check(drum_notes(tq, sim_t, added, 64) == 0, "a tap on REC mutes the drums' loop");
    rec_hold(0.1f); run(len + 0.2f);
    check(drum_notes(sim_t - len, sim_t, added, 64) >= 4, "another tap brings it back");
    if (layer_drums.action) layer_drums.action(9);                      // UNDO LAST
    run(len + 0.2f);
    na = drum_notes(sim_t - len, sim_t, added, 64); hats = 0;
    for (int i = 0; i < na; i++) hats += added[i] == 42;
    check(na == 4 && hats == 0, "UNDO LAST takes the hat away (%d notes a pass)", na);

    // ---- the stick: up rolls the last drum (by the song's tempo), down repeats the last slice
    sim_gif_begin("drums-stick");
    knock(sim_t + 0.05f, PLEFT, K_DULL, 1.0f); run(0.3f);
    SI.jy = 0.75f; float tr = sim_t; run(1.0f); sim_shot("drums_roll");
    int rolls[64]; int nr = drum_notes(tr + 0.05f, sim_t, rolls, 64), snares = 0;
    for (int i = 0; i < nr; i++) snares += rolls[i] == 38;
    float want_rate = 4.0f * song.bpm / 60.0f;
    check(snares >= (int)(want_rate * 0.95f * 0.8f) && snares <= (int)(want_rate + 2.0f),
          "stick up: a roll of the snare in sixteenths of the song (%d in 0.95 s, about %.0f wanted)", snares, (double)(want_rate * 0.95f));
    SI.jy = -0.75f; run(0.3f);
    float ts0 = sim_t; run(0.6f); sim_shot("drums_repeat");
    SI.jy = 0; run(0.3f);
    sim_gif_end();
    {
        float slice = 60.0f / song.bpm / 4.0f;
        int L = (int)(slice * AUDIO_SR), a = (int)(ts0 * AUDIO_SR);
        double c = 0, e0 = 0, e1 = 0;
        for (int i = 0; i < 3 * L; i++) { float x0 = sim_out[a + i], x1 = sim_out[a + i + L]; c += x0 * x1; e0 += x0 * x0; e1 += x1 * x1; }
        float r = (float)(c / sqrt(e0 * e1 + 1e-12));
        check(r > 0.8f, "stick down: the last 1/16 of the drums repeats (correlation %.2f at %.0f ms)", (double)r, (double)(slice * 1000.0f));
    }

    // ---- a BY SIDE body plays the song's chord: MARIMBA, the face the root, left the third, right the fifth
    if (layer_drums.clear) layer_drums.clear();
    set("dkit", 5); set("dpit", 0); set("dtun", 0);
    run(2.5f);
    int good = 0;
    for (int i = 0; i < 3; i++) {
        float tk = sim_t + 0.05f;
        knock(tk, pushes[i], K_DULL, 1.0f);
        run(0.6f);
        float f = m_pitch_at(tk + 0.12f, tk + 0.5f, 120, 1200);
        int wantm = scale_note(song.degree + 2 * i, 4);
        if (fabsf(hz_to_midi(f) - (float)wantm) < 0.35f) good++;
        else note("side %d: %.1f Hz (MIDI %.2f), wanted %d", i, (double)f, (double)hz_to_midi(f), wantm);
        run(1.4f);
    }
    check(good == 3, "MARIMBA by side: face, left, right ring the chord's root, third and fifth (%d right)", good);
    sim_shot("drums_marimba");
    set("dkit", 0);

    // ---- not live: knocks are not the drums', and the drums' loop plays on
    if (layer_drums.clear) layer_drums.clear();
    knock(sim_t + 0.1f, PFACE, K_DULL, 1.0f); knock(sim_t + 0.5f, PLEFT, K_DULL, 1.0f);
    rec_hold(0.9f);
    run(0.3f);
    make_live(L_BASS);
    float tn = sim_t + 0.1f;
    knock(tn, PRIGHT, K_DULL, 1.0f);
    run(len + 0.3f);
    int nb[32]; int nnl = drum_notes(tn, sim_t, nb, 32), hat2 = 0;
    for (int i = 0; i < nnl; i++) hat2 += nb[i] == 42;
    check(hat2 == 0 && nnl >= 2, "BASS live: a knock is not a drum, and the drums' loop plays on (%d notes, %d hats)", nnl, hat2);
    make_live(L_DRUMS);

    // ---- the layer's own cost, busy: a full loop, a BELLS body, voices
    {
        set("dkit", 6);
        float in[AUDIO_BLOCK], dry[AUDIO_BLOCK], send[AUDIO_BLOCK];
        excite_period = 0.1f;
        struct timespec a, b;
        double tsum = 0; int nblk = 3000;
        float tt = sim_t;
        for (int k = 0; k < nblk; k++) {
            for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = mic(tt + (float)i / (float)AUDIO_SR, NULL);
            tt += (float)AUDIO_BLOCK / (float)AUDIO_SR;
            memset(dry, 0, sizeof dry); memset(send, 0, sizeof send);
            clock_gettime(CLOCK_MONOTONIC, &a);
            layer_drums.audio(in, dry, send, AUDIO_BLOCK, true);
            clock_gettime(CLOCK_MONOTONIC, &b);
            tsum += (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
        }
        excite_period = 0;
        double us = tsum / nblk;
        metric("drums_us_per_block", us);
        check(us < 9.0, "the drums layer takes %.1f us per block, busy (budget 9)", us);
        set("dkit", 0);
    }
    if (layer_drums.clear) layer_drums.clear();
    nscript = 0; first = 0;
    run(0.5f);
}
