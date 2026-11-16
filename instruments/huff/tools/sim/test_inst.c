// HUFF on the host: breath noise into the mic; every model held on every note of the ladder and its pitch
// measured; overblowing; silence without breath (and with the speaker leaking into the mic); the bend,
// MIDI in and out, the harmony voice, humming, BREATH SETUP, the tilt ladder's hysteresis; GIFs and screens.
// Last, rev E (the Vessel): the left knob's octaves and the hand's mute, vibrato and bends, and that the board with
// the joystick has none of them.
#include "sim.h"
#include "looper.h"
#include "link.h"
#include "oled.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

bool sim_menu_param(int p);
void sim_menu_close(void);

// param indexes, as instrument.c's enum
enum {
    P_MODEL, P_SCALE, P_KEY, P_OCT, P_NOTES,
    P_SOURCE, P_SNAP, P_SPAN, P_GLIDE, P_BENDR, P_LEAN, P_JOYY, P_TUNE,
    P_SENSE, P_CURVE, P_GATE, P_OVER, P_BPITCH, P_BSETUP,
    P_AIR, P_CHIFF, P_BRIGHT, P_VIB, P_VIBR, P_VIBD, P_HARM, P_HLVL,
    P_REVERB, P_SIZE, P_DAMP, P_ECHO, P_ETIME, P_EFB, P_BRANGE,
    P_ESYNC, P_TEMPO, P_LINK, P_GROUP, P_RANGE, P_PEERS,
    P_DRIVE, P_NOISE, P_BWIND,
    P_HAND,
};
#define NM 13
int huff_peers(void);
int huff_sleep(void);
float huff_breath(void);
float huff_dbg(int k);
static const char *const model_name[NM] = { "OCARINA", "FLUTE", "PAN PIPE", "SHAKUHACHI", "CLARINET", "WHISTLE", "BOTTLE",
                                            "TRUMPET", "TROMBONE", "DIDGERIDOO", "GUITAR", "SYNTH LEAD", "CHOIR" };
static const int model_shift[NM] = { 0, 0, 0, 0, 0, 12, -12, 0, -12, -36, -12, 0, -12 };

// ---- the mic: breath is low rumble; plus a little room; plus, if asked, the speaker leaking back, music (a bass line, a
// chord, a kick on every beat), someone talking (band noise in syllables), a hummed note
typedef struct { float level, lp, lp2, leak, hum_hz, hum_ph, music, mph[4], kt, speech, sp1, sp2, sp_t; } mic_t;
static mic_t M;
static float mic(float t, void *u)
{
    mic_t *b = u;
    b->lp += 0.03f * (sim_noise() - b->lp); b->lp2 += 0.03f * (b->lp - b->lp2);   // wind on a MEMS mic: rumble, 12 dB/oct down above ~150 Hz
    float x = 0.0015f * sim_noise() + b->level * b->lp2 * 50.0f;   // a board reads a normal breath near 0.6 (2026-10-22)
    if (b->leak > 0) { int i = (int)(t * AUDIO_SR) - 200; if (i >= 0 && i < sim_n) x += b->leak * sim_out[i]; }
    if (b->music > 0) {
        static const float hz[4] = { 55.0f, 220.0f, 277.2f, 329.6f };
        float m = 0;
        for (int k = 0; k < 4; k++) { b->mph[k] += hz[k] / AUDIO_SR; if (b->mph[k] >= 1) b->mph[k] -= 1; m += (k ? 0.12f : 0.5f) * (2.0f * b->mph[k] - 1.0f); }
        b->kt += 1.0f / AUDIO_SR; if (b->kt >= 0.5f) b->kt = 0;
        m += 1.2f * sinf(6.2831853f * 60.0f * b->kt) * expf(-b->kt / 0.06f);        // the kick's thump
        if (b->kt < 0.008f) m += 0.8f * sim_noise() * (1.0f - b->kt / 0.008f);        // ... and its beater's click
        x += b->music * m;
    }
    if (b->speech > 0) {
        float w = sim_noise();
        b->sp1 += 0.056f * (w - b->sp1); float hp = w - b->sp1;         // 300 Hz up
        b->sp2 += 0.45f * (hp - b->sp2);                                // ... to about 3 kHz
        b->sp_t += 1.0f / AUDIO_SR;
        float syl = 0.5f + 0.5f * sinf(6.2831853f * 4.0f * b->sp_t);
        x += b->speech * b->sp2 * syl * syl * 2.0f;
    }
    if (b->hum_hz > 0) {                                          // a hummed note: a soft sawtooth
        b->hum_ph += b->hum_hz / AUDIO_SR; if (b->hum_ph > 1) b->hum_ph -= 1;
        x += 0.5f * (2.0f * b->hum_ph - 1.0f);
    }
    return x;
}
static void blow(float level) { M.level = level; SI.mic = mic; SI.mic_u = &M; }

const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", NULL };   // the radio's switches, not the sound
void inst_excite(void) { blow(0.12f); M.leak = 0; M.hum_hz = 0; M.music = M.speech = 0; sim_tilt(0, 0); SI.jx = SI.jy = 0; }

// the ladder as instrument.c builds it
static int ladder(int *notes)
{
    static const struct { int n; int st[12]; } sc[12] = {
        { 5, { 0, 2, 4, 7, 9 } }, { 5, { 0, 3, 5, 7, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 11 } }, { 7, { 0, 2, 3, 5, 7, 8, 10 } },
        { 7, { 0, 2, 3, 5, 7, 9, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 10 } }, { 7, { 0, 1, 4, 5, 7, 8, 10 } }, { 6, { 0, 3, 5, 6, 7, 10 } },
        { 5, { 0, 1, 5, 7, 10 } }, { 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } }, { 7, { 0, 1, 3, 5, 7, 8, 10 } }, { 7, { 0, 2, 3, 5, 7, 8, 11 } } };
    int s = (int)pv[P_SCALE], n = sc[s].n, N = (int)pv[P_NOTES], r = N / 3, base = 72 + (int)pv[P_KEY] + 12 * (int)pv[P_OCT];
    for (int k = 0; k < N; k++) { int d = k - r, oc = d >= 0 ? d / n : -((-d + n - 1) / n), st = d - oc * n; notes[k] = base + 12 * oc + sc[s].st[st]; }
    return N;
}
static float lean_for(int k, int N) { return (((float)k + 0.5f) / (float)N - 0.5f) * pv[P_SPAN]; }
static float midi_hz_(float m) { return pv[P_TUNE] * powf(2.0f, (m - 69.0f) / 12.0f); }

// every note of the ladder for one model: worst error in cents, notes that did not sound
static void sweep(int model, float level, float *worst, int *silent, float *rms_avg, bool show)
{
    param_set(P_MODEL, (float)model);
    int notes[16], N = ladder(notes);
    *worst = 0; *silent = 0; *rms_avg = 0;
    for (int k = 0; k < N; k++) {
        sim_tilt(0, lean_for(k, N));
        blow(0); sim_run(0.25f);
        blow(level); sim_run(0.9f);
        float want = midi_hz_((float)(notes[k] + model_shift[model]));
        if (want > 2000.0f && (model == 1 || model == 2 || model == 3 || model == 5)) want *= 0.5f;   // jets fold above B6
        float f = m_pitch_at(sim_t - 0.4f, sim_t, want * (model == 11 ? 0.4f : 0.75f), want * 1.33f);
        if (model == 11 && f > 0 && f < 0.75f * want) f *= 2.0f;      // the lead's sub octave
        float cents = f > 0 ? 1200.0f * log2f(f / want) : 9999.0f;
        float r = m_rms(sim_t - 0.4f, sim_t);
        *rms_avg += r / (float)N;
        if (show) note("%-10s %3d  want %7.1f Hz  got %7.1f Hz  %+6.1f cents  rms %.3f", model_name[model], notes[k] + model_shift[model], (double)want, (double)f, (double)cents, (double)r);
        if (f <= 0) (*silent)++;
        else if (fabsf(cents) > *worst) *worst = fabsf(cents);
    }
    blow(0); sim_run(0.3f);
}

// the first sample over 0.01 from t0 on, as a time (t0 if none)
static float onset_after(float t0)
{
    int i = (int)(t0 * AUDIO_SR);
    while (i < sim_n && fabsf(sim_out[i]) < 0.01f) i++;
    return i < sim_n ? (float)i / (float)AUDIO_SR : t0;
}

static int midi_count(float t0, int status, int *offs)
{
    int on = 0, off = 0;
    for (int i = 0; i < midi_n; i++) {
        if (midi_log[i].t < t0) continue;
        int k = midi_log[i].st & 0xF0;
        if (k == status && status == 0x90 && midi_log[i].d2 > 0) on++;
        else if (status == 0x90 && (k == 0x80 || (k == 0x90 && midi_log[i].d2 == 0))) off++;
        else if (k == status && status != 0x90) on++;
    }
    if (offs) *offs = off;
    return on;
}

// HUFF_CAL=1 ./run.sh inst: pitch error and level for each model against a steady breath (MIDI CC 2)
static void calibrate(void)
{
    param_set(P_VIB, 0); param_set(P_BPITCH, 0); param_set(P_REVERB, 0); param_set(P_ECHO, 0);
    param_set(P_OVER, 100);
    blow(0);
    static const float bs[4] = { 0.2f, 0.4f, 0.6f, 0.8f };
    int m0 = getenv("HUFF_M") ? atoi(getenv("HUFF_M")) : 1, m1 = getenv("HUFF_M") ? m0 + 1 : NM;
    for (int m = m0; m < m1; m++) {
        param_set(P_MODEL, (float)m);
        printf("model %s\n", model_name[m]);
        for (int st = -36; st <= 36; st += 4) {
            float mid = 60.0f + (float)st;
            printf("  midi %3.0f:", (double)(mid + (float)model_shift[m]));
            for (int L = 0; L < 4; L++) {
                front_midi(0x90, (uint8_t)mid, 100);
                front_midi(0xB0, 2, 0); sim_run(0.2f);
                front_midi(0xB0, 2, (uint8_t)(bs[L] * 127.0f)); sim_run(0.8f);
                float want = midi_hz_(mid + (float)model_shift[m]);
                float f = m_pitch_at(sim_t - 0.4f, sim_t, want * (m == 11 ? 0.4f : 0.75f), want * 1.33f);
                if (m == 11 && f > 0 && f < 0.75f * want) f *= 2.0f;      // the lead's sub octave
                printf("  b%.1f %+6.1fc %.3f", (double)bs[L], f > 0 ? (double)(1200.0f * log2f(f / want)) : 999.0, (double)m_rms(sim_t - 0.4f, sim_t));
                front_midi(0x80, (uint8_t)mid, 0);
            }
            printf("\n");
        }
    }
    front_midi(0xB0, 2, 0);
}

