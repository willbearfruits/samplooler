// HUFF on the host: breath noise into the mic; every model held on every note of the ladder and its pitch
// measured; overblowing; silence without breath (and with the speaker leaking into the mic); the bend,
// MIDI in and out, the harmony voice, humming, BREATH SETUP, the tilt ladder's hysteresis; GIFs and screens.
#include "sim.h"
#include "looper.h"
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
};
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

const char *const inst_click_skip[] = { NULL };
void inst_excite(void) { blow(0.12f); M.leak = 0; M.hum_hz = 0; sim_tilt(0, 0); SI.jx = SI.jy = 0; }

// the ladder as instrument.c builds it
static int ladder(int *notes)
{
    static const struct { int n; int st[12]; } sc[10] = {
        { 5, { 0, 2, 4, 7, 9 } }, { 5, { 0, 3, 5, 7, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 11 } }, { 7, { 0, 2, 3, 5, 7, 8, 10 } },
        { 7, { 0, 2, 3, 5, 7, 9, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 10 } }, { 7, { 0, 1, 4, 5, 7, 8, 10 } }, { 6, { 0, 3, 5, 6, 7, 10 } },
        { 5, { 0, 1, 5, 7, 10 } }, { 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } } };
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

void inst_tests(void)
{
    if (getenv("HUFF_CAL")) { calibrate(); return; }
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

    // CPU on the host: front_audio per 64-frame block, with reverb and echo and harmony on
    section("cost");
    param_set(P_REVERB, 60); param_set(P_ECHO, 40); param_set(P_HARM, 1);
    for (int m = 0; m < 7; m += 1) {
        param_set(P_MODEL, (float)m); blow(0.12f); sim_run(0.3f);
        float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
        for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.05f * sim_noise();
        double us = 1e9;                                           // the best of 8 runs: other work on the machine only adds
        for (int k = 0; k < 8; k++) {
            struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
            for (int r = 0; r < 1000; r++) front_audio(in, out, AUDIO_BLOCK);
            clock_gettime(CLOCK_MONOTONIC, &b);
            double t = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / 1000.0;
            if (t < us) us = t;
        }
        char key[48]; snprintf(key, sizeof key, "host_us_per_block_%s", model_name[m]); metric(key, us);
        check(us < 20.0, "%s with harmony, reverb and echo: %.1f us per block on the host (< 20)", model_name[m], us);
    }
    param_set(P_HARM, 0); param_set(P_ECHO, 0); param_set(P_REVERB, 30); param_set(P_MODEL, 0);
    blow(0); sim_run(2.0f);

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
