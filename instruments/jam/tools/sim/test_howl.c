// HOWL's layer in the jam: live by turning the knob in play. The mic hears a quiet room (and, for the safety
// check, the jam's own output 8 ms later, four times too loud). Checks: quiet flat, a howl on the song's
// chord when tipped, a chord change that slides without a click and lands on the new chord's notes, the
// level ceiling under abuse, FREEZE keeping it going under another live layer, the 50 ms fade otherwise,
// MIDI on HOWL's channel, and the cost live, frozen and asleep.
#include "sim.h"
#include "jam.h"
#include <math.h>
#include <string.h>
#include <time.h>

// from layer_howl.c, for the tests
const volatile float *howl_band_hz(void);
float howl_level(void);
int   howl_sleeping(void);
float howl_out_peak(void);

enum { W_MODE, W_GAIN, W_RING, W_LEVEL, W_FREEZE, W_OCT, W_BANDS, W_GLIDE, W_ROOM, W_SIZE, W_SHIFT, W_COLOUR, W_HEAT, W_DRIFT, W_SWELL, W_SEND };
static void wset(int i, float v) { LP_SET(L_HOWL, i, v); }

typedef struct { float gain, noise; } air_t;
static air_t air = { 0, 0.002f };
static float mic(float t, void *u)
{
    air_t *a = u;
    float x = a->noise * sim_noise();
    if (a->gain > 0) {
        int i = (int)(t * (float)AUDIO_SR + 0.5f);
        float s = 0;
        if (i - 256 >= 0 && i - 256 < sim_n) s += 0.7f * sim_out[i - 256];
        if (i - 353 >= 0 && i - 353 < sim_n) s += 0.3f * sim_out[i - 353];
        if (i - 657 >= 0 && i - 657 < sim_n) s -= 0.2f * sim_out[i - 657];
        x += a->gain * s;
    }
    return x;
}

void sim_menu_close(void);
// The knob in play picks the layer, unless a setting was edited in the menu since (then the knob turns that):
// close the menu and give the knob back to the layers first.
static void go_live(int l)
{
    sim_menu_close();
    memset(meta.quick, 0, sizeof meta.quick);
    for (int k = 0; k < 40 && host_live() != l; k++) { sim_turn(host_live() < l ? 1 : -1); sim_run(0.05f); }
}
// only HOWL in the mix (the other layers' levels to 0, HOWL's to 100 %), if the host has those settings
static void solo(bool on)
{
    static const char *tags[] = { "mlvd", "mlvb", "mlvh" };
    for (int i = 0; i < 3; i++) { int p = param_by_tag(tags[i]); if (p >= 0) param_set(p, on ? 0.0f : param(p)->def); }
    int p = param_by_tag("mlvw"); if (p >= 0) param_set(p, on ? 100.0f : param(p)->def);
}
static int chord_pcs(int *pcs)
{
    int n = 0;
    for (int i = 0; i < song.chord_n; i++) {
        int pc = ((song.chord[i] % 12) + 12) % 12, dup = 0;
        for (int k = 0; k < n; k++) if (pcs[k] == pc) dup = 1;
        if (!dup) pcs[n++] = pc;
    }
    return n;
}
static bool in_chord(float hz)
{
    if (hz <= 0) return false;
    float m = hz_to_midi(hz);
    int pcs[12], n = chord_pcs(pcs), pc = (((int)lroundf(m)) % 12 + 12) % 12;
    if (fabsf(m - roundf(m)) > 0.4f) return false;
    for (int k = 0; k < n; k++) if (pcs[k] == pc) return true;
    return false;
}
static int bands_in_chord(void)
{
    const volatile float *hz = howl_band_hz();
    int ok = 0, nb = (int)LP(L_HOWL, W_BANDS);
    for (int b = 0; b < nb; b++) if (in_chord(hz[b])) ok++;
    return ok;
}
static int chord_sig(void) { int s = song.chord_n; for (int i = 0; i < song.chord_n; i++) s = s * 131 + song.chord[i]; return s; }
static float max_rms(float t0, float t1, float win)
{
    float m = 0;
    for (float t = t0; t + win <= t1; t += win * 0.5f) { float r = m_rms(t, t + win); if (r > m) m = r; }
    return m;
}

void excite_howl(void)
{
    air.gain = 0; SI.mic = mic; SI.mic_u = &air; SI.joy = false;
    wset(W_FREEZE, 0);
    sim_tilt(0, 22);
}