// ---- by ear, in numbers: no click when the note changes, a clean overblow, no zipper from the effects' knobs,
// WHISTLE on B6 with vibrato stays in one octave (the jet's fold has hysteresis). The WAVs are there to listen to.
// the core's click measure (m_click: the first difference's spike in the 6 ms after a change against the signal's own
// around it; over 3 is a click) at each change time, the worst
static float worst_click(const float *at, int n)
{
    float worst = 0;
    for (int i = 0; i < n; i++) { float r = m_click(at[i], NULL, NULL); if (r > worst) worst = r; }
    return worst;
}
// ... and over a stretch: the same, every 10 ms
static float worst_click_over(float t0, float t1)
{
    float worst = 0;
    for (float t = t0; t < t1; t += 0.01f) { float r = m_click(t, NULL, NULL); if (r > worst) worst = r; }
    return worst;
}
static void by_ear(void)
{
    section("by ear");
    int notes[16], N = ladder(notes);
    for (int m = 0; m < NM; m++) {                                 // a run up and down the ladder on each model, blowing through
        param_set(P_MODEL, (float)m); param_set(P_REVERB, 0); param_set(P_ECHO, 0); param_set(P_VIB, 0);
        blow(0); sim_tilt(0, lean_for(2, N)); sim_run(0.5f);
        blow(0.12f); sim_run(0.5f);
        float t0 = sim_t, at[10];
        static const int walk[] = { 3, 4, 5, 6, 7, 6, 5, 4, 3, 2 };
        for (int i = 0; i < 10; i++) { sim_tilt(0, lean_for(walk[i], N)); sim_tick(); at[i] = sim_t; sim_run(0.25f); }
        float wc = worst_click(at, 10);
        char key[48]; snprintf(key, sizeof key, "note_change_click_%s", model_name[m]); metric(key, wc);
        check(wc < 3.0f, "%s: a run up and down the ladder, no click at a note change (worst spike %.2f of the signal's own)", model_name[m], (double)wc);
        if (m == 0 || m == 1 || m == 4 || m == 7 || m == 10 || m == 11 || m == 12) { char sfx[32]; snprintf(sfx, sizeof sfx, "-run-%d", m); sim_wav(sfx, t0 - 0.3f, sim_t); }
        blow(0); sim_run(0.4f);
    }
    // the overblow: FLUTE from soft to hard and back, the jump itself without a click
    param_set(P_MODEL, 1); sim_tilt(0, lean_for(3, N)); blow(0.1f); sim_run(0.8f);
    float t0 = sim_t; blow(0.6f); sim_run(1.0f); blow(0.1f); sim_run(1.0f);
    float wc = worst_click_over(t0, sim_t);
    metric("overblow_click", wc);
    check(wc < 3.0f, "FLUTE blown hard and soft again: the octave jumps without a click (worst spike %.2f of the signal's own)", (double)wc);
    sim_wav("-overblow", t0 - 0.5f, sim_t);
    // WHISTLE on B6 with full vibrato: one octave, not two
    param_set(P_MODEL, 5); param_set(P_VIB, 100); param_set(P_VIBD, 0);
    blow(0); sim_run(0.3f); front_midi(0x90, 83, 100); front_midi(0xB0, 2, 70); sim_run(1.5f);
    int lo = 0, hi = 0;
    for (int i = 0; i < 16; i++) { float f = m_pitch_at(sim_t - 1.0f + 0.06f * (float)i, sim_t - 0.94f + 0.06f * (float)i, 700, 2600); if (f > 1400) hi++; else if (f > 700) lo++; }
    front_midi(0x80, 83, 0); front_midi(0xB0, 2, 0); param_set(P_VIB, 15); param_set(P_VIBD, 0.3f); sim_run(0.5f);
    check((lo == 0) != (hi == 0), "WHISTLE on B6 with full vibrato stays in one octave (%d windows low, %d high)", lo, hi);
    // the effects' knobs while a note sounds: no zipper (steps of 5 every 60 ms)
    param_set(P_MODEL, 0); param_set(P_REVERB, 0); param_set(P_ECHO, 0); param_set(P_EFB, 50); param_set(P_ETIME, 0.3f);
    sim_tilt(0, lean_for(4, N)); blow(0.12f); sim_run(1.0f);
    t0 = sim_t;
    for (int v = 0; v <= 100; v += 5) { param_set(P_REVERB, (float)v); sim_run(0.06f); }
    for (int v = 0; v <= 100; v += 5) { param_set(P_ECHO, (float)v); sim_run(0.06f); }
    for (int v = 100; v >= 0; v -= 5) { param_set(P_SIZE, (float)v); sim_run(0.06f); }
    for (int v = 0; v <= 100; v += 5) { param_set(P_DAMP, (float)v); sim_run(0.06f); }
    for (int i = 0; i <= 20; i++) { param_set(P_ETIME, 0.3f - 0.012f * (float)i); sim_run(0.06f); }
    wc = worst_click_over(t0, sim_t);
    metric("fx_knobs_click", wc);
    check(wc < 3.0f, "REVERB, ECHO, ROOM SIZE, DAMPING and ECHO TIME turned while a note sounds: no zipper (worst spike %.2f of the signal's own)", (double)wc);
    sim_wav("-fx-knobs", t0, sim_t);
    blow(0); param_set(P_REVERB, 30); param_set(P_ECHO, 0); param_set(P_EFB, 35); param_set(P_ETIME, 0.33f); param_set(P_SIZE, 55); param_set(P_DAMP, 45);
    front_midi(0xC0, 0, 0); sim_run(3.0f);
}

// HUFF_SOAK=<seconds a model> ./run.sh inst: each model for that long (3600 is an hour each) with everything going at
// once: gusty breath with pauses, the tilt swept, a sound every bar (the model put back), REC takes back to back, MIDI
// notes and CC 2, the tempo moved by a linked board (the echo on its beat), BREATH SETUP now and then. The recording
// is dropped as it goes; each 0.3 s step is measured on its own tail: no NaN, under full scale, never silent for long
// while blown; at the end of each model the breath stops and the note stops with it, and a normal breath still reads
// (the room's floor may have crept up under a long soft breath: it comes down the moment the breath stops).
static void soak(float per)
{
    section("a long time on each model");
    static const char *const names[7] = { "OCARINA", "FLUTE", "PAN PIPE", "SHAKUHACHI", "CLARINET", "WHISTLE", "BOTTLE" };
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
    sim_peer_use(k); { uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 0); } sim_peer_use(-1);
    sim_run(1.0f);
    int ok = 0;
    uint32_t rs = 0x1234567u;
    for (int m = 0; m < 7; m++) {
        float t0 = sim_t, pk = 0, longest_quiet = 0, worst_floor = 0; int quiet_run = 0, nan = 0, step = 0, snd = 0, takes = 0, tempos = 0, notes_sent = 0;
        front_midi(0xC0, 0, 0); param_set(P_MODEL, (float)m); blow(0); sim_run(0.5f);
        float level = 0.1f; int note_on = -1; bool blowing = true;
        while (sim_t - t0 < per) {
            rs = rs * 1664525u + 1013904223u;
            // the breath: a random walk 0.03..0.3, a pause every so often, a gust now and then
            level += 0.02f * (float)((int)(rs >> 24) % 7 - 3);
            level = level < 0.03f ? 0.03f : level > 0.3f ? 0.3f : level;
            if (step % 37 == 0) blowing = !blowing;
            blow(blowing ? ((step % 53 == 10) ? 0.5f : level) : 0.0f);
            if (step % 7 == 2) sim_tilt((float)((step % 5) - 2) * 10.0f, (float)((step % 11) - 5) * 8.0f);
            if (step % 20 == 5) { front_midi(0xC0, (uint8_t)(snd++ % 14), 0); param_set(P_MODEL, (float)m); }
            if (step % 23 == 0) {                                       // the tempo from the linked board, 70..170
                tempos++;
                uint32_t fpb = 60u * AUDIO_SR / (uint32_t)(70 + (tempos * 29) % 100);
                sim_peer_use(k); uint32_t o; float f; link_origin(&o, &f); link_set_time(fpb, o, 4, 0); sim_peer_use(-1);
                param_set(P_ESYNC, (float)(tempos % 6)); param_set(P_ECHO, 40);
            }
            if (step % 17 == 5) { SI.jx = (float)((step % 3) - 1); SI.jy = (float)((step % 5) - 2) * 0.5f; }
            if (step % 13 == 4) { if (note_on >= 0) front_midi(0x80, (uint8_t)note_on, 0); note_on = 55 + (int)((rs >> 16) % 30); front_midi(0x90, (uint8_t)note_on, 100); notes_sent++; }
            if (step % 13 == 9 && note_on >= 0) { front_midi(0x80, (uint8_t)note_on, 0); note_on = -1; }
            if (step % 31 == 7) front_midi(0xB0, 2, (uint8_t)((rs >> 8) % 100));
            if (step % 31 == 12) front_midi(0xB0, 2, 0);
            if (step % 50 == 20) { sim_rec(1.2f); sim_rec(1.6f); takes += 2; }
            if (step % 97 == 40) { sim_rec(0.1f); sim_run(1.5f); sim_rec(0.1f); }
            if (step % 400 == 150) { sim_menu_param(P_BSETUP); sim_tap(); blow(0); sim_run(2.4f); blow(0.1f); sim_run(3.6f); }
            sim_run(0.3f);
            int n = 9600 < sim_n ? 9600 : sim_n;
            const float *x = sim_out + sim_n - n;
            double e = 0; float p = 0;
            for (int i = 0; i < n; i++) { float a = fabsf(x[i]); if (a > p) p = a; e += (double)x[i] * x[i]; if (x[i] != x[i]) nan++; }
            if (p > pk) pk = p;
            if (blowing && sqrt(e / n) < 0.002) quiet_run++; else quiet_run = 0;
            if ((float)quiet_run * 0.3f > longest_quiet) longest_quiet = (float)quiet_run * 0.3f;
            if (huff_dbg(1) > worst_floor) worst_floor = huff_dbg(1);
            sim_n = 0;
            step++;
        }
        // the note stops with the breath, and a normal breath still reads
        if (note_on >= 0) front_midi(0x80, (uint8_t)note_on, 0);
        front_midi(0xB0, 2, 0); front_midi(0xB0, 123, 0); SI.jx = SI.jy = 0; sim_tilt(0, 0);
        blow(0); sim_run(2.5f);
        sim_n = 0; sim_run(1.0f);
        float after = m_rms(sim_t - 0.8f, sim_t);
        blow(0.12f); sim_run(1.0f);
        float reads = huff_breath();
        blow(0); sim_run(1.0f); sim_n = 0;
        bool good = nan == 0 && pk < 0.99f && longest_quiet < 3.0f && after < 0.003f && reads > 0.3f && worst_floor < 0.3f;
        if (good) ok++;
        note("%s for %.0f s: %d sounds, %d takes, %d tempos, %d MIDI notes; peak %.3f, longest quiet while blown %.1f s, NaN %d, floor at most %.4f; "
             "breath off: %.5f rms; a normal breath reads %.2f%s", names[m], (double)(sim_t - t0), snd, takes, tempos, notes_sent, (double)pk, (double)longest_quiet, nan, (double)worst_floor, (double)after, (double)reads, good ? "" : " BAD");
    }
    check(ok == 7, "%.0f s on each model with everything going: no NaN, under full scale, never silent while blown, the note stops with the breath, the breath floor recovers (%d of 7)", (double)per, ok);
    param_set(P_ESYNC, 0); param_set(P_ECHO, 0);
    sim_peer_power(k, false); sim_run(1.5f);
}

