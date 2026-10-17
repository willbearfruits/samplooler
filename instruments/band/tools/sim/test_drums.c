// DRUMS (KNOCK as a layer) on the host: knocks into the mic, each with the push it gives the box in the
// 1 kHz accelerometer stream. Each side its drum; the jam's first loop made on the drums (the song's tempo
// and beats from it); the loop on time, on the grid, added to, muted with a tap, undone; the kicks published
// for the bass; the stick's roll and repeat; a BY SIDE body playing the song's chord; knocks ignored while
// another layer is live while the drums' loop plays on; the layer's own cost. Then the GRID: its steps on the
// song's beats (sample-accurate), GRID STEP, the editor (open, move, set, paint, leave), the pattern kept in a
// sound, playing under another layer, its kicks for the bass, and a first take placed on a loop made elsewhere.
#include "sim.h"
#include "band.h"
#include "oled.h"
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
bool sim_menu_param(int p);
void sim_menu_close(void);
bool drums_editing(void);
int  drums_cursor(void);
static void test_grid(void);
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static void make_live(int l) { for (int i = 0; i < 12 && host_live() != l; i++) sim_turn(host_live() < l ? 1 : -1); run(0.1f); }

void excite_drums(void)
{
    nscript = 0; first = 0;
    excite_period = 0.05f;                                           // knocks close together, and the stick's roll on top:
    SI.mic = mic; SI.jx = 0; SI.jy = 1.0f;                           // every window around a change hears as sharp a hit
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
        // where the first kick was knocked, in the loop (its start is on the song's beats, not the take's)
        int bits = __builtin_popcountll(song.kicks), steps = song.beats * 4;
        int want0 = (int)((float)song_pos((uint32_t)((ts + kt[0]) * AUDIO_SR)) / (float)song.loop_len * (float)steps + 0.5f) % steps;
        bool near = (song.kicks >> want0 & 1) || (song.kicks >> ((want0 + 1) % steps) & 1) || (song.kicks >> ((want0 + steps - 1) % steps) & 1);
        check(bits == 2 && near, "song.kicks: the loop's 2 kicks as sixteenths (%d set, the first at sixteenth %d %s)", bits, want0, near ? "on" : "off");
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
    if (layer_drums.action) layer_drums.action(param_by_tag("dund") - layer_base[L_DRUMS]);   // UNDO LAST
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
    knock(sim_t + 0.2f, PFACE, K_DULL, 1.0f); knock(sim_t + 0.5f, PLEFT, K_DULL, 1.0f);   // (past REC's click guard)
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
        set("dgon", 1); set("dgst", 1); set("dgr0", 0xFFFF); set("dgr1", 0xFFFF); set("dgr2", 0xFFFF); set("dgr3", 0xFFFF);   // and the grid, every step
        float in[AUDIO_BLOCK], dry[AUDIO_BLOCK], send[AUDIO_BLOCK];
        excite_period = 0.1f;
        struct timespec a, b;
        double us = 1e9; int nblk = 1500;                                // the best of three runs: other work on this machine only adds
        float tt = sim_t;
        for (int run3 = 0; run3 < 3; run3++) {
            double tsum = 0;
            for (int k = 0; k < nblk; k++) {
                for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = mic(tt + (float)i / (float)AUDIO_SR, NULL);
                tt += (float)AUDIO_BLOCK / (float)AUDIO_SR;
                memset(dry, 0, sizeof dry); memset(send, 0, sizeof send);
                clock_gettime(CLOCK_MONOTONIC, &a);
                layer_drums.audio(in, dry, send, AUDIO_BLOCK, true);
                clock_gettime(CLOCK_MONOTONIC, &b);
                tsum += (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
            }
            if (tsum / nblk < us) us = tsum / nblk;
        }
        excite_period = 0;
        metric("drums_us_per_block", us);
        check(us < 9.0, "the drums layer takes %.1f us per block, busy: knock loop, grid on every step, BELLS (budget 9)", us);
        set("dkit", 0); set("dgon", 0); set("dgst", 0);
        set("dgr0", 0x0111); set("dgr1", 0x4444); set("dgr2", 0x55FF); set("dgr3", 0);
    }
    if (layer_drums.clear) layer_drums.clear();
    nscript = 0; first = 0;
    run(0.5f);
    test_grid();
}

