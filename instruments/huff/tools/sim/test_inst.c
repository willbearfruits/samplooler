// HUFF on the host: breath noise into the mic; every model held on every note of the ladder and its pitch
// measured; overblowing; silence without breath (and with the speaker leaking into the mic); the bend,
// MIDI in and out, the harmony voice, humming, BREATH SETUP, the tilt ladder's hysteresis; GIFs and screens.
#include "sim.h"
#include "looper.h"
#include "link.h"
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
};
int huff_peers(void);
int huff_sleep(void);
float huff_breath(void);
float huff_dbg(int k);
static const char *const model_name[7] = { "OCARINA", "FLUTE", "PAN PIPE", "SHAKUHACHI", "CLARINET", "WHISTLE", "BOTTLE" };
static const int model_shift[7] = { 0, 0, 0, 0, 0, 12, -12 };

// ---- the mic: breath is low rumble; plus a little room; plus, if asked, the speaker leaking back
typedef struct { float level, lp, leak, hum_hz, hum_ph; } mic_t;
static mic_t M;
static float mic(float t, void *u)
{
    mic_t *b = u;
    b->lp += 0.03f * (sim_noise() - b->lp);                      // wind on a MEMS mic: rumble, falling off above ~150 Hz
    float x = 0.0015f * sim_noise() + b->level * b->lp * 6.0f;
    if (b->leak > 0) { int i = (int)(t * AUDIO_SR) - 200; if (i >= 0 && i < sim_n) x += b->leak * sim_out[i]; }
    if (b->hum_hz > 0) {                                          // a hummed note: a soft sawtooth
        b->hum_ph += b->hum_hz / AUDIO_SR; if (b->hum_ph > 1) b->hum_ph -= 1;
        x += 0.08f * (2.0f * b->hum_ph - 1.0f);
    }
    return x;
}
static void blow(float level) { M.level = level; SI.mic = mic; SI.mic_u = &M; }