// ---- rev E (the Vessel): the left knob and the hand. No rev E has been built: this is how they are checked.
// the level of what was played from fc up (eight one-pole high passes a sixth under fc: a note at a third of fc is
// 69 dB down, its 2nd harmonic 33), between two times. With fc at 3x the note: its harmonics from the 3rd up, and the air
static float hi_rms(float t0, float t1, float fc)
{
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR);
    if (a < 0) a = 0;
    if (b > sim_n) b = sim_n;
    float k = 1.0f - expf(-6.2831853f * fc / 1.2f / (float)AUDIO_SR), z[8] = { 0 };
    double e = 0; int n = 0;
    for (int i = a > 3200 ? a - 3200 : 0; i < b; i++) {
        float x = sim_out[i];
        for (int j = 0; j < 8; j++) { z[j] += k * (x - z[j]); x -= z[j]; }
        if (i >= a) { e += (double)x * x; n++; }
    }
    return n ? (float)sqrt(e / n) : 0.0f;
}
// the pitch of what was played, read below 1.3x the wanted note (four one-pole low passes first): the autocorrelation's
// peak then has the same shape whatever the tone above is, so a darker tone does not read as another pitch
static float pitch_low(float t0, float t1, float want, bool sub)
{
    static float buf[AUDIO_SR];
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR);
    if (a < 0) a = 0;
    if (b > sim_n) b = sim_n;
    if (b - a > AUDIO_SR) a = b - AUDIO_SR;
    float k = 1.0f - expf(-6.2831853f * 1.3f * want / (float)AUDIO_SR), z[4] = { 0, 0, 0, 0 };
    for (int i = a > 3200 ? a - 3200 : 0; i < b; i++) {
        float x = sim_out[i];
        for (int j = 0; j < 4; j++) { z[j] += k * (x - z[j]); x = z[j]; }
        if (i >= a) buf[i - a] = x;
    }
    float f = b > a ? m_pitch(buf, b - a, want * (sub ? 0.4f : 0.75f), want * 1.33f) : 0.0f;
    return sub && f > 0 && f < 0.75f * want ? 2.0f * f : f;          // the lead's sub octave, as the sweep reads it
}
// the largest step from one sample to the next
static float max_step(float t0, float t1)
{
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR);
    if (a < 1) a = 1;
    if (b > sim_n) b = sim_n;
    float m = 0;
    for (int i = a; i < b; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > m) m = d; }
    return m;
}
static float db(float a, float b) { return 20.0f * log10f((a + 1e-9f) / (b + 1e-9f)); }
static float cents(float f, float want) { return f > 0 ? 1200.0f * log2f(f / want) : 9999.0f; }
// a steady breath from MIDI CC 2 (0.63: under the overblow), kept up (CC 2 lets go after 2 s without news)
#define STEADY 80
static void held(float s) { while (s > 0.001f) { front_midi(0xB0, 2, STEADY); float d = s > 0.5f ? 0.5f : s; sim_run(d); s -= d; } }

// the coach's hint is up: its dot with the "i" at the left of the bottom strip
static bool hint_up(void) { return oled_get(1, 55) && oled_get(3, 55) && oled_get(4, 55) && !oled_get(5, 55) && oled_get(6, 55) && oled_get(7, 55) && oled_get(9, 55); }
static bool wait_hint(float s) { for (float t = 0; t < s; t += 0.2f) { sim_run(0.2f); if (hint_up()) return true; } return false; }