// ---------------------------------------------------------------- the grid
static void rows(int r0, int r1, int r2, int r3) { set("dgr0", (float)r0); set("dgr1", (float)r1); set("dgr2", (float)r2); set("dgr3", (float)r3); }
static int rowv(int r) { char t[5] = "dgr0"; t[3] = (char)('0' + r); return (int)pv[tag(t)]; }
// the channel-10 notes between two times, with where each fell on the song's beats (MIDI is stamped at the tick
// after the sound: 5 ms back is the middle of that tick)
static int grid_notes(float t0, float t1, float *beat, int *note, int max)
{
    int n = 0;
    for (int i = 0; i < midi_n && n < max; i++) {
        if (midi_log[i].st != 0x99 || midi_log[i].d2 == 0 || midi_log[i].t < t0 || midi_log[i].t >= t1) continue;
        beat[n] = song_beat((uint32_t)((midi_log[i].t - 0.005f) * AUDIO_SR)); note[n] = midi_log[i].d1; n++;
    }
    return n;
}
static float bar_s(void) { return 4.0f * (float)song.fpb / (float)AUDIO_SR; }
// run until just after a bar starts; returns the time the bar's first tick began
static float to_bar(void)
{
    int bar = (int)(song_beat(audio_frames()) / 4.0f);
    for (int i = 0; i < 1000 && (int)(song_beat(audio_frames()) / 4.0f) == bar; i++) step();
    return sim_t - 0.011f;
}
static float near_int(float x) { return fabsf(x - floorf(x + 0.5f)); }