void test_layer_howl(void)
{
    go_live(L_HOWL);
    check(host_live() == L_HOWL, "the knob makes HOWL the live layer");
    solo(true);
    wset(W_SEND, 0);
    air.gain = 0; SI.mic = mic; SI.mic_u = &air; SI.joy = false;
    sim_tilt(0, 0); sim_run(2.0f);
    sim_shot("howl_quiet");
    check(m_rms(sim_t - 1.0f, sim_t) < 0.01f, "HOWL flat on the table: quiet (rms %.4f)", (double)m_rms(sim_t - 1.0f, sim_t));
    check(bands_in_chord() == (int)LP(L_HOWL, W_BANDS), "every band sits on a note of the song's chord (%d of %d)", bands_in_chord(), (int)LP(L_HOWL, W_BANDS));

    // tipped: a howl on the chord
    sim_gif_begin("howl_layer");
    for (int i = 0; i < 300; i++) { sim_tilt(0, 35.0f * (float)i / 300.0f); sim_tick(); }
    sim_run(2.0f);
    float h = m_rms(sim_t - 1.0f, sim_t), f = m_pitch_at(sim_t - 0.5f, sim_t, 100, 4000);
    check(h > 0.05f && in_chord(f), "tipped 35 degrees: it howls (rms %.3f) on a note of the chord (%.0f Hz, MIDI %.2f)", (double)h, (double)f, (double)hz_to_midi(f));
    float lim = exp2f(LP(L_HOWL, W_LEVEL) * 0.16609640f);
    check(howl_out_peak() <= 1.25f * lim * 1.01f, "and stays under 1.25x HOWL LEVEL (%.3f)", (double)(1.25f * lim));
    sim_shot("howl_howling");

    // the chord changes: the bands slide (shared notes stay), the howl lands on the new chord, no click
    sim_tilt(0, 30); sim_run(1.0f);
    int sig0 = chord_sig(), deg0 = song.degree;
    song.degree = deg0 == 3 ? 4 : 3;                         // the IV (or the V)
    sim_tick();
    if (chord_sig() == sig0) {                              // the host runs its own progression: wait for its change
        for (int k = 0; k < 1200 && chord_sig() == sig0; k++) sim_tick();
    }
    float tc = sim_t;
    bool changed = chord_sig() != sig0;
    check(changed, "the song's chord changed (to degree %d)", song.degree);
    float worst = 0;
    for (float t = tc; t < tc + 1.5f; t += 0.01f) { sim_tick(); float r = m_click(t, NULL, NULL); if (r > worst) worst = r; }
    sim_run(1.5f);
    check(worst < 3.0f, "the bands glide to the new chord without a click (worst jump %.1fx the signal's own)", (double)worst);
    float f2 = m_pitch_at(sim_t - 0.5f, sim_t, 100, 4000);
    check(bands_in_chord() == (int)LP(L_HOWL, W_BANDS) && in_chord(f2) && m_rms(sim_t - 0.5f, sim_t) > 0.05f,
          "and lands on it: every band on the new chord, the howl at %.0f Hz (MIDI %.2f)", (double)f2, (double)hz_to_midi(f2));
    sim_gif_end();
    sim_shot("howl_new_chord");
    song.degree = deg0; sim_run(1.5f);

    // safety: the jam's own output back into the mic, four times too loud, every control thrown about
    air.gain = 4.0f; howl_out_peak();
    float t0 = sim_t;
    uint32_t s = 0xfeedu;
    for (int k = 0; k < 100; k++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        float u = (float)((s >> 8) & 1023) / 1023.0f;
        switch (s % 8u) {
        case 0: sim_tilt(-40.0f + 80.0f * u, -30.0f + 80.0f * u); break;
        case 1: wset(W_GAIN, 100.0f * u); break;
        case 2: wset(W_RING, 6.0f + 144.0f * u); break;
        case 3: wset(W_MODE, (float)(s % 3u)); break;
        case 4: wset(W_ROOM, 100.0f * u); wset(W_SHIFT, -20.0f + 40.0f * u); break;
        case 5: wset(W_COLOUR, (float)(s % 4u)); wset(W_HEAT, 1.0f + 9.0f * u); break;
        case 6: song.degree = (int)(s % 7u); break;
        case 7: SI.joy = true; SI.jx = 2.0f * u - 1.0f; SI.jy = 1.0f; break;
        }
        wset(W_LEVEL, -6); wset(W_SWELL, 0.2f);
        sim_run(0.2f);
    }
    float pk = howl_out_peak(), mr = max_rms(t0, sim_t, 0.05f);
    check(pk <= 0.625f * 1.01f, "20 s of abuse, a speaker 4x too loud: HOWL's output peaks at %.3f, never past 1.25x HOWL LEVEL (0.625); loudest 50 ms of the mix %.3f rms", (double)pk, (double)mr);
    float jump = 0;
    for (float t = t0 + 0.02f; t + 0.02f <= sim_t; t += 0.02f) { float a = m_rms(t - 0.02f, t), b = m_rms(t, t + 0.02f); if (a > 0.003f && b / a > jump) jump = b / a; }
    check(jump < 8.0f, "nothing jumps out: the biggest rise between two 20 ms windows is %.1fx", (double)jump);
    air.gain = 0; SI.joy = false; SI.jx = SI.jy = 0;
    for (int i = 0; i < 16; i++) LP_SET(L_HOWL, i, param(layer_base[L_HOWL] + i)->def);
    wset(W_SEND, 0); song.degree = deg0;

    // FREEZE: it keeps howling under another live layer (with the mic cut); without it, a 50 ms fade
    sim_tilt(0, 30); sim_run(3.0f);
    float before = m_rms(sim_t - 0.5f, sim_t);
    wset(W_FREEZE, 1); sim_run(0.3f);
    go_live(L_DRUMS);
    sim_run(3.0f);
    float held = m_rms(sim_t - 0.5f, sim_t);
    check(host_live() != L_HOWL && held > 0.5f * before && !howl_sleeping(), "FREEZE on, DRUMS live: HOWL keeps howling under it (rms %.3f, was %.3f)", (double)held, (double)before);
    sim_shot("howl_frozen_under_drums");
    wset(W_FREEZE, 0);
    float tf = sim_t;
    sim_run(0.3f);
    float gone = max_rms(tf + 0.08f, sim_t, 0.02f);
    check(gone < 0.002f && howl_sleeping(), "FREEZE off while another layer is live: faded out within 80 ms (rms after %.4f) and asleep", (double)gone);
    go_live(L_HOWL); sim_tilt(0, 30); sim_run(3.0f);
    float back = m_rms(sim_t - 0.5f, sim_t);
    float tl = sim_t;
    go_live(L_DRUMS);
    sim_run(0.4f);
    float fade_rms = max_rms(tl + 0.1f, sim_t, 0.02f);
    check(back > 0.05f && fade_rms < 0.002f, "live again it howls (rms %.3f); leaving it fades it out (rms %.4f after 100 ms)", (double)back, (double)fade_rms);

    // MIDI on HOWL's channel (base + 2) while it howls
    int ons = 0, ch = ((int)pv[CP(CP_MIDI_CH)] - 1 + 2) % 16;
    for (int i = 0; i < midi_n; i++) if (midi_log[i].st == (uint8_t)(0x90 | ch) && midi_log[i].d2) ons++;
    check(ons > 0, "MIDI: %d notes for the howling band on channel %d", ons, ch + 1);

    // cost: live (the worst case: 8 bands, the room, the shifter, heat), frozen under another layer, asleep
    float in[AUDIO_BLOCK], dry[AUDIO_BLOCK], snd[AUDIO_BLOCK];
    for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.002f * sim_noise();
    wset(W_BANDS, 8); wset(W_ROOM, 50); wset(W_SHIFT, 3); wset(W_HEAT, 4); sim_run(0.2f);
    struct timespec a, b;
    double us[3];
    for (int k = 0; k < 3; k++) {
        bool live = k == 0;
        if (k == 1) wset(W_FREEZE, 1);
        if (k == 2) { wset(W_FREEZE, 0); for (int j = 0; j < 100; j++) { memset(dry, 0, sizeof dry); memset(snd, 0, sizeof snd); layer_howl.audio(in, dry, snd, AUDIO_BLOCK, false); } }
        const int N = 20000;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (int j = 0; j < N; j++) { memset(dry, 0, sizeof dry); memset(snd, 0, sizeof snd); layer_howl.audio(in, dry, snd, AUDIO_BLOCK, live); }
        clock_gettime(CLOCK_MONOTONIC, &b);
        us[k] = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / N;
    }
    metric("howl_live_us_per_block", us[0]); metric("howl_frozen_us_per_block", us[1]); metric("howl_asleep_us_per_block", us[2]);
    check(us[0] <= 6.0 && us[2] < 0.1, "cost per 64-frame block on the host: %.2f us live (worst case), %.2f frozen under another layer, %.3f asleep", us[0], us[1], us[2]);
    for (int i = 0; i < 16; i++) LP_SET(L_HOWL, i, param(layer_base[L_HOWL] + i)->def);
    solo(false);
    sim_tilt(0, 0);
}