static void rev_e(void)
{
    sim_in_t keep = SI;
    uint32_t learned = meta.learned;
    float hints = pv[CP(CP_HINTS)];
    front_midi(0xC0, 0, 0); sim_run(0.5f);                          // OCARINA, the first sound, as it comes
    param_set(P_REVERB, 0); param_set(P_ECHO, 0); param_set(P_VIB, 0); param_set(P_BPITCH, 0);
    blow(0); sim_tilt(0, 0); SI.jx = SI.jy = 0; sim_run(1.5f);
    int notes[16], N = ladder(notes);
    const ctl_t *c = front_ctl();
    float w0 = midi_hz_((float)notes[3]);

    // ---- the board with the joystick: no left knob, no sensor. Turns and a hand that reach its pins do nothing,
    // whatever HAND DOES says
    section("the board with the joystick: nothing new");
    sim_tilt(0, lean_for(3, N)); held(1.5f);
    {   float f_a = m_pitch_at(sim_t - 0.6f, sim_t, w0 * 0.75f, w0 * 1.33f), r_a = m_rms(sim_t - 0.6f, sim_t), h_a = hi_rms(sim_t - 0.6f, sim_t, 3.0f * w0);
        float seen = 0; bool parts = false; int oct = 0;
        float f_b = 0, r_b = 0, h_b = 0, worst_c = 0, worst_r = 0, worst_h = 0;
        for (int hd = 0; hd < 4; hd++) {
            param_set(P_HAND, (float)hd);
            SI.hand_mm = 40; sim_lturn(hd & 1 ? -2 : 3);
            for (int i = 0; i < 8; i++) {
                held(0.1f);
                seen = fmaxf(seen, fmaxf(fmaxf(fabsf(huff_dbg(11)), fabsf(huff_dbg(12))), fmaxf(fabsf(huff_dbg(13)), fabsf(huff_dbg(14)))));
                if (c->has_lknob || c->has_hand || c->hand != 0.0f || c->lknob) parts = true;
                if ((int)pv[P_OCT] != 0) oct++;
            }
            f_b = m_pitch_at(sim_t - 0.6f, sim_t, w0 * 0.75f, w0 * 1.33f); r_b = m_rms(sim_t - 0.6f, sim_t); h_b = hi_rms(sim_t - 0.6f, sim_t, 3.0f * w0);
            worst_c = fmaxf(worst_c, fabsf(cents(f_b, f_a))); worst_r = fmaxf(worst_r, fabsf(db(r_b, r_a))); worst_h = fmaxf(worst_h, fabsf(db(h_b, h_a)));
        }
        sim_shot("joystick_board");
        check(!parts && oct == 0 && seen == 0.0f && worst_c < 3.0f && worst_r < 1.0f && worst_h < 1.5f,
              "a board with the joystick, the left knob's pins turned and a hand at 40 mm, HAND DOES on each of its four: OCTAVE stays %d, "
              "the mute, the bend and the vibrato stay 0 (largest %.4f), the note moves %.1f cents, the level %.2f dB, the part above 3x the note %.2f dB",
              (int)pv[P_OCT], (double)seen, (double)worst_c, (double)worst_r, (double)worst_h);
        SI.hand_mm = 0; SI.lk = 0; param_set(P_HAND, 0); param_set(P_OCT, 0);
        // the stick still bends, as before
        SI.jx = 1.0f; held(0.8f);
        float f = m_pitch_at(sim_t - 0.4f, sim_t, w0, w0 * 1.5f);
        check(fabsf(cents(f, w0) - 100.0f * pv[P_BENDR]) < 15.0f, "... and its stick still bends by BEND RANGE (%.0f cents)", (double)cents(f, w0));
        SI.jx = 0; front_midi(0xB0, 2, 0); sim_run(1.0f);
        // the two new hints are not for this board: with everything else learned, the coach has nothing to say
        pv[CP(CP_HINTS)] = 1; meta.learned = ~((1u << LEARN_LKNOB) | (1u << LEARN_HAND));
        sim_run(5.0f);                                                // a hint that was up has gone by now (4.5 s)
        bool shown = wait_hint(20.0f);
        meta.learned = ~(1u << LEARN_JOY);
        bool joy_hint = wait_hint(20.0f);
        check(!shown && joy_hint, "the hints for the left knob and the hand never show on this board (20 s idle: %s); the stick's does (%s)", shown ? "SHOWN" : "none", joy_hint ? "yes" : "NO");
        meta.learned = ~0u; sim_run(5.0f);
    }

    // ---- rev E from here on
    sim_rev_e(); sim_run(0.3f);
    check(c->has_lknob && c->has_hand && !c->has_joy, "rev E: the left knob and the sensor, no joystick");
    // the two hints, each alone (the others learned), as the idle player gets them; doing what one says ends it
    {   pv[CP(CP_HINTS)] = 1;
        meta.learned = ~(1u << LEARN_LKNOB);
        sim_run(5.0f);
        bool h_knob = hint_up() || wait_hint(20.0f); sim_shot("rev_e_hint_knob");
        sim_lturn(1); sim_run(0.3f); bool knob_done = (meta.learned >> LEARN_LKNOB) & 1 && !hint_up(); sim_lturn(-1);
        meta.learned = ~(1u << LEARN_HAND);
        bool h_hand = wait_hint(20.0f); sim_shot("rev_e_hint_hand");
        sim_hand(60, 0.5f); bool hand_done = (meta.learned >> LEARN_HAND) & 1 && !hint_up(); sim_hand(0, 1.0f);
        meta.learned = ~(1u << LEARN_JOY);
        sim_run(5.0f);
        bool none = !wait_hint(20.0f);
        check(h_knob && h_hand && knob_done && hand_done && none, "rev E's two hints show to an idle player (the left knob's %s, the hand's %s), go once the knob has turned (%s) and the hand has come down (%s), and the stick's never shows (%s)",
              h_knob ? "yes" : "NO", h_hand ? "yes" : "NO", knob_done ? "yes" : "NO", hand_done ? "yes" : "NO", none ? "yes" : "NO");
        meta.learned = ~0u; param_set(P_OCT, 0); sim_run(2.0f);
    }

    section("rev E: the left knob is the octave");
    sim_tilt(0, lean_for(3, N)); blow(0.12f); sim_run(1.0f);
    {   float f0 = m_pitch_at(sim_t - 0.4f, sim_t, w0 * 0.75f, w0 * 1.33f);
        static const int turn[10] = { 1, 1, 1, -1, -1, -1, -1, -1, 1, 1 };
        static const int want_o[10] = { 1, 2, 2, 1, 0, -1, -2, -2, -1, 0 };
        float at[10], worst = 0, fs[10]; int right = 0;
        for (int i = 0; i < 10; i++) {
            sim_lturn(turn[i]); at[i] = sim_t;
            if (i == 0) { sim_run(0.3f); sim_shot("rev_e_octave"); sim_run(0.6f); } else sim_run(0.9f);
            float want = w0 * powf(2.0f, (float)want_o[i]);
            fs[i] = m_pitch_at(sim_t - 0.4f, sim_t, want * 0.75f, want * 1.33f);
            float ct = fabsf(cents(fs[i], f0) - 1200.0f * (float)want_o[i]);
            if (ct > worst) worst = ct;
            if ((int)pv[P_OCT] == want_o[i]) right++;
        }
        note("the note at OCTAVE +1 +2 +2 +1 0 -1 -2 -2 -1 0: %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f Hz (from %.1f)", (double)fs[0], (double)fs[1], (double)fs[2],
             (double)fs[3], (double)fs[4], (double)fs[5], (double)fs[6], (double)fs[7], (double)fs[8], (double)fs[9], (double)f0);
        metric("lknob_octave_worst_cents", worst);
        check(right == 10 && worst < 10.0f, "each detent of the left knob moves the note an octave: up two, down four, back (worst %.1f cents off a whole octave over ten turns)", (double)worst);
        check((int)pv[P_OCT] == 0 && fabsf(cents(fs[2], fs[1])) < 5.0f && fabsf(cents(fs[7], fs[6])) < 5.0f,
              "it stops at two up and at two down: a turn more leaves the note where it is (%.1f and %.1f cents)", (double)cents(fs[2], fs[1]), (double)cents(fs[7], fs[6]));
        float wc = worst_click(at, 10);
        metric("lknob_octave_click", wc);
        check(wc < 3.0f, "the octave changes as a note change does, without a click (worst spike %.2f of the signal's own)", (double)wc);
        sim_wav("-rev-e-octaves", at[0] - 0.8f, sim_t);
        // three detents on one tick are three octaves, held at the end; it stays where it is left
        sim_lturn(-1); sim_run(0.2f); sim_lturn(3); sim_run(3.5f);
        check((int)pv[P_OCT] == 2, "three detents at once from -1: it stops at +2, and 3.5 s later it is still there: it is PLAY > OCTAVE itself (%+d)", (int)pv[P_OCT]);
        sim_shot("rev_e_octave_rail");
        // a setting the menu put lower than the knob goes: the knob does not jump it up, and turns up from where it is
        param_set(P_OCT, -4); sim_run(0.2f); sim_lturn(-1); int o1 = (int)pv[P_OCT]; sim_run(0.2f); sim_lturn(1); int o2 = (int)pv[P_OCT];
        sim_run(0.5f); sim_shot("rev_e_octave_low");
        check(o1 == -4 && o2 == -3, "PLAY > OCTAVE at -4 from the menu: a turn down leaves it (%d), a turn up is one octave (%d)", o1, o2);
        param_set(P_OCT, 0); blow(0); sim_run(1.0f);
    }

    section("rev E: a hand over the sensor is a mute");
    float worst_dark = 0, worst_level = 0, worst_click_h = 0, worst_step = 0; int dark_m = 0, level_m = 0;
    note("each model on the ladder's fourth note, a steady breath from CC 2. The highs: what is above 3x the note, or above 875 Hz for the notes under 292 Hz "
         "(the mute closes to 1.2x the note, 350 Hz at least: 2.5x that)");
    for (int m = 0; m < NM; m++) {
        param_set(P_MODEL, (float)m); SI.hand_mm = 0; sim_tilt(0, lean_for(3, N)); sim_run(0.3f);
        float w = midi_hz_((float)(notes[3] + model_shift[m]));
        float top = fmaxf(3.0f * w, 875.0f);                           // the mute closes to 1.2x the note, 350 Hz at least: 2.5x that
        held(1.0f);
        // the note with no hand, for as long as the passage with the hand below
        float t_ref = sim_t;
        held(4.2f);
        float r0 = m_rms(sim_t - 0.6f, sim_t), h0 = hi_rms(sim_t - 0.6f, sim_t, top), f0 = pitch_low(sim_t - 1.0f, sim_t, w, m == 11);
        float wc_ref = worst_click_over(t_ref, sim_t - 0.2f), st_ref = max_step(t_ref, sim_t);
        // the hand comes down at once (the core glides it), sits, and leaves
        float t_hand = sim_t;
        SI.hand_mm = 40; held(1.6f);
        float r1 = m_rms(sim_t - 0.6f, sim_t), h1 = hi_rms(sim_t - 0.6f, sim_t, top), f1 = pitch_low(sim_t - 1.0f, sim_t, w, m == 11);
        float mute = huff_dbg(11);
        if (m == 7) sim_shot("rev_e_mute_down");
        if (m == 0) sim_shot("rev_e_mute_ocarina");
        SI.hand_mm = 0; held(1.0f);
        float r2 = m_rms(sim_t - 0.4f, sim_t);
        // ... and a hand waved fast: down and nearly away five times a second
        for (int i = 0; i < 10; i++) { SI.hand_mm = i & 1 ? 240 : 40; held(0.1f); }
        SI.hand_mm = 0; held(0.6f);
        float wc = worst_click_over(t_hand - 0.05f, sim_t - 0.2f), st = max_step(t_hand, sim_t);
        float dark = db(h1, h0) - db(r1, r0), lvl = db(r1, r0);
        char key[48];
        snprintf(key, sizeof key, "mute_highs_db_%s", model_name[m]); metric(key, db(h1, h0));
        snprintf(key, sizeof key, "mute_level_db_%s", model_name[m]); metric(key, lvl);
        snprintf(key, sizeof key, "mute_click_%s", model_name[m]); metric(key, wc);
        check(mute > 0.97f && db(h1, h0) < -12.0f && dark < -4.0f && lvl > -14.0f && lvl < -3.0f && fabsf(cents(f1, f0)) < 6.0f && fabsf(db(r2, r0)) < 1.5f,
              "%s, a hand at 40 mm: what is above %.0f Hz falls %.1f dB, the level %.1f dB (rms %.3f to %.3f), the note stays (%+.1f cents); "
              "the hand gone, the level is back (%+.1f dB)", model_name[m], (double)top, (double)-db(h1, h0), (double)-lvl, (double)r0, (double)r1, (double)cents(f1, f0), (double)db(r2, r0));
        check(wc < 3.0f && st < 1.5f * st_ref,
              "%s, the hand down at once, there 1.6 s, away, then waved five times a second: no click (worst spike %.2f of the signal's own, %.2f in the "
              "same note for as long with no hand; the largest step between samples %.4f, %.4f with no hand)", model_name[m], (double)wc, (double)wc_ref, (double)st, (double)st_ref);
        if (db(h1, h0) > worst_dark || m == 0) { worst_dark = db(h1, h0); dark_m = m; }
        if (lvl < worst_level) { worst_level = lvl; level_m = m; }
        if (wc > worst_click_h) worst_click_h = wc;
        if (st / (st_ref + 1e-9f) > worst_step) worst_step = st / (st_ref + 1e-9f);
        if (m == 7) sim_wav("-rev-e-mute", t_hand - 1.0f, sim_t);
    }
    note("the mute over the thirteen models: the highs fall least on %s (%.1f dB), the level most on %s (%.1f dB); worst click %.2f, largest step %.2f of the open note's",
         model_name[dark_m], (double)-worst_dark, model_name[level_m], (double)-worst_level, (double)worst_click_h, (double)worst_step);
    metric("mute_highs_db_least", worst_dark); metric("mute_level_db_most", worst_level); metric("mute_click_worst", worst_click_h);
    // the trumpet: the lower the hand the darker, and a waved hand is a wah
    param_set(P_MODEL, 7); SI.hand_mm = 0; held(1.2f);
    {   float w = midi_hz_((float)(notes[3] + model_shift[7]));
        static const int mm[5] = { 0, 200, 145, 90, 40 };
        float h[5], r[5]; bool falling = true;
        for (int i = 0; i < 5; i++) {
            SI.hand_mm = mm[i]; held(1.0f);
            h[i] = hi_rms(sim_t - 0.5f, sim_t, 3.0f * w); r[i] = m_rms(sim_t - 0.5f, sim_t);
            if (i && h[i] >= h[i - 1]) falling = false;
            if (i == 2) sim_shot("rev_e_mute_half");
        }
        check(falling && c->hand_mm == 40, "TRUMPET, the lower the hand the darker: away, then at 200, 145, 90 and 40 mm, what is above 3x the note is at 0, %.1f, %.1f, %.1f, %.1f dB, the level at 0, %.1f, %.1f, %.1f, %.1f dB",
              (double)db(h[1], h[0]), (double)db(h[2], h[0]), (double)db(h[3], h[0]), (double)db(h[4], h[0]), (double)db(r[1], r[0]), (double)db(r[2], r[0]), (double)db(r[3], r[0]), (double)db(r[4], r[0]));
        // waved between 40 and 200 mm twice a second: the highs follow the hand
        sim_gif_begin("rev-e-wah");
        float t0 = sim_t; double lo_e = 0, hi_e = 0; int lo_n = 0, hi_n = 0;
        for (int i = 0; i < 300; i++) {
            SI.hand_mm = 120 + (int)lroundf(80.0f * sinf(6.2831853f * 2.0f * (float)i * 0.01f));
            if (i % 40 == 0) front_midi(0xB0, 2, STEADY);
            sim_tick();
            if (i < 50) continue;
            float e = hi_rms(sim_t - 0.01f, sim_t, 3.0f * w);
            if (c->hand > 0.85f) { lo_e += (double)e * e; lo_n++; } else if (c->hand < 0.4f) { hi_e += (double)e * e; hi_n++; }
        }
        sim_gif_end();
        float swing = db((float)sqrt(hi_e / (hi_n ? hi_n : 1)), (float)sqrt(lo_e / (lo_n ? lo_n : 1)));
        float wc = worst_click_over(t0, sim_t);
        metric("wah_swing_db", swing);
        check(lo_n > 20 && hi_n > 20 && swing > 6.0f && wc < 3.0f, "a hand waved between 40 and 200 mm twice a second is a wah: what is above 3x the note is %.1f dB louder with the hand high than low, no click (worst spike %.2f)", (double)swing, (double)wc);
        sim_wav("-rev-e-wah", t0 - 0.5f, sim_t);
        SI.hand_mm = 0; held(0.8f);
    }
    // the mute is not a sound's: GUITAR SOLO (the amp at 70) and DEEP CHOIR as they come
    {   static const int snd[2] = { 14, 23 }; static const char *const sn[2] = { "GUITAR SOLO", "DEEP CHOIR" };
        for (int k = 0; k < 2; k++) {
            front_midi(0xC0, (uint8_t)snd[k], 0); sim_run(0.5f);
            int nl[24], n = ladder(nl); sim_tilt(0, lean_for(3, n));      // the sound's own ladder, its fourth note,
            SI.hand_mm = 0; front_midi(0xB0, 2, 0); sim_run(2.0f); held(2.5f);   // begun from silence (the guitar's pick sets its tone)
            float r0 = m_rms(sim_t - 0.8f, sim_t), h0 = hi_rms(sim_t - 0.8f, sim_t, 1500.0f);
            float t0 = sim_t;
            SI.hand_mm = 40; held(2.5f);
            float r1 = m_rms(sim_t - 0.8f, sim_t), h1 = hi_rms(sim_t - 0.8f, sim_t, 1500.0f);
            if (getenv("HUFF_TRACE")) note("%s: note %d (%.1f Hz played %.1f), key %d scale %d oct %d drive %.0f vib %.0f rvb %.0f echo %.0f; open rms %.4f highs %.5f, muted rms %.4f highs %.5f", sn[k], nl[3] + model_shift[(int)pv[P_MODEL]],
                 (double)midi_hz_((float)(nl[3] + model_shift[(int)pv[P_MODEL]])), (double)m_pitch_at(sim_t - 0.5f, sim_t, 40, 2000), (int)pv[P_KEY], (int)pv[P_SCALE], (int)pv[P_OCT], (double)pv[P_DRIVE], (double)pv[P_VIB], (double)pv[P_REVERB], (double)pv[P_ECHO], (double)r0, (double)h0, (double)r1, (double)h1);
            if (k == 0) sim_shot("rev_e_mute_guitar");
            SI.hand_mm = 0; held(1.0f);
            float wc = worst_click_over(t0 - 0.05f, sim_t - 0.2f);
            // (the driven guitar loses 5 to 12 dB from one run to the next: its pick is noise, and how bright the string rings after it differs)
            check(db(h1, h0) < -12.0f && db(r1, r0) > -16.0f && wc < 3.0f, "the sound %s with its reverb, echo and amp, a hand at 40 mm: what is above 1.5 kHz falls %.1f dB, the level %.1f dB, no click (%.2f)",
                  sn[k], (double)-db(h1, h0), (double)-db(r1, r0), (double)wc);
        }
        front_midi(0xC0, 0, 0); sim_run(0.5f);
        param_set(P_REVERB, 0); param_set(P_ECHO, 0); param_set(P_VIB, 0); param_set(P_BPITCH, 0); sim_tilt(0, lean_for(3, N)); held(1.0f);
    }

    section("rev E: HAND DOES vibrato, or a bend");
    {   // pitch every 30 ms over 0.9 s: how far it swings
        float pp[2];
        param_set(P_HAND, 1);
        for (int k = 0; k < 2; k++) {
            SI.hand_mm = k ? 40 : 0; held(1.6f);
            float lo = 1e9f, hi = 0;
            for (int i = 0; i < 30; i++) { float f = m_pitch_at(sim_t - 0.93f + 0.03f * (float)i, sim_t - 0.9f + 0.03f * (float)i, w0 * 0.8f, w0 * 1.25f); if (f > 0) { if (f < lo) lo = f; if (f > hi) hi = f; } }
            pp[k] = hi > 0 ? cents(hi, lo) : 0;
            if (k) sim_shot("rev_e_vibrato");
        }
        float r_v = m_rms(sim_t - 0.6f, sim_t);
        check(pp[1] > 60.0f && pp[1] < 130.0f && pp[0] < 12.0f && huff_dbg(11) == 0.0f, "HAND DOES VIBRATO: a hand at 40 mm swings the note %.0f cents end to end (half a semitone each way, as the stick pushed up); no hand, %.0f; nothing is muted (rms %.3f)", (double)pp[1], (double)pp[0], (double)r_v);
        SI.hand_mm = 0; held(0.8f);
        float fa = m_pitch_at(sim_t - 0.4f, sim_t, w0 * 0.75f, w0 * 1.33f);
        param_set(P_HAND, 2);
        float t0 = sim_t;
        SI.hand_mm = 145; held(0.9f); float fh = m_pitch_at(sim_t - 0.4f, sim_t, w0 * 0.75f, w0 * 1.5f);
        SI.hand_mm = 40; held(0.9f); float fu = m_pitch_at(sim_t - 0.4f, sim_t, w0 * 0.75f, w0 * 1.5f);
        sim_shot("rev_e_bend");
        int pbs = midi_count(t0, 0xE0, NULL);
        SI.hand_mm = 0; held(2.4f); float fb = m_pitch_at(sim_t - 0.4f, sim_t, w0 * 0.75f, w0 * 1.33f);
        float wc_u = worst_click_over(t0, sim_t);
        check(fabsf(cents(fu, fa) - 100.0f * pv[P_BENDR]) < 10.0f && fabsf(cents(fh, fa) - 50.0f * pv[P_BENDR]) < 10.0f && fabsf(cents(fb, fa)) < 5.0f && pbs >= 2 && wc_u < 3.0f,
              "HAND DOES BEND UP: at 145 mm the note is %.0f cents up, at 40 mm %.0f (BEND RANGE %d semitones), the hand gone %.0f; it goes out as pitch bend (%d messages), no click (%.2f)",
              (double)cents(fh, fa), (double)cents(fu, fa), (int)pv[P_BENDR], (double)cents(fb, fa), pbs, (double)wc_u);
        param_set(P_HAND, 3);
        SI.hand_mm = 40; held(0.9f); float fd = m_pitch_at(sim_t - 0.4f, sim_t, w0 * 0.6f, w0 * 1.33f);
        SI.hand_mm = 0; held(2.4f); fb = m_pitch_at(sim_t - 0.4f, sim_t, w0 * 0.75f, w0 * 1.33f);
        check(fabsf(cents(fd, fa) + 100.0f * pv[P_BENDR]) < 10.0f && fabsf(cents(fb, fa)) < 5.0f && huff_dbg(11) == 0.0f, "HAND DOES BEND DOWN: at 40 mm the note is %.0f cents down, the hand gone %.0f", (double)cents(fd, fa), (double)cents(fb, fa));
        // HAND DOES changed while the hand is down (the menu, or CC 63): the mute and the bend swap without a click
        param_set(P_HAND, 0); SI.hand_mm = 40; held(1.0f);
        float t_sw = sim_t;
        param_set(P_HAND, 3); held(0.5f); param_set(P_HAND, 0); held(0.5f); param_set(P_HAND, 1); held(0.5f); param_set(P_HAND, 0); held(0.5f);
        float wc_sw = worst_click_over(t_sw - 0.05f, sim_t - 0.2f);
        SI.hand_mm = 0; held(1.0f);
        check(wc_sw < 3.0f, "HAND DOES switched from MUTE to BEND DOWN, back, to VIBRATO and back with the hand at 40 mm: no click (worst spike %.2f)", (double)wc_sw);
        // SYSTEM > HAND OFF: no mute
        param_set(CP(CP_HAND), 0); SI.hand_mm = 40; held(0.8f);
        float off = huff_dbg(11);
        param_set(CP(CP_HAND), 1); SI.hand_mm = 0; held(0.5f);
        check(off == 0.0f, "SYSTEM > HAND OFF: a hand at 40 mm mutes nothing");
    }

    // the cost with the mute on top of everything else: the same budget as without
    section("rev E: cost");
    param_set(P_REVERB, 60); param_set(P_ECHO, 40); param_set(P_HARM, 1); param_set(P_VIB, 15);
    {   double worst = 0; int worst_m = 0, asleep = 0;
        for (int m = 0; m < NM; m++) {
            param_set(P_MODEL, (float)m); SI.hand_mm = 40; held(0.6f);
            float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
            for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.05f * sim_noise();
            double us = 1e9;
            for (int k = 0; k < 8; k++) {
                struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
                for (int r = 0; r < 1000; r++) { front_midi(0xB0, 2, 90); front_audio(in, out, AUDIO_BLOCK); }
                clock_gettime(CLOCK_MONOTONIC, &b);
                double t = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / 1000.0;
                if (t < us) us = t;
            }
            char key[56]; snprintf(key, sizeof key, "host_us_per_block_muted_%s", model_name[m]); metric(key, us);
            if (us > worst) { worst = us; worst_m = m; }
            if (huff_sleep() != 0 || huff_dbg(11) < 0.97f) asleep++;
        }
        check(worst < 20.0 && asleep == 0, "every model with harmony, reverb, echo and the hand's mute fully down: at most %.1f us per block on the host (%s; < 20, the budget without the mute)", worst, model_name[worst_m]);
    }

    // back to the board with the joystick, the first sound, as the tests found it
    SI = keep; meta.learned = learned; pv[CP(CP_HINTS)] = hints;
    front_midi(0xB0, 2, 0); blow(0); sim_tilt(0, 0);
    front_midi(0xC0, 0, 0); param_set(P_HAND, 0); sim_run(0.5f);
    sim_run(12.0f);
    check(!c->has_lknob && !c->has_hand && (int)pv[P_OCT] == 0 && huff_dbg(11) == 0.0f && huff_sleep() == 15, "back on the board with the joystick: no left knob, no hand, no mute, everything asleep");
}