static void test_grid(void)
{
    section("drums: grid");
    nscript = 0; first = 0; excite_period = 0; SI.mic = NULL; SI.jx = SI.jy = 0;
    if (layer_drums.clear) layer_drums.clear();
    song.loop_len = 0; song.beats = 0;
    make_live(L_DRUMS);
    set("dkit", 0); set("dsnd", 0); set("dmut", 0); set("dswg", 0); set("stmp", 100); set("dtun", 0);
    run(1.5f);
    check(!(layer_drums.playing && layer_drums.playing()), "the grid is off at first: nothing plays by itself");

    // ---- a rock beat at 1/8: kicks on 1 and 3, snares on 2 and 4, hats on every eighth
    set("dgst", 0); rows(0x11, 0x44, 0xFF, 0); set("dgon", 1);
    check(layer_drums.playing && layer_drums.playing(), "GRID ON with steps set: the layer says it plays by itself");
    float tb = to_bar(), bs = bar_s();
    run(2.0f * bs);
    float bt[128]; int nt[128];
    int n = grid_notes(tb, tb + 2.0f * bs, bt, nt, 128), kicks = 0, snares = 0, hats = 0, wrong = 0, off = 0;
    for (int i = 0; i < n; i++) {
        float e = bt[i] * 2.0f;                                          // eighths since the bar line (song_beat wraps by bars)
        if (near_int(e) > 0.08f) { off++; continue; }
        int stp = (int)floorf(e + 0.5f) % 8;
        if (nt[i] == 36) { kicks++; if (stp != 0 && stp != 4) wrong++; }
        else if (nt[i] == 38) { snares++; if (stp != 2 && stp != 6) wrong++; }
        else if (nt[i] == 42) hats++;
        else wrong++;
    }
    check(kicks == 4 && snares == 4 && hats == 16 && wrong == 0 && off == 0,
          "1/8: in two bars 4 kicks on 1 and 3, 4 snares on 2 and 4, 16 hats, all on the song's eighths (%d %d %d, %d wrong, %d off)", kicks, snares, hats, wrong, off);
    sim_shot("drums_grid_lane");

    // ---- sample-accurate: one hat on the fourth eighth, its onset in the sound (the bass, playing on from before, muted)
    rows(0, 0, 0x08, 0);
    set("mmub", 1);
    run(1.5f);
    tb = to_bar(); run(2.0f * bs);
    {
        int a = (int)(tb * AUDIO_SR), b = (int)((tb + 2.0f * bs) * AUDIO_SR), quiet_n = 0, found = 0;
        float worst = 0;
        for (int i = a; i < b && i < sim_n; i++) {
            float x = fabsf(sim_out[i]);
            if (x < 0.002f) { quiet_n++; continue; }
            if (quiet_n > 3200) {                                            // the first sample out of 0.1 s of quiet
                float e = song_beat((uint32_t)i) * 2.0f;                    // eighths
                float d = e - 3.0f - 8.0f * floorf((e - 3.0f + 4.0f) / 8.0f);
                float fr = fabsf(d) * (float)song.fpb * 0.5f;                // frames from the step's start
                if (fr > worst) worst = fr;
                found++;
            }
            quiet_n = 0;
        }
        check(found == 2 && worst < 8.0f, "a hat on step 4 starts within %.0f frames of the song's fourth eighth, each bar (%d found)", (double)worst, found);
    }
    set("mmub", 0);

    // ---- GRID STEP 1/16: eight sixteenths, twice a bar; 1/4: two bars
    set("dgst", 1); rows(0x01, 0, 0, 0);
    run(0.5f);
    tb = to_bar(); run(2.0f * bs);
    n = grid_notes(tb, tb + 2.0f * bs, bt, nt, 128); kicks = 0; wrong = 0;
    for (int i = 0; i < n; i++) { if (nt[i] != 36) continue; kicks++; float sx = bt[i] * 4.0f; int s16 = (int)floorf(sx + 0.5f) % 16; if (near_int(sx) > 0.15f || (s16 != 0 && s16 != 8)) wrong++; }
    check(kicks == 4 && wrong == 0, "1/16: a kick on step 1 sounds twice a bar, on beats 1 and 3 (%d in 2 bars, %d wrong)", kicks, wrong);
    set("dgst", 2); rows(0x11, 0, 0, 0);
    run(0.5f);
    for (int k = 0; k < 8 && ((int)(song_beat(audio_frames()) / 4.0f)) % 2 != 1; k++) run(bs * 0.5f);
    tb = to_bar(); run(4.0f * bs);
    n = grid_notes(tb, tb + 4.0f * bs, bt, nt, 128); kicks = 0; wrong = 0;
    for (int i = 0; i < n; i++) { if (nt[i] != 36) continue; kicks++; float q = bt[i]; int bq = (int)floorf(q + 0.5f) % 8; if (near_int(q) > 0.06f || (bq != 0 && bq != 4)) wrong++; }
    check(kicks == 4 && wrong == 0, "1/4: steps 1 and 5 are beats 1 of two bars, so 4 kicks in 4 bars (%d, %d wrong)", kicks, wrong);
    // a loop of one bar and the 1/4 grid's two bars: the pattern carries on from pass to pass
    {
        uint32_t fpb = song.fpb, now = audio_frames();
        song.beats = 4;
        song.loop_start = now - (uint32_t)song_mod((int32_t)(now - song.origin), (int32_t)(4 * fpb));
        __atomic_store_n(&song.loop_len, 4 * fpb, __ATOMIC_RELEASE);
        rows(0x01, 0x10, 0, 0);                                          // a kick on step 1, a snare on step 5
        tb = to_bar(); run(4.0f * bs);
        n = grid_notes(tb, tb + 4.0f * bs, bt, nt, 128); kicks = snares = 0; wrong = 0;
        for (int i = 0; i < n; i++) { if (nt[i] == 36) kicks++; else if (nt[i] == 38) snares++; else wrong++; if (near_int(bt[i]) > 0.06f || (int)floorf(bt[i] + 0.5f) % 4 != 0) wrong++; }
        check(kicks == 2 && snares == 2 && wrong == 0, "a one-bar loop, GRID STEP 1/4: kick bar, snare bar, in turn (%d kicks, %d snares in 4 bars)", kicks, snares);
        song.loop_len = 0; song.beats = 0;
    }
    set("dgst", 0);

    // ---- the kicks for the bass, with no knock loop
    rows(0x11, 0x44, 0xFF, 0);
    run(0.3f);
    check(song.kicks == ((1ull << 0) | (1ull << 8)), "song.kicks from the grid: sixteenths 1 and 9 of the bar (%llx)", (unsigned long long)song.kicks);

    // ---- the pattern is part of the sound (what SAVE AS and flash keep)
    {
        static uint8_t buf[PARAMS_MAX * 8];
        rows(0x0111, 0x1090, 0x55FF, 0x0080);
        int nb = params_pack(buf, sizeof buf, true);
        rows(0, 0, 0, 0);
        params_unpack(buf, nb, true);
        check(rowv(0) == 0x0111 && rowv(1) == 0x1090 && rowv(2) == 0x55FF && rowv(3) == 0x0080, "the grid is kept with the sound (packed and unpacked: %04x %04x %04x %04x)", rowv(0), rowv(1), rowv(2), rowv(3));
    }

    // ---- GRID BEAT: a ready beat loads; drawn on, it is MINE; back on MINE, the drawn one returns
    set("dgbt", 0); rows(0x03, 0, 0, 0);
    set("dgbt", 2);                                                      // FUNK
    check(rowv(0) == 0x0109 && (int)pv[tag("dgst")] == 1, "GRID BEAT FUNK loads its steps and 1/16 (%04x)", rowv(0));
    set("dgbt", 0);
    check(rowv(0) == 0x03, "and MINE brings back what was drawn (%04x)", rowv(0));
    set("dgst", 0);

    // ---- the editor, from DRUMS > EDIT GRID
    rows(0, 0, 0, 0); set("dgon", 0);
    run(0.3f);
    int pe = tag("dged");
    if (pe >= 0 && sim_menu_param(pe)) { sim_tap(); run(0.1f); }
    check(drums_editing() && front_menu_level() == 0, "EDIT GRID: the menu closes and the editor is up");
    check(pv[tag("dgon")] >= 1.0f, "opening it starts the grid (you hear what you draw)");
    sim_shot("drums_grid_editor_open");
    int c0 = drums_cursor();
    sim_turn(1); sim_turn(1); sim_turn(1);
    int c3 = drums_cursor();
    check(c3 == (c0 + 3) % 32, "turning moves the cursor a step each detent (%d -> %d)", c0, c3);
    int lv0 = host_live();
    check(lv0 == L_DRUMS, "and does not change the layer");
    // put the cursor on row 1 (kicks), step 3, and tap
    while (drums_cursor() != 2) sim_turn(drums_cursor() < 2 ? 1 : -1);
    sim_turn(1);
    sim_tap(); run(0.05f);
    check((rowv(0) & 0x08) != 0, "a press sets the step under the cursor (row 1: %04x)", rowv(0));
    tb = to_bar(); run(bs);
    n = grid_notes(tb, tb + bs, bt, nt, 128); kicks = 0; wrong = 0;
    for (int i = 0; i < n; i++) { if (nt[i] != 36) continue; kicks++; if ((int)floorf(bt[i] * 2.0f + 0.5f) % 8 != 3) wrong++; }
    check(kicks == 1 && wrong == 0, "and the kick sounds there: one a bar, on the fourth eighth (%d, %d wrong)", kicks, wrong);
    sim_tap(); sim_tap(); run(0.05f);
    check((rowv(0) & 0x0808) == 0, "pressing again: accent, then off (%04x)", rowv(0));
    // paint the hats: cursor to row 3 step 1, press and turn seven times
    while (drums_cursor() != 16) sim_turn(drums_cursor() < 16 ? 1 : -1);
    SI.pressed = true; run(0.05f);
    for (int k = 0; k < 7; k++) { sim_turn(1); run(0.02f); }
    SI.pressed = false; step(); run(0.05f);
    check((rowv(2) & 0xFF) == 0xFF && drums_editing(), "press and turn paints: the whole hat row in one move (%04x), no tap after it", rowv(2));
    // a long paint does not leave the editor
    while (drums_cursor() != 24) sim_turn(drums_cursor() < 24 ? 1 : -1);
    SI.pressed = true; run(0.05f);
    for (int k = 0; k < 3; k++) { sim_turn(1); run(0.3f); }
    SI.pressed = false; step(); run(0.05f);
    check(drums_editing() && (rowv(3) & 0x0F) == 0x0F, "a slow paint held past the hold time stays in the editor (%04x)", rowv(3));
    // REC held 1 s clears the cursor's row
    SI.rec = true; run(1.1f); SI.rec = false; step(); run(0.05f);
    check(rowv(3) == 0 && (rowv(2) & 0xFF) == 0xFF, "REC held 1 s clears the cursor's row only (%04x, hats %04x)", rowv(3), rowv(2));
    // REC tapped: the grid stops, again: it plays
    SI.rec = true; run(0.1f); SI.rec = false; step();
    bool stopped = pv[tag("dgon")] < 1.0f;
    SI.rec = true; run(0.1f); SI.rec = false; step();
    check(stopped && pv[tag("dgon")] >= 1.0f, "REC tapped in the editor stops the grid, again starts it");
    // the hint fits the bottom line
    int hw = oled_ftext_w(F_TINY, "TURN: MOVE  PRESS: SET  HOLD: DONE", 0);
    check(hw <= 126, "the editor's hint fits the screen (%d px)", hw);
    // hold: the editor goes, and the knob picks layers again
    sim_hold_knob(); run(0.05f);
    check(!drums_editing(), "holding the knob leaves the editor");
    sim_turn(1); run(0.1f);
    check(host_live() == L_DRUMS + 1, "after it a turn picks the next layer again (live %d)", host_live());
    make_live(L_DRUMS);
    // the layer's own hold hook (the host calls it when the knob is held in play)
    if (layer_drums.hold) {
        bool used = layer_drums.hold(); run(0.05f);
        check(used && drums_editing(), "layer.hold() opens the editor and says it used the hold");
        sim_hold_knob(); run(0.05f);
        check(!drums_editing(), "and a hold closes it");
    }

    // ---- under another layer the grid plays on
    rows(0x11, 0x44, 0xFF, 0); set("dgon", 1);
    make_live(L_BASS);
    tb = to_bar(); run(bs);
    n = grid_notes(tb, tb + bs, bt, nt, 128);
    check(n == 12, "BASS live: the drawn beat plays on (%d notes a bar, 12 drawn)", n);
    make_live(L_DRUMS);

    // ---- a first knock take while another layer makes the loop: the knocks stay where they were knocked
    set("dgon", 0); run(1.0f);
    if (layer_drums.clear) layer_drums.clear();
    song.loop_len = 0; song.beats = 0;
    SI.mic = mic; nscript = 0; first = 0;
    float ts = sim_t + 0.1f;
    const float kt2[3] = { 0.25f, 0.95f, 1.55f };
    knock(ts + kt2[0], PFACE, K_DULL, 1.0f); knock(ts + kt2[1], PLEFT, K_DULL, 1.0f); knock(ts + kt2[2], PRIGHT, K_DULL, 1.0f);
    run(0.1f);
    SI.rec = true; run(0.5f);
    {   // the loop appears now, from elsewhere: a bar, starting on a beat well before this take
        uint32_t fpb = song.fpb, now = audio_frames();
        song.beats = 4;
        song.loop_start = now - (uint32_t)song_mod((int32_t)(now - song.origin), (int32_t)fpb) - 2 * fpb;
        __atomic_store_n(&song.loop_len, 4 * fpb, __ATOMIC_RELEASE);
    }
    run(1.4f); SI.rec = false; step();
    float L = (float)song.loop_len / AUDIO_SR;
    float t1 = sim_t + 0.2f; run(0.2f + 3.0f * L);
    int got = 0, bad = 0;
    for (int i = 0; i < midi_n; i++) {
        if (midi_log[i].st != 0x99 || midi_log[i].d2 == 0 || midi_log[i].t < t1) continue;
        float best = 1e9f;
        for (int k = 0; k < 3; k++) { float d = fmodf(midi_log[i].t - (ts + kt2[k]), L); if (d < 0) d += L; if (d > L / 2) d -= L; if (fabsf(d) < fabsf(best)) best = d; }
        got++; if (best < -0.005f || best > 0.03f) bad++;
    }
    check(got >= 8 && bad == 0, "a take begun before the loop existed: its hits replay where they were knocked (%d notes, %d misplaced)", got, bad);
    if (layer_drums.clear) layer_drums.clear();
    song.loop_len = 0; song.beats = 0;
    nscript = 0; first = 0; SI.mic = NULL;
    run(0.5f);

    // ---- drawing a beat, for the GIF
    rows(0, 0, 0, 0); set("dgbt", 0); set("dkit", 0); set("dsnd", 15); set("stmp", 104);
    run(2.0f);
    sim_gif_begin("drums-grid");
    run(0.6f);
    if (layer_drums.hold) layer_drums.hold();
    run(0.6f);
    while (drums_cursor() != 16) { sim_turn(drums_cursor() < 16 ? 1 : -1); run(0.02f); }
    SI.pressed = true; run(0.1f);
    for (int k = 0; k < 7; k++) { sim_turn(1); run(0.06f); }
    SI.pressed = false; step(); run(0.5f);
    while (drums_cursor() != 0) { sim_turn(-1); run(0.01f); }
    run(0.2f);
    sim_tap(); run(0.3f);
    for (int k = 0; k < 4; k++) { sim_turn(1); run(0.05f); }
    sim_tap(); run(0.15f);
    sim_turn(1); run(0.05f); sim_tap(); run(0.3f);
    for (int k = 0; k < 5; k++) { sim_turn(1); run(0.05f); }
    sim_tap(); run(0.15f);
    for (int k = 0; k < 4; k++) { sim_turn(1); run(0.05f); }
    sim_tap(); run(0.3f);
    for (int k = 0; k < 2; k++) { sim_turn(1); run(0.05f); }
    sim_tap(); run(0.05f); sim_tap(); run(0.3f);                     // an accented hat on 1
    for (int k = 0; k < 4; k++) { sim_turn(1); run(0.05f); }
    sim_tap(); run(0.05f); sim_tap(); run(0.1f);                     // and on 3
    sim_shot("drums_grid_editor");
    run(2.5f);
    sim_hold_knob();
    run(2.0f);
    sim_gif_end();
    sim_shot("drums_grid_back");
    set("dgon", 0); rows(0x0111, 0x4444, 0x55FF, 0); set("dgbt", 1); set("dsnd", 15); set("stmp", 100);
    run(1.8f);

    // ---- REC's clicks: a knock right at the press or at the release is not recorded
    if (layer_drums.clear) layer_drums.clear();
    set("dgon", 0); run(0.3f);
    SI.mic = mic; excite_period = 0; make_live(L_DRUMS); run(0.3f);
    knock(sim_t + 0.01f, PTOP, K_SHARP, 1.0f);                       // the press's click
    knock(sim_t + 0.35f, PFACE, K_DULL, 1.0f); knock(sim_t + 0.75f, PFACE, K_DULL, 1.0f);
    knock(sim_t + 1.18f, PTOP, K_SHARP, 1.0f);                       // the release's click, just before it is seen
    rec_hold(1.2f);
    run(0.2f);
    int nc[64], ncl = 0;
    float tc0 = sim_t; run(2.5f);
    int nn2 = drum_notes(tc0, sim_t, nc, 64);
    for (int i = 0; i < nn2; i++) ncl += nc[i] != 36;
    check(nn2 >= 2 && ncl == 0, "REC's clicks stay out of the drums' loop (%d notes, %d not the kick)", nn2, ncl);
    // stopped (a tap), then REC held: a new take in place of the knocked loop (here with no knocks: nothing left)
    {
        bool g0 = pv[tag("dgon")] >= 1.0f;
        set("dgon", 0); run(0.1f);
        sim_rec(0.1f); run(0.3f);
        int st0 = layer_drums.state();
        rec_hold(0.8f); run(0.2f);
        int nr[64]; float tr = sim_t; run(2.5f);
        int left = drum_notes(tr, sim_t, nr, 64);
        check(st0 == LOOP_OFF && layer_drums.state() == LOOP_NONE && left == 0,
              "stopped, then REC held: a new take replaces the knocked loop (%d notes left)", left);
        if (g0) set("dgon", 1);
    }
    if (layer_drums.clear) layer_drums.clear();
    run(0.3f);

    // ---- KNOCKS off: only the grid plays; the grid open: knocks are not heard either (pressing the knob jolts the box)
    int ns[64];
    SI.mic = mic; excite_period = 0; make_live(L_DRUMS); run(0.5f);
    knock(sim_t + 0.2f, PFACE, K_DULL, 1.0f); run(0.8f);
    int heard = drum_notes(sim_t - 0.8f, sim_t, ns, 64);
    set("dkno", 0); run(0.1f);
    knock(sim_t + 0.2f, PFACE, K_DULL, 1.0f); run(0.8f);
    int k_off = drum_notes(sim_t - 0.8f, sim_t, ns, 64);
    set("dkno", 1); run(0.1f);
    check(heard >= 1 && k_off == 0, "KNOCKS OFF: a knock plays nothing (%d notes, %d with it on)", k_off, heard);
    set("dgon", 0); run(0.2f);
    if (layer_drums.hold) layer_drums.hold();
    run(0.2f);
    float t_ed = sim_t;
    knock(sim_t + 0.2f, PFACE, K_DULL, 1.0f); run(0.8f);
    int in_ed = 0;
    for (int i = 0; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t_ed && midi_log[i].d1 == 36 && midi_log[i].t < t_ed + 0.25f + 0.05f && midi_log[i].t > t_ed + 0.15f) in_ed++;
    sim_hold_knob(); run(0.2f);
    check(!drums_editing() && in_ed == 0, "the grid open: a knock is not heard as a drum (%d)", in_ed);
    // ---- a tap on REC stops the drums (the grid with the knocked loop) and another starts them again
    set("dgon", 1); run(0.5f);
    sim_rec(0.1f); run(1.2f);
    int n_stop = drum_notes(sim_t - 1.0f, sim_t, ns, 64);
    bool gon_after_stop = pv[tag("dgon")] >= 1.0f;
    sim_rec(0.1f); run(1.2f);
    int again = drum_notes(sim_t - 1.0f, sim_t, ns, 64);
    check(n_stop == 0 && !gon_after_stop && again >= 3, "tap REC: the drums stop (%d notes), tap again: they play (%d)", n_stop, again);
    set("dgon", 0); run(0.5f);
}