const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", NULL };   // the radio's switches, not the sound
void inst_excite(void) { blow(0.12f); M.leak = 0; M.hum_hz = 0; sim_tilt(0, 0); SI.jx = SI.jy = 0; }

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
        float f = m_pitch_at(sim_t - 0.4f, sim_t, want * 0.75f, want * 1.33f);
        float cents = f > 0 ? 1200.0f * log2f(f / want) : 9999.0f;
        float r = m_rms(sim_t - 0.4f, sim_t);
        *rms_avg += r / (float)N;
        if (show) note("%-10s %3d  want %7.1f Hz  got %7.1f Hz  %+6.1f cents  rms %.3f", model_name[model], notes[k] + model_shift[model], (double)want, (double)f, (double)cents, (double)r);
        if (f <= 0) (*silent)++;
        else if (fabsf(cents) > *worst) *worst = fabsf(cents);
    }
    blow(0); sim_run(0.3f);
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
    int m0 = getenv("HUFF_M") ? atoi(getenv("HUFF_M")) : 1, m1 = getenv("HUFF_M") ? m0 + 1 : 7;
    for (int m = m0; m < m1; m++) {
        param_set(P_MODEL, (float)m);
        printf("model %s\n", model_name[m]);
        for (int st = 0; st <= 32; st += 4) {
            float mid = 60.0f + (float)st;
            printf("  midi %3.0f:", (double)(mid + (float)model_shift[m]));
            for (int L = 0; L < 4; L++) {
                front_midi(0x90, (uint8_t)mid, 100);
                front_midi(0xB0, 2, 0); sim_run(0.2f);
                front_midi(0xB0, 2, (uint8_t)(bs[L] * 127.0f)); sim_run(0.8f);
                float want = midi_hz_(mid + (float)model_shift[m]);
                float f = m_pitch_at(sim_t - 0.4f, sim_t, want * 0.75f, want * 1.33f);
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
    for (int m = 0; m < 7; m++) {                                  // a run up and down the ladder on each model, blowing through
        param_set(P_MODEL, (float)m); param_set(P_REVERB, 0); param_set(P_ECHO, 0); param_set(P_VIB, 0);
        blow(0); sim_tilt(0, lean_for(2, N)); sim_run(0.5f);
        blow(0.12f); sim_run(0.5f);
        float t0 = sim_t, at[10];
        static const int walk[] = { 3, 4, 5, 6, 7, 6, 5, 4, 3, 2 };
        for (int i = 0; i < 10; i++) { sim_tilt(0, lean_for(walk[i], N)); sim_tick(); at[i] = sim_t; sim_run(0.25f); }
        float wc = worst_click(at, 10);
        char key[48]; snprintf(key, sizeof key, "note_change_click_%s", model_name[m]); metric(key, wc);
        check(wc < 3.0f, "%s: a run up and down the ladder, no click at a note change (worst spike %.2f of the signal's own)", model_name[m], (double)wc);
        if (m == 0 || m == 1 || m == 4) { char sfx[32]; snprintf(sfx, sizeof sfx, "-run-%d", m); sim_wav(sfx, t0 - 0.3f, sim_t); }
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

void inst_tests(void)
{
    if (getenv("HUFF_CAL")) { calibrate(); return; }
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

    // every model, every note
    section("models in tune");
    float worst_all = 0;
    for (int m = 0; m < 7; m++) {
        float worst, rms; int silent;
        sweep(m, 0.12f, &worst, &silent, &rms, m == 1);
        char key[48]; snprintf(key, sizeof key, "cents_worst_%s", model_name[m]); metric(key, worst);
        check(silent == 0, "%s sounds on all %d notes (%d silent)", model_name[m], (int)pv[P_NOTES], silent);
        check(worst < (m == 1 ? 12.0f : 15.0f), "%s in tune within %d cents on every note (worst %.1f)", model_name[m], m == 1 ? 12 : 15, (double)worst);
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
    M.hum_hz = 196.0f; blow(0.06f); sim_run(2.0f);           // G3 hummed, with some breath
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
    check(pv[P_BRANGE] < 0.3f && rd > 0.4f && rd < 0.8f, "after setup a normal breath reads %.0f %% (range %.3f)", (double)(rd * 100.0f), (double)pv[P_BRANGE]);
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
        float t_on = sim_t; blow(0.12f); sim_run(0.12f); blow(0); sim_run(1.5f);
        float e0 = m_rms(t_on + 0.52f, t_on + 0.65f), e1 = m_rms(t_on + 0.9f, t_on + 1.05f), e2 = m_rms(t_on + 0.3f, t_on + 0.45f);
        check(e0 > 3.0f * e1 && e0 > 3.0f * e2, "ECHO BEATS 1/4 at 120 BPM: a short note repeats half a second later (%.4f, around it %.4f %.4f)", (double)e0, (double)e2, (double)e1);
        sim_peer_use(k); { uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 0); } sim_peer_use(-1);
        sim_run(4.0f);
        check((int)pv[P_TEMPO] == 100, "the BAND slows to 100 BPM: TEMPO follows (%d)", (int)pv[P_TEMPO]);
        t_on = sim_t; blow(0.12f); sim_run(0.12f); blow(0); sim_run(1.5f);
        e0 = m_rms(t_on + 0.62f, t_on + 0.72f); e1 = m_rms(t_on + 0.35f, t_on + 0.5f);
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
    for (int m = 0; m < 7; m += 1) {
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
    // a shot of each model
    for (int m = 0; m < 7; m++) {
        param_set(P_MODEL, (float)m); sim_tilt(0, lean_for(5, N)); blow(0.12f); sim_run(0.8f);
        char lb[32]; snprintf(lb, sizeof lb, "model_%d", m); if (m == 3 || m == 4) sim_shot(lb);
    }
    blow(0); param_set(P_MODEL, 0); sim_run(1.0f);
    // the screensaver, in a gif
    param_set(CP(CP_SAVER), 1); sim_run(61.0f);
    sim_gif_begin("saver"); sim_run(4.0f); sim_gif_end();
    sim_turn(1); sim_run(0.2f); sim_turn(-1);
    param_set(CP(CP_SAVER), param(CP(CP_SAVER))->def);
}