void inst_tests(void)
{
    if (getenv("HUFF_CAL")) { calibrate(); return; }
    if (getenv("HUFF_SWEEP")) { sim_menu_close(); for (int i = 0; i < 70; i++) sim_turn(-1); sim_run(0.3f); param_set(P_REVERB, 0); param_set(P_ECHO, 0);
        if (getenv("HUFF_NOVIB")) { param_set(P_VIB, 0); param_set(P_BPITCH, 0); }
        float worst, rms; int silent; sweep(atoi(getenv("HUFF_SWEEP")), 0.12f, &worst, &silent, &rms, true); return; }
    if (getenv("HUFF_DBG")) {                                      // one note on one model, the level every 50 ms
        param_set(P_VIB, 0); param_set(P_BPITCH, 0); param_set(P_REVERB, 0); param_set(P_ECHO, 0); param_set(P_OVER, 100);
        param_set(P_MODEL, (float)atoi(getenv("HUFF_DBG"))); blow(0); sim_run(0.3f);
        if (getenv("HUFF_DRV")) param_set(P_DRIVE, (float)atoi(getenv("HUFF_DRV")));
        int mid = getenv("HUFF_N") ? atoi(getenv("HUFF_N")) : 72; int cc = getenv("HUFF_B") ? atoi(getenv("HUFF_B")) : 25;
        front_midi(0x90, (uint8_t)mid, 100); front_midi(0xB0, 2, (uint8_t)cc);
        for (int i = 0; i < 30; i++) { sim_run(0.05f); if (i % 10 == 9) { float w = midi_hz_((float)mid + (float)model_shift[atoi(getenv("HUFF_DBG"))]); float fd = m_pitch_at(sim_t - 0.3f, sim_t, 40, 4000); if (fd > 0.75f * w && fd < 1.33f * w) w = fd; printf("   pitch %.1f  harmonics at it: 0.5 %.5f 1 %.5f 1.5 %.5f 2 %.5f 3 %.5f 5 %.5f\n", (double)fd, (double)m_goertzel(sim_t - 0.3f, sim_t, 0.5f * w), (double)m_goertzel(sim_t - 0.3f, sim_t, w), (double)m_goertzel(sim_t - 0.3f, sim_t, 1.5f * w), (double)m_goertzel(sim_t - 0.3f, sim_t, 2 * w), (double)m_goertzel(sim_t - 0.3f, sim_t, 3 * w), (double)m_goertzel(sim_t - 0.3f, sim_t, 5 * w)); } printf("%5.2f s  rms %.4f  peak %.4f  breath %.2f  loop %.4f g %.5f press %.3f\n", (double)(0.05f * (float)(i + 1)), (double)m_rms(sim_t - 0.05f, sim_t), (double)m_peak(sim_t - 0.05f, sim_t), (double)huff_breath(), (double)huff_dbg(7), (double)huff_dbg(8), (double)huff_dbg(9)); }
        front_midi(0xB0, 2, 0); front_midi(0x80, (uint8_t)mid, 0);
        for (int i = 0; i < 20; i++) { sim_run(0.1f); printf("off %4.1f s  rms %.4f\n", (double)(0.1f * (float)(i + 1)), (double)m_rms(sim_t - 0.1f, sim_t)); }
        return;
    }
    if (getenv("HUFF_SOAK")) { sim_menu_close(); for (int i = 0; i < 70; i++) sim_turn(-1); sim_run(0.3f); soak((float)atof(getenv("HUFF_SOAK"))); return; }
    if (getenv("HUFF_QUICK")) {                                    // a few play screens, fast, for drawing work
        for (int i = 0; i < 70; i++) sim_turn(-1);
        int nq[16], Nq = ladder(nq);
        static const int ks[4] = { 0, 3, 6, 9 };
        for (int i = 0; i < 4; i++) { sim_tilt(0, lean_for(ks[i], Nq)); sim_run(0.3f); blow(i == 3 ? 0.18f : 0.1f); sim_run(0.8f); char lb[16]; snprintf(lb, sizeof lb, "quick_%d", i); sim_shot(lb); }
        blow(0); return;
    }
    sim_menu_close();
    // a known start: the first sound, rested, no reverb tails
    for (int i = 0; i < 70; i++) sim_turn(-1);
    sim_run(0.3f);
    param_set(P_REVERB, 0); param_set(P_ECHO, 0);
    blow(0); sim_tilt(0, 0); SI.jx = SI.jy = 0;
    sim_run(1.5f);
    check(m_rms(sim_t - 0.8f, sim_t) < 0.002f, "no breath: silent (rms %.5f)", (double)m_rms(sim_t - 0.8f, sim_t));
    sim_shot("waiting");
    {   extern float huff_breath(void);
        for (int i = 1; i <= 6; i++) { blow(0.03f * (float)i); sim_run(0.6f); extern float huff_dbg(int); note("breath level %.2f reads %.2f (env %.4f floor %.4f oenv %.4f)", (double)(0.03f * (float)i), (double)huff_breath(), (double)huff_dbg(0), (double)huff_dbg(1), (double)huff_dbg(2)); }
        blow(0); sim_run(1.0f); }

    // the speaker leaking into the mic must not keep it blowing
    blow(0.12f); sim_run(1.0f);
    float playing = m_rms(sim_t - 0.5f, sim_t);
    M.leak = 0.5f; sim_run(0.5f);
    blow(0); M.leak = 0.5f; sim_run(1.5f);
    float after = m_rms(sim_t - 0.5f, sim_t);
    check(playing > 0.02f && after < 0.003f, "the speaker leaking back into the mic (at half level) does not keep it sounding (%.3f -> %.5f)", (double)playing, (double)after);
    M.leak = 0;

    // a noisy room: music (a bass line, a chord, a kick every half second) and someone talking must not play it, and a
    // breath over them must. The breath is wind: its low band stands far over its middle; music and words have a middle.
    section("a noisy room");
    extern float huff_dbg(int);
    blow(0); sim_run(0.5f);
    // BREATH SETUP in the quiet first: it learns how far the breath's low band stands over its middle
    sim_menu_param(P_BSETUP); sim_tap(); sim_run(2.3f); blow(0.12f); sim_run(3.6f); blow(0); sim_run(0.5f);
    float bands_quiet = pv[P_BWIND];
    check(bands_quiet > 3.0f, "BREATH SETUP in a quiet room learns the breath's bands (low over middle, halved: %.1f)", (double)bands_quiet);
    M.music = 0.06f; sim_run(4.0f);
    float r_music = m_rms(sim_t - 2.5f, sim_t);
    note("music at the mic: low band %.4f, middle %.4f, floor %.4f, gate %s", (double)huff_dbg(0), (double)huff_dbg(5), (double)huff_dbg(1), huff_dbg(6) > 0 ? "open" : "shut");
    check(r_music < 0.01f, "music at the mic (bass, a chord, a kick on every beat), no breath: at most a whisper on the kicks (rms %.5f)", (double)r_music);
    blow(0.12f);
    for (int i = 0; i < 15; i++) { sim_run(0.1f); if (getenv("HUFF_TRACE")) note("over music %.1f s: breath %.2f low %.4f mid %.4f floor %.4f gate %s", (double)(0.1f * (float)(i + 1)), (double)huff_breath(), (double)huff_dbg(0), (double)huff_dbg(5), (double)huff_dbg(1), huff_dbg(6) > 0 ? "open" : "shut"); }
    float r_over = m_rms(sim_t - 0.8f, sim_t), rd_over = huff_breath();
    check(r_over > 0.02f && rd_over > 0.25f, "a breath over the music plays (rms %.3f, the breath reads %.2f: what stands over the room)", (double)r_over, (double)rd_over);
    blow(0); sim_run(1.5f);
    check(m_rms(sim_t - 0.8f, sim_t) < 0.01f, "the breath stops, the music goes on: quiet again (rms %.5f)", (double)m_rms(sim_t - 0.8f, sim_t));
    M.music = 0; sim_run(1.0f);
    M.speech = 0.08f; sim_run(4.0f);
    float r_talk = m_rms(sim_t - 2.5f, sim_t);
    note("talking at the mic: low band %.4f, middle %.4f, floor %.4f", (double)huff_dbg(0), (double)huff_dbg(5), (double)huff_dbg(1));
    check(r_talk < 0.003f, "someone talking at the mic: silent (rms %.5f)", (double)r_talk);
    blow(0.12f); sim_run(1.5f);
    check(m_rms(sim_t - 0.8f, sim_t) > 0.02f, "a breath over the talking plays (rms %.3f)", (double)m_rms(sim_t - 0.8f, sim_t));
    blow(0); sim_run(1.0f); M.speech = 0; sim_run(1.0f);
    // a bass line alone (no middle to tell it by, but a tone: its low band correlates half a period back, wind does not)
    M.music = 0.04f; M.mph[1] = M.mph[2] = M.mph[3] = 0; sim_run(3.0f);
    float r_bass = m_rms(sim_t - 2.0f, sim_t), tn = huff_dbg(10);
    blow(0.12f); sim_run(1.5f);
    float r_bass_b = m_rms(sim_t - 0.8f, sim_t), tnb = huff_dbg(10);
    check(r_bass < 0.01f && r_bass_b > 0.02f, "a bass line alone: at most a whisper (rms %.5f, its low band correlates %.2f half a period back); a breath over it plays (%.3f)", (double)r_bass, (double)tn, (double)r_bass_b);
    blow(0); M.music = 0; sim_run(1.0f);
    // the setup with the music on: the range is learned over the room (its low band taken off), the bands are kept
    M.music = 0.06f; sim_run(1.0f);
    sim_menu_param(P_BSETUP); sim_tap(); sim_run(2.3f); blow(0.12f); sim_run(3.6f);
    blow(0); sim_run(2.0f);
    float r_after = m_rms(sim_t - 1.0f, sim_t);
    blow(0.12f);
    for (int i = 0; i < 10; i++) { sim_run(0.1f); if (getenv("HUFF_TRACE")) note("after setup %.1f s: breath %.2f low %.4f mid %.4f floor %.4f gate %s", (double)(0.1f * (float)(i + 1)), (double)huff_breath(), (double)huff_dbg(0), (double)huff_dbg(5), (double)huff_dbg(1), huff_dbg(6) > 0 ? "open" : "shut"); }
    float rd_after = huff_breath();
    check(r_after < 0.01f && rd_after > 0.4f && rd_after < 0.85f && fabsf(pv[P_BWIND] / bands_quiet - 1.0f) < 0.25f, "BREATH SETUP done with the music on: the music stays quiet (rms %.5f), a normal breath reads %.0f %%, the bands hold (%.1f, range %.3f)", (double)r_after, (double)(rd_after * 100.0f), (double)pv[P_BWIND], (double)pv[P_BRANGE]);
    blow(0); M.music = 0; sim_run(1.0f);
    param_set(P_BRANGE, param(P_BRANGE)->def); param_set(P_BWIND, param(P_BWIND)->def); sim_run(0.5f);
    // the breath's latency: from the first air to the first sound
    {   blow(0.12f); float t_air = sim_t; int i0 = (int)(t_air * AUDIO_SR); sim_run(0.3f);
        int i = i0; while (i < sim_n && fabsf(sim_out[i]) < 0.01f) i++;
        float lat = (float)(i - i0) / (float)AUDIO_SR * 1000.0f;
        metric("breath_latency_ms", lat);
        check(lat < 70.0f, "a breath sounds within 70 ms (%.0f ms: the gate's hold, the RMS and the onset)", (double)lat);
        blow(0); sim_run(0.8f); }

    // every model, every note
    section("models in tune");
    float worst_all = 0;
    for (int m = 0; m < NM; m++) {
        float worst, rms; int silent;
        sweep(m, 0.12f, &worst, &silent, &rms, m == 1);
        char key[48]; snprintf(key, sizeof key, "cents_worst_%s", model_name[m]); metric(key, worst);
        check(silent == 0, "%s sounds on all %d notes (%d silent)", model_name[m], (int)pv[P_NOTES], silent);
        int tol = m == 1 ? 12 : (m >= 7 && m <= 9) ? 25 : 15;        // the brass bends with the breath, as brass does
        check(worst < (float)tol, "%s in tune within %d cents on every note (worst %.1f)", model_name[m], tol, (double)worst);
        check(rms > 0.03f && rms < 0.25f, "%s at a normal breath: rms %.3f (about -12 dBFS peaks)", model_name[m], (double)rms);
        if (worst > worst_all) worst_all = worst;
    }

    metric("cents_worst_all_models", worst_all);
    section("overblowing");
    int notes[16], N = ladder(notes);
    param_set(P_MODEL, 1); sim_tilt(0, lean_for(3, N)); blow(0.1f); sim_run(1.0f);
    float want = midi_hz_((float)notes[3]);
    blow(0.6f); sim_run(1.5f);
    float f = m_pitch_at(sim_t - 0.5f, sim_t, want * 1.5f, want * 2.6f);
    check(fabsf(1200.0f * log2f(f / (2.0f * want))) < 30.0f, "FLUTE blown hard jumps an octave (%.0f Hz on %.0f)", (double)f, (double)want);
    sim_shot("overblown");
    blow(0.1f); sim_run(1.0f);
    f = m_pitch_at(sim_t - 0.4f, sim_t, want * 0.75f, want * 1.33f);
    check(fabsf(1200.0f * log2f(f / want)) < 15.0f, "softer again: back down (%.0f Hz)", (double)f);
    param_set(P_MODEL, 4); blow(0); sim_run(0.3f); blow(0.1f); sim_run(1.0f);
    blow(0.6f); sim_run(1.2f);
    f = m_pitch_at(sim_t - 0.4f, sim_t, want * 2.4f, want * 3.6f);
    check(fabsf(1200.0f * log2f(f / (3.0f * want))) < 30.0f, "CLARINET blown hard jumps a twelfth (%.0f Hz on %.0f)", (double)f, (double)want);
    param_set(P_MODEL, 0); blow(0); sim_run(0.3f); blow(0.6f); sim_run(1.0f);
    f = m_pitch_at(sim_t - 0.4f, sim_t, want * 0.75f, want * 1.33f);
    check(fabsf(1200.0f * log2f(f / want)) < 30.0f, "OCARINA does not overblow (%.0f Hz on %.0f)", (double)f, (double)want);
    blow(0); sim_run(0.5f);

    // the trumpet blown hard jumps an octave; the lead too
    param_set(P_MODEL, 7); blow(0); sim_run(0.3f); blow(0.1f); sim_run(1.0f);
    f = m_pitch_at(sim_t - 0.4f, sim_t, want * 0.75f, want * 1.33f);
    check(fabsf(1200.0f * log2f(f / want)) < 20.0f, "TRUMPET at a normal breath: %.0f Hz on %.0f", (double)f, (double)want);
    blow(0.6f); sim_run(1.5f);
    f = m_pitch_at(sim_t - 0.5f, sim_t, want * 1.5f, want * 2.6f);
    check(fabsf(1200.0f * log2f(f / (2.0f * want))) < 30.0f, "TRUMPET blown hard: the lip tightens, an octave up (%.0f Hz)", (double)f);
    blow(0); sim_run(0.5f);

    section("deep and wide");
    // OCTAVE -3: the flute at C2 (65 Hz) and the guitar (an octave down on top of that: C1, 33 Hz) sound and are in tune
    param_set(P_OCT, -3); param_set(P_MODEL, 1); blow(0); sim_tilt(0, lean_for(3, N)); sim_run(0.3f); blow(0.12f); sim_run(1.5f);
    { int nl[24]; ladder(nl); float w = midi_hz_((float)nl[3]);
      f = m_pitch_at(sim_t - 0.6f, sim_t, w * 0.7f, w * 1.4f);
      check(f > 0 && fabsf(1200.0f * log2f(f / w)) < 25.0f && m_rms(sim_t - 0.5f, sim_t) > 0.02f, "FLUTE three octaves down plays %.1f Hz for %.1f (rms %.3f)", (double)f, (double)w, (double)m_rms(sim_t - 0.5f, sim_t));
      blow(0); param_set(P_MODEL, 10); sim_run(0.5f); blow(0.12f); sim_run(2.0f);
      w *= 0.5f;                                                      // the guitar is an octave down on top
      f = m_pitch_at(sim_t - 0.8f, sim_t, w * 0.7f, w * 1.4f);
      check(f > 0 && fabsf(1200.0f * log2f(f / w)) < 40.0f && m_rms(sim_t - 0.5f, sim_t) > 0.008f, "GUITAR three octaves down plays %.1f Hz for %.1f (rms %.3f)", (double)f, (double)w, (double)m_rms(sim_t - 0.5f, sim_t));
      blow(0); param_set(P_MODEL, 0); param_set(P_OCT, 0); sim_run(0.8f); }
    // NOTES 24 over TILT SPAN 180: the ends of the ladder at the tilt's ends
    param_set(P_NOTES, 24); param_set(P_SPAN, 180); sim_run(0.2f);
    { int nl[24]; int n24 = ladder(nl);
      sim_tilt(0, -88.0f); sim_run(0.5f); blow(0.12f); sim_run(1.0f);
      float fl = m_pitch_at(sim_t - 0.4f, sim_t, 50, 6000), wl = midi_hz_((float)nl[0]);
      blow(0); sim_tilt(0, 88.0f); sim_run(0.5f); blow(0.12f); sim_run(1.0f);
      float fh = m_pitch_at(sim_t - 0.4f, sim_t, 50, 6000), wh = midi_hz_((float)nl[n24 - 1]);
      sim_shot("ladder24");
      check(fabsf(1200.0f * log2f(fl / wl)) < 30.0f && fabsf(1200.0f * log2f(fh / wh)) < 30.0f, "24 notes over 180 degrees: the box face down plays the lowest (%.0f Hz), face up the highest (%.0f Hz)", (double)fl, (double)fh);
      blow(0); sim_tilt(0, 0); param_set(P_NOTES, 10); param_set(P_SPAN, 70); sim_run(0.5f); }
    // DRIVE: the amp adds harmonics; turned while a note sounds it does not click; at 0 it is a wire
    param_set(P_MODEL, 0); param_set(P_DRIVE, 0); sim_tilt(0, lean_for(3, N)); blow(0); sim_run(0.3f); blow(0.1f); sim_run(1.2f);
    { int nl[24]; ladder(nl); float w0 = midi_hz_((float)nl[3]), w = m_pitch_at(sim_t - 0.5f, sim_t, w0 * 0.75f, w0 * 1.33f);   // at the pitch as played
      float h1 = m_goertzel(sim_t - 0.5f, sim_t, w), h3 = m_goertzel(sim_t - 0.5f, sim_t, 3.0f * w), h5 = m_goertzel(sim_t - 0.5f, sim_t, 5.0f * w);
      float clean = (h3 + h5) / (h1 + 1e-9f);
      float tc = sim_t;
      for (int v = 0; v <= 100; v += 5) { param_set(P_DRIVE, (float)v); sim_run(0.05f); }
      float wc = worst_click_over(tc, sim_t);
      sim_run(0.8f);
      w = m_pitch_at(sim_t - 0.5f, sim_t, w0 * 0.75f, w0 * 1.33f);
      float d1 = m_goertzel(sim_t - 0.5f, sim_t, w), d3 = m_goertzel(sim_t - 0.5f, sim_t, 3.0f * w), d5 = m_goertzel(sim_t - 0.5f, sim_t, 5.0f * w);
      float dirty = (d3 + d5) / (d1 + 1e-9f);
      float r_drv = m_rms(sim_t - 0.5f, sim_t);
      check(dirty > 2.0f * clean, "DRIVE 100 on the ocarina: more odd harmonics than clean (3rd and 5th over the 1st: %.2f clean, %.2f driven)", (double)clean, (double)dirty);
      check(wc < 3.0f, "DRIVE turned from 0 to 100 while the note sounds: no click (worst spike %.2f)", (double)wc);
      check(r_drv > 0.03f && r_drv < 0.4f, "... and the level stays sane (rms %.3f)", (double)r_drv);
      sim_shot("driven"); sim_wav("-drive", tc - 0.5f, sim_t);
      param_set(P_DRIVE, 0); blow(0); sim_run(1.5f); }
    // the guitar: blown, a note holds and swells; let go, it rings out
    param_set(P_MODEL, 10); blow(0); sim_run(0.5f); blow(0.3f); sim_run(0.25f);
    { float e0 = m_rms(sim_t - 0.1f, sim_t); sim_run(1.5f); float e1 = m_rms(sim_t - 0.3f, sim_t);
      blow(0); sim_run(0.3f); float e2 = m_rms(sim_t - 0.1f, sim_t); sim_run(1.7f); float e3 = m_rms(sim_t - 0.3f, sim_t);
      check(e1 > 0.7f * e0 && e1 > 0.02f, "GUITAR held by the breath: after 1.7 s the note is still there (%.3f, was %.3f)", (double)e1, (double)e0);
      check(e2 > 0.003f && e3 < 0.3f * e2, "let go, the string rings on and fades (%.4f, then %.4f)", (double)e2, (double)e3); }
    param_set(P_MODEL, 0); sim_run(0.5f);

    section("expression");
    // the joystick bends by BEND RANGE
    param_set(P_MODEL, 0); sim_tilt(0, lean_for(3, N)); blow(0.12f); sim_run(0.8f);
    SI.jx = 1.0f; sim_run(0.8f);
    f = m_pitch_at(sim_t - 0.4f, sim_t, want, want * 1.5f);
    check(fabsf(1200.0f * log2f(f / want) - 100.0f * pv[P_BENDR]) < 15.0f, "stick right: up %d semitones (%.0f cents)", (int)pv[P_BENDR], (double)(1200.0f * log2f(f / want)));
    SI.jx = 0; sim_run(0.5f);
    // the tilt ladder holds its note through a wobble at a boundary
    float edge = (lean_for(3, N) + lean_for(4, N)) * 0.5f, step = pv[P_SPAN] / (float)N;
    sim_tilt(0, edge - 0.3f * step); sim_run(0.5f);
    float f0 = m_pitch_at(sim_t - 0.3f, sim_t, 100, 3000);
    int changes = 0; float last = f0;
    for (int i = 0; i < 20; i++) {
        sim_tilt(0, edge + (i & 1 ? 0.12f : -0.12f) * step); sim_run(0.15f);
        float fx = m_pitch_at(sim_t - 0.1f, sim_t, 100, 3000);
        if (fabsf(1200.0f * log2f(fx / last)) > 50.0f) changes++;
        last = fx;
    }
    check(changes <= 1, "a shaky hand at a boundary does not flip between two notes (%d changes)", changes);
    // MIDI notes in set the pitch while held; CC 2 in blows
    blow(0); sim_tilt(0, lean_for(3, N)); sim_run(0.5f);
    front_midi(0x90, 67, 100); front_midi(0xB0, 2, 90); sim_run(1.0f);
    f = m_pitch_at(sim_t - 0.4f, sim_t, 300, 1200);
    check(fabsf(1200.0f * log2f(f / midi_hz_(67.0f))) < 15.0f, "MIDI note 67 in and CC 2 (breath) in play G4 with no breath at the mic (%.1f Hz)", (double)f);
    front_midi(0x80, 67, 0); front_midi(0xB0, 2, 0); sim_run(2.6f);
    check(m_rms(sim_t - 0.3f, sim_t) < 0.003f, "let go: silent again");
    // harmony: a fifth above
    param_set(P_HARM, 2); param_set(P_HLVL, 80);
    blow(0.12f); sim_run(1.2f);
    float p1 = m_goertzel(sim_t - 0.5f, sim_t, want), p5 = m_goertzel(sim_t - 0.5f, sim_t, want * 1.4983f), pn = m_goertzel(sim_t - 0.5f, sim_t, want * 1.25f);
    check(p5 > 0.1f * p1 && p5 > 20.0f * pn, "HARMONY 5TH UP: the fifth sounds with the note (%.2g / %.2g, between %.2g)", (double)p5, (double)p1, (double)pn);
    sim_shot("harmony");
    param_set(P_HARM, 0);
    blow(0); sim_run(0.5f);

    // humming: the pitch of your voice, snapped to the scale
    section("humming");
    param_set(P_SOURCE, 2);
    M.hum_hz = 196.0f; blow(0.015f); sim_run(2.0f);          // G3 hummed, with a little breath
    f = m_pitch_at(sim_t - 0.5f, sim_t, 300, 1700);
    float mm = hz_to_midi(f);
    float pc = fmodf(mm + 1200.0f, 12.0f);
    check(f > 0 && fabsf(pc - 7.0f) < 0.3f, "humming a G3: it plays a G (%.1f Hz, MIDI %.2f)", (double)f, (double)mm);
    M.hum_hz = 207.65f;                                            // G#3: not in C major pentatonic
    sim_run(1.5f);
    f = m_pitch_at(sim_t - 0.5f, sim_t, 300, 1700);
    pc = fmodf(hz_to_midi(f) + 1200.0f, 12.0f);
    check(fabsf(pc - 7.0f) < 0.3f || fabsf(pc - 9.0f) < 0.3f, "humming a G#3, off the scale: it snaps to a scale note (pitch class %.2f)", (double)pc);
    M.hum_hz = 0; blow(0); param_set(P_SOURCE, 0); sim_run(1.0f);

    // breath setup: quiet, then a normal breath that should read about 60 %
    section("breath setup");
    param_set(P_BRANGE, 0.5f);
    sim_menu_param(P_BSETUP); sim_tap();
    check(front_menu_level() == 0, "BREATH SETUP closes the menu and takes the screen");
    blow(0); sim_run(1.2f); sim_shot("setup_quiet");
    sim_gif_begin("setup");
    sim_run(1.1f);
    blow(0.08f); sim_run(1.8f); sim_shot("setup_blow"); sim_run(1.8f);
    sim_gif_end();
    blow(0.08f); sim_run(1.0f);
    extern float huff_breath(void);
    float rd = huff_breath();
    check(pv[P_BRANGE] > 0.3f && pv[P_BRANGE] < 2.0f && rd > 0.4f && rd < 0.8f, "after setup a normal breath reads %.0f %% (range %.3f)", (double)(rd * 100.0f), (double)pv[P_BRANGE]);
    blow(0); sim_run(0.5f);

    // MIDI out
    section("MIDI out");
    midi_n = 0;                                                    // the log holds 8192: start it again
    float t0 = sim_t;
    for (int k = 0; k < 5; k++) { sim_tilt(0, lean_for(k + 2, N)); sim_run(0.3f); blow(0.12f); sim_run(0.4f); blow(0); sim_run(0.3f); }
    int offs, ons = midi_count(t0, 0x90, &offs);
    int ccs = 0; for (int i = 0; i < midi_n; i++) if (midi_log[i].t >= t0 && (midi_log[i].st & 0xF0) == 0xB0 && midi_log[i].d1 == 2) ccs++;
    check(ons == 5 && offs == 5, "five breaths on five notes: five notes out, each with its off (%d on, %d off)", ons, offs);
    check(ccs > 10, "the breath goes out as CC 2 (%d messages)", ccs);
    SI.jx = 1; sim_run(0.2f); SI.jx = 0; sim_run(0.2f);
    int pbs = midi_count(t0, 0xE0, NULL);
    check(pbs >= 2, "the stick's bend goes out as pitch bend (%d messages)", pbs);

    // the link: a pretend BAND leads the tempo and the harmony; HUFF follows, and its own changes go out
    section("linked to a BAND");
    blow(0); sim_tilt(0, 0); sim_run(0.5f);
    {   int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
        sim_peer_use(k);
        {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); link_set_harm(9, 1, 0, 0, 0, -1); }
        sim_peer_use(-1);
        sim_run(2.0f);
        check(!link_is_clock() && link_synced() && huff_peers() == 1, "HUFF takes the BAND as the clock and hears it");
        check((int)pv[P_TEMPO] == 120 && (int)pv[P_KEY] == 9 && (int)pv[P_SCALE] == 3, "the BAND's 120 BPM, its A minor: TEMPO %d, KEY %d, SCALE %d", (int)pv[P_TEMPO], (int)pv[P_KEY], (int)pv[P_SCALE]);
        sim_shot("linked");
        // HUFF's own scale that sits in the flock's (MIN PENTA in MINOR) stays when the flock only moves the key
        param_set(P_SCALE, 1); sim_run(0.3f);
        sim_peer_use(k); check(link_harm()->scale == 1 && link_harm()->key == 9, "MIN PENTA tells the flock MINOR (scale %d, key %d)", link_harm()->scale, link_harm()->key);
        link_set_harm(2, 1, 0, 0, 0, -1); sim_peer_use(-1);
        sim_run(1.0f);
        check((int)pv[P_KEY] == 2 && (int)pv[P_SCALE] == 1, "the BAND moves to D minor: HUFF's key follows, MIN PENTA stays (key %d, scale %d)", (int)pv[P_KEY], (int)pv[P_SCALE]);
        // a key set here goes out; a scale of the link's goes out as itself
        param_set(P_KEY, 4); param_set(P_SCALE, 5); sim_run(0.5f);
        sim_peer_use(k); int bk = link_harm()->key, bs = link_harm()->scale; sim_peer_use(-1);
        check(bk == 4 && bs == 3, "E MIXOLYDIAN set here reaches the BAND (key %d, scale %d)", bk, bs);
        // the echo on the beat: ECHO BEATS 1/4 at 120 BPM is 0.5 s; the BAND goes to 100 BPM and the repeats stretch to 0.6 s
        param_set(P_REVERB, 0); param_set(P_ECHO, 60); param_set(P_EFB, 0); param_set(P_ESYNC, 3); param_set(P_MODEL, 0);
        sim_tilt(0, lean_for(3, N)); sim_run(1.5f);
        float t_on = sim_t; blow(0.12f); sim_run(0.15f); blow(0); sim_run(1.5f);
        t_on = onset_after(t_on);                                      // from the sound's own start (the gate's hold varies)
        float e0 = m_rms(t_on + 0.5f, t_on + 0.65f), e1 = m_rms(t_on + 0.9f, t_on + 1.05f), e2 = m_rms(t_on + 0.3f, t_on + 0.43f);
        check(e0 > 3.0f * e1 && e0 > 3.0f * e2, "ECHO BEATS 1/4 at 120 BPM: a short note repeats half a second later (%.4f, around it %.4f %.4f)", (double)e0, (double)e2, (double)e1);
        sim_peer_use(k); { uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 0); } sim_peer_use(-1);
        sim_run(4.0f);
        check((int)pv[P_TEMPO] == 100, "the BAND slows to 100 BPM: TEMPO follows (%d)", (int)pv[P_TEMPO]);
        t_on = sim_t; blow(0.12f); sim_run(0.15f); blow(0); sim_run(1.5f);
        t_on = onset_after(t_on);
        e0 = m_rms(t_on + 0.6f, t_on + 0.75f); e1 = m_rms(t_on + 0.35f, t_on + 0.5f);
        check(e0 > 3.0f * e1, "... and the echo now repeats 0.6 s later (%.4f, before it %.4f)", (double)e0, (double)e1);
        param_set(P_ESYNC, 0); param_set(P_ECHO, 0); param_set(P_EFB, 35); param_set(P_REVERB, 30);
        sim_peer_power(k, false); sim_run(2.0f);
        check(huff_peers() == 0, "the BAND switched off: alone again");
        param_set(P_KEY, 0); param_set(P_SCALE, 0); param_set(P_TEMPO, 100); sim_run(0.3f);
    }

    // CPU on the host: front_audio per 64-frame block, with reverb and echo and harmony on, blown through CC 2 (the
    // room's floor would learn the test's steady noise and gate it); then everything asleep
    section("cost");
    param_set(P_REVERB, 60); param_set(P_ECHO, 40); param_set(P_HARM, 1);
    blow(0); front_midi(0xB0, 2, 90);
    for (int m = 0; m < NM; m += 1) {
        param_set(P_MODEL, (float)m); sim_run(0.5f);
        float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
        for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.05f * sim_noise();
        double us = 1e9;                                           // the best of 8 runs: other work on the machine only adds
        for (int k = 0; k < 8; k++) {
            struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
            for (int r = 0; r < 1000; r++) { front_midi(0xB0, 2, 90); front_audio(in, out, AUDIO_BLOCK); }
            clock_gettime(CLOCK_MONOTONIC, &b);
            double t = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / 1000.0;
            if (t < us) us = t;
        }
        char key[48]; snprintf(key, sizeof key, "host_us_per_block_%s", model_name[m]); metric(key, us);
        check(huff_sleep() == 0 && us < 20.0, "%s with harmony, reverb and echo: %.1f us per block on the host (< 20), all awake (sleep bits %d)", model_name[m], us, huff_sleep());
    }
    front_midi(0xB0, 2, 0); param_set(P_HARM, 0); param_set(P_ECHO, 0); param_set(P_REVERB, 30); param_set(P_MODEL, 0);
    blow(0); sim_run(12.0f);                                       // the reverb's tail goes, then everything sleeps
    {   float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
        for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.001f * sim_noise();
        double us = 1e9;
        for (int k = 0; k < 8; k++) {
            struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
            for (int r = 0; r < 1000; r++) front_audio(in, out, AUDIO_BLOCK);
            clock_gettime(CLOCK_MONOTONIC, &b);
            double t = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / 1000.0;
            if (t < us) us = t;
        }
        metric("host_us_per_block_idle", us);
        check(huff_sleep() == 15 && us < 2.5, "quiet: the voices, the reverb and the echo sleep (bits %d), %.2f us per block", huff_sleep(), us);
    }

    by_ear();

    // ---- the pictures
    section("pictures");
    front_midi(0xC0, 0, 0);                                         // OCARINA, the first sound, as it comes
    sim_run(0.5f);
    int notes2[16]; N = ladder(notes2);
    sim_gif_begin("play");
    static const int tune[] = { 3, 4, 5, 6, 5, 4, 3, 2, 3, 5, 7, 6, 5 };
    for (int i = 0; i < 13; i++) {
        sim_tilt(0, lean_for(tune[i], N));
        blow(0.11f); sim_run(i == 6 ? 0.6f : 0.32f);
        if (i == 6) { blow(0.6f); sim_run(0.5f); }
        if (i == 9) { SI.jx = 0.8f; sim_run(0.3f); SI.jx = 0; }
        if (i == 4) sim_shot("playing");
        blow(0.0f); sim_run(0.06f);
    }
    blow(0); sim_run(1.0f);
    sim_gif_end();
    sim_wav("-tune", sim_t - 7.5f, sim_t);
    // the guitar solo: a run with bends, sustained, in a gif
    front_midi(0xC0, 14, 0); sim_run(0.5f);
    { int nl[24]; int n = ladder(nl);
      sim_gif_begin("solo");
      static const int lick[] = { 2, 3, 4, 3, 2, 4, 5, 6, 5 };
      for (int i = 0; i < 9; i++) {
          sim_tilt(0, lean_for(lick[i], n)); blow(i == 8 ? 0.6f : 0.3f); sim_run(i == 8 ? 1.2f : 0.3f);
          if (i == 8) { sim_tilt(14.0f, lean_for(lick[i], n)); sim_run(0.6f); sim_shot("solo"); }
          if (i == 4) { blow(0); sim_run(0.1f); }
      }
      blow(0); sim_run(1.5f);
      sim_gif_end();
      sim_wav("-solo", sim_t - 6.0f, sim_t); }
    front_midi(0xC0, 0, 0); sim_run(0.5f);
    // a shot of each model
    for (int m = 0; m < NM; m++) {
        param_set(P_MODEL, (float)m); sim_tilt(0, lean_for(5, N)); blow(0.12f); sim_run(0.8f);
        char lb[32]; snprintf(lb, sizeof lb, "model_%d", m); if (m == 3 || m == 4 || m == 7 || m == 10 || m == 11 || m == 12) sim_shot(lb);
    }
    blow(0); param_set(P_MODEL, 0); sim_run(1.0f);
    // the screensaver, in a gif
    param_set(CP(CP_SAVER), 1); sim_run(61.0f);
    sim_gif_begin("saver"); sim_run(4.0f); sim_gif_end();
    sim_turn(1); sim_run(0.2f); sim_turn(-1);
    param_set(CP(CP_SAVER), param(CP(CP_SAVER))->def);

    rev_e();
}
