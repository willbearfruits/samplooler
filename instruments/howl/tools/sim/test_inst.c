// HOWL on the host. The mic hears a quiet room (and, for AIR, the box's own speaker through a little
// simulated air: the output 8 ms later plus two reflections). Checks: quiet when flat, a howl on the chord's
// notes when tipped, leaning and the joystick walk the howl through the chord, a new chord moves it, AIR
// howls through the room, the level stays under HOWL LEVEL whatever is done to it, FREEZE holds, the tide
// breathes, SHIFT makes it climb, EVOLVE wanders, MIDI keys retune it, MIDI and the LED follow, CPU.
#include "sim.h"
#include "instrument.c"
#include "looper.h"
#include <time.h>

void sim_menu_close(void);
bool sim_menu_param(int p);

// Every param of HOWL ramps (glide, governor, crossfades), so none is left out of the click check.
const char *const inst_click_skip[] = { NULL };

typedef struct { float gain, noise; float ah_t0, ah_t1; } air_t;
static air_t air = { 0, 0.002f, -1, -1 };
static float mic(float t, void *u)
{
    air_t *a = u;
    float x = a->noise * sim_noise();
    if (t > a->ah_t0 && t < a->ah_t1) x += 0.06f * (sinf(TWO_PI * 200.0f * t) + 0.5f * sinf(TWO_PI * 400.0f * t));   // someone says "ah"
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

static void load(int sound) { front_midi(0xC0, (uint8_t)sound, 0); sim_run(0.05f); }
static void room_only(void) { air.gain = 0; air.noise = 0.002f; air.ah_t0 = air.ah_t1 = -1; SI.mic = mic; SI.mic_u = &air; }

void inst_excite(void)
{
    room_only();
    load(0);
    param_set(P_SWELL, 0.5f);
    SI.joy = false;
    sim_tilt(0, 22);
}

// The power within 1.5 % of a frequency (a howl drifts, and the room loop pulls it onto its comb).
static float band_tol = 0.015f;
static float band_power(float t0, float t1, float f)
{
    float p = 0;
    int K = (int)(band_tol / 0.0025f + 0.5f);
    for (int k = -K; k <= K; k++) p += m_goertzel(t0, t1, f * (1.0f + 0.0025f * (float)k));
    return p;
}
// power on the bands' notes against a whole tone above each
static float chord_ratio(float t0, float t1)
{
    float on = 0, off = 0;
    for (int b = 0; b < nb_now; b++) { float f = midi_hz(notes[b]); on += band_power(t0, t1, f); off += band_power(t0, t1, f * 1.1225f); }
    return on / (off + 1e-12f);
}
static int dominant(float t0, float t1, float *share)
{
    float best = 0, tot = 0; int bi = 0;
    for (int b = 0; b < nb_now; b++) { float p = band_power(t0, t1, midi_hz(notes[b])); tot += p; if (p > best) { best = p; bi = b; } }
    if (share) *share = best / (tot + 1e-12f);
    return bi;
}
// How far the spectrum moves (Hz) between [t, t + 0.5] and [t + dt, t + dt + 0.5]: the lag that best lines up
// their power spectra (0.5 Hz steps over 500..2000 Hz).
static float spectral_drift(float t, float dt)
{
    enum { NF = 3000 };
    static float a[NF], b[NF];
    for (int k = 0; k < NF; k++) { float f = 500.0f + 0.5f * (float)k; a[k] = m_goertzel(t, t + 0.5f, f); b[k] = m_goertzel(t + dt, t + dt + 0.5f, f); }
    float best = -1; int bl = 0;
    for (int l = -120; l <= 120; l++) {
        double c = 0;
        for (int k = 200; k < NF - 200; k++) c += (double)a[k] * b[k + l];
        if (c > best) { best = (float)c; bl = l; }
    }
    return 0.5f * (float)bl;
}
static float max_rms(float t0, float t1, float win)
{
    float m = 0;
    for (float t = t0; t + win <= t1; t += win * 0.5f) { float r = m_rms(t, t + win); if (r > m) m = r; }
    return m;
}

static void tip_and_lean(void)
{
    room_only(); SI.joy = false;
    load(0);                                             // FIRST HOWL
    param_set(P_SWELL, 1.5f);
    sim_tilt(0, 0); sim_run(2.0f);
    float t0 = sim_t;
    sim_shot("quiet");
    float q = m_rms(t0 - 1.2f, t0);
    check(q < 0.02f, "flat on the table: nearly quiet (rms %.4f)", (double)q);
    sim_gif_begin("howl");
    for (int i = 0; i < 300; i++) { sim_tilt(0, 35.0f * (float)i / 300.0f); sim_tick(); }   // tipped over 3 s
    sim_run(2.0f);
    sim_shot("howling");
    float h = m_rms(sim_t - 1.0f, sim_t);
    check(h > 0.08f, "tipped 35 degrees: it howls (rms %.3f)", (double)h);
    float r = chord_ratio(sim_t - 1.0f, sim_t);
    check(r > 20.0f, "the howl sits on the chord's notes (%.0fx the power a tone up)", (double)r);
    float lim = exp2f(pv[P_LEVEL] * 0.16609640f);
    float mr = max_rms(sim_t - 1.5f, sim_t, 0.1f);
    check(mr <= lim, "held at HOWL LEVEL: 100 ms rms %.3f, the level's peak %.3f (%.0f dB)", (double)mr, (double)lim, (double)pv[P_LEVEL]);
    check(ratio_s > 0.6f && head_a > 0.4f, "the screen knows: howl %.2f of the level, the wolf's head %.2f up", (double)ratio_s, (double)head_a);

    // near the edge, leaning picks the note
    sim_tilt(0, 14); sim_run(2.5f);
    int win[3]; float leans[3] = { -35, 0, 35 }, sh;
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 50; i++) { sim_tilt(leans[k] * (float)(i + 1) / 50.0f + (k ? leans[k - 1] * (1.0f - (float)(i + 1) / 50.0f) : 0), 14); sim_tick(); }
        sim_run(2.5f);
        win[k] = dominant(sim_t - 0.8f, sim_t, &sh);
        note("tilt 14, lean %+.0f: loudest band %d (%.0f Hz, %.0f%%), rms %.3f", (double)leans[k], win[k], (double)midi_hz(notes[win[k]]), (double)(sh * 100), (double)m_rms(sim_t - 0.8f, sim_t));
        if (k == 0) sim_shot("lean_left");
    }
    sim_gif_end();
    check(win[0] <= 1 && win[2] >= nb_now - 2 && win[1] >= 2 && win[1] <= 3, "leaning walks the howl through the chord, low to high (bands %d %d %d of %d)", win[0], win[1], win[2], nb_now);

    // the joystick does the same, and swells
    SI.joy = true; sim_tilt(0, 8);
    SI.jx = -0.95f; SI.jy = 0.6f; sim_run(3.0f);
    int jl = dominant(sim_t - 0.8f, sim_t, NULL);
    SI.jx = 0.95f; sim_run(3.0f);
    int jr = dominant(sim_t - 0.8f, sim_t, NULL);
    check(jl <= 1 && jr >= nb_now - 2, "the joystick's x picks the note too (bands %d then %d), its y swells it (rms %.3f)", jl, jr, (double)m_rms(sim_t - 0.8f, sim_t));
    SI.jx = 0; SI.jy = 0; SI.joy = false;
}

static void chords_and_sounds(void)
{
    room_only();
    load(1);                                             // MOON CHOIR: A minor, wide, drifting
    sim_tilt(0, 30); sim_run(4.0f);
    float r = chord_ratio(sim_t - 1.0f, sim_t);
    check(r > 10.0f, "MOON CHOIR (%s): the howl moves to its notes (%.0fx)", chord_name, (double)r);
    sim_shot("moon_choir");
    // every factory sound howls when tipped, and stays under its level
    int howled = 0, under = 0;
    for (int s = 0; s < N_PRESETS; s++) {
        load(s);
        bool air_mode = (int)pv[P_MODE] != 0;
        air.gain = air_mode ? 1.6f : 0.0f;
        sim_tilt(0, 0); sim_run(0.8f);
        sim_tilt(0, 32); sim_run(4.0f);
        float mx = max_rms(sim_t - 4.0f, sim_t, 0.1f), lim = exp2f(pv[P_LEVEL] * 0.16609640f);
        metric(presets[s].name, mx);
        if (mx > 0.05f) howled++; else note("%s: no more than %.3f rms within 4 s of tipping", presets[s].name, (double)mx);
        if (mx <= lim * 1.05f) under++; else note("%s: 100 ms rms %.3f over its level %.3f", presets[s].name, (double)mx, (double)lim);
    }
    check(howled == N_PRESETS, "every one of the %d sounds howls within 4 s of tipping (%d did)", N_PRESETS, howled);
    check(under == N_PRESETS, "and every one stays under its HOWL LEVEL (%d did)", under);
    air.gain = 0;
}

static void air_mode(void)
{
    room_only();
    load(12);                                            // THE ROOM: AIR
    check((int)pv[P_MODE] == 1, "THE ROOM is AIR: no loop inside the box");
    air.gain = 0; sim_tilt(0, 35); sim_run(2.0f);
    float none = m_rms(sim_t - 1.0f, sim_t);
    check(none < 0.02f, "AIR without a speaker in the room: no howl, even tipped (rms %.4f)", (double)none);
    air.gain = 1.8f; sim_run(4.0f);
    // the room's delay pulls a howl onto the nearest of its comb's teeth (every 1 / 8 ms = 125 Hz here):
    // in AIR a howl sits within a few percent of a chord note, as feedback does in a real room
    float h = m_rms(sim_t - 1.0f, sim_t), f = m_pitch_at(sim_t - 0.5f, sim_t, 100, 4000), off = 1;
    for (int b = 0; b < nb_now; b++) { float d = fabsf(f / midi_hz(notes[b]) - 1.0f); if (d < off) off = d; }
    check(h > 0.05f && off < 0.045f, "with the speaker heard 8 ms later: it howls through the air at %.0f Hz, %.1f%% from a chord note (rms %.3f)", (double)f, (double)(off * 100.0f), (double)h);
    sim_shot("air");
    air.gain = 0;
}

// Anything a player (or a cat) could do, as fast as it can be done, with a loud speaker in the loop.
static void abuse(void)
{
    room_only();
    load(0);
    air.gain = 4.0f;                                     // a speaker much louder than it should be
    uint32_t s = 0xfeedu;
    float t0 = sim_t, worst = 0;
    for (int k = 0; k < 120; k++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int what = (int)(s % 9u);
        float u = (float)((s >> 8) & 1023) / 1023.0f;
        switch (what) {
        case 0: sim_tilt(-40.0f + 80.0f * u, -30.0f + 80.0f * u); break;
        case 1: param_set(P_GAIN, 100.0f * u); break;
        case 2: param_set(P_RING, 6.0f + 144.0f * u); break;
        case 3: param_set(P_MODE, (float)(s % 3u)); break;
        case 4: param_set(P_ROOM, 100.0f * u); param_set(P_SHIFT, -20.0f + 40.0f * u); break;
        case 5: param_set(P_COLOUR, (float)(s % 4u)); param_set(P_FOLD, 100.0f * u); break;
        case 6: param_set(P_FREEZE, (float)(s & 1u)); break;
        case 7: SI.joy = true; SI.jx = 2.0f * u - 1.0f; SI.jy = 1.0f; break;
        case 8: load((int)(s % (unsigned)N_PRESETS)); param_set(P_LEVEL, -6); param_set(P_MICIN, 200); param_set(P_SWELL, 0.2f); air.gain = 4.0f; break;
        }
        param_set(P_LEVEL, -6);
        sim_run(0.2f);
    }
    worst = max_rms(t0, sim_t, 0.05f);
    float pk = m_peak(t0, sim_t);
    // the promise: what HOWL plays never passes 1.25x HOWL LEVEL (here -6 dB, 0.5: so 0.625)
    check(worst <= 0.625f && pk <= 0.63f, "25 s of abuse with a speaker 4x too loud: the loudest 50 ms is %.3f rms, the peak %.3f, never past 1.25x HOWL LEVEL (0.625)", (double)worst, (double)pk);
    // nothing jumps: the 20 ms rms never rises by more than 18 dB from one 20 ms to the next
    float jump = 0;
    for (float t = t0 + 0.02f; t + 0.02f <= sim_t; t += 0.02f) {
        float a = m_rms(t - 0.02f, t), b = m_rms(t, t + 0.02f);
        if (a > 0.003f && b / a > jump) jump = b / a;
    }
    check(jump < 8.0f, "nothing jumps out: the biggest rise between two 20 ms windows is %.1fx (%.0f dB)", (double)jump, (double)(20.0f * log10f(jump + 1e-9f)));
    air.gain = 0; SI.joy = false; SI.jx = SI.jy = 0;
    param_set(P_FREEZE, 0);
}

static void freeze_and_tide(void)
{
    room_only(); load(0);
    sim_tilt(0, 30); sim_run(3.0f);
    float before = m_rms(sim_t - 0.5f, sim_t);
    param_set(P_FREEZE, 1);
    SI.mic = NULL; sim_tilt(0, 0);
    sim_run(4.0f);
    float held = m_rms(sim_t - 0.5f, sim_t);
    sim_shot("frozen");
    check(held > 0.5f * before, "FREEZE: mic gone, box flat, the howl still rings 4 s later (rms %.3f, was %.3f)", (double)held, (double)before);
    param_set(P_FREEZE, 0); SI.mic = mic;
    sim_run(4.0f);
    float after = m_rms(sim_t - 0.5f, sim_t);
    check(after < 0.3f * before, "FREEZE off, still flat: it dies away (rms %.4f)", (double)after);

    load(0); param_set(P_TIDE, 90); param_set(P_TIDERATE, 0.5f);
    sim_tilt(0, 24); sim_run(3.0f);
    float lo = 1, hi = 0;
    float t0 = sim_t; sim_run(6.0f);
    for (float t = t0; t + 0.1f <= sim_t; t += 0.1f) { float r = m_rms(t, t + 0.1f); if (r < lo) lo = r; if (r > hi) hi = r; }
    check(hi > 2.5f * lo, "TIDE 90%% at 0.5 Hz: the howl breathes (100 ms rms from %.3f to %.3f)", (double)lo, (double)hi);
    param_set(P_TIDE, 0);
}

static void shift_and_evolve(void)
{
    room_only();
    load(3);                                             // SHEPARD
    sim_tilt(0, 30); sim_run(3.0f);
    sim_gif_begin("shepard");
    float t0 = sim_t; sim_run(5.0f);
    sim_gif_end();
    sim_wav("-shepard", t0, sim_t);
    // A shifted loop is a comb of partials SHIFT Hz apart, all climbing SHIFT Hz per trip round the room:
    // the spectrum a quarter second later is the same spectrum moved up by SHIFT x 0.25 s / SIZE.
    float lag = spectral_drift(t0 + 1.0f, 0.25f), want = pv[P_SHIFT] * 0.25f / pv[P_SIZE];
    check(fabsf(lag - want) < 0.3f * want, "SHEPARD (SHIFT +%.0f Hz, a %.0f ms room): the whole howl climbs %.1f Hz in 0.25 s (%.0f expected)", (double)pv[P_SHIFT], (double)(pv[P_SIZE] * 1000.0f), (double)lag, (double)want);
    load(4);                                             // FALLING
    sim_tilt(0, 30); sim_run(3.0f);
    lag = spectral_drift(sim_t - 1.5f, 0.25f); want = pv[P_SHIFT] * 0.25f / pv[P_SIZE];
    check(fabsf(lag - want) < 0.3f * fabsf(want), "FALLING (SHIFT %.0f Hz): it falls %.1f Hz in 0.25 s (%.0f expected)", (double)pv[P_SHIFT], (double)lag, (double)want);
    sim_shot("shepard");

    load(10);                                            // WANDERER: EVOLVE FAST
    sim_tilt(0, 28);
    int n0 = evo_n; char first[24]; snprintf(first, sizeof first, "%s", chord_name);
    sim_run(16.0f);
    check(evo_n - n0 >= 2, "EVOLVE FAST: the chord wandered %d times in 16 s (%s ... %s)", evo_n - n0, first, chord_name);
    sim_shot("wanderer");
}

static void keys_and_midi(void)
{
    room_only(); load(14);                               // DRONE KEYS
    sim_tilt(0, 28);
    front_midi(0x90, 57, 100); front_midi(0x90, 64, 100);   // A3 and E4
    sim_run(4.0f);
    bool all = true;
    for (int b = 0; b < nb_now; b++) { int pc = ((int)lroundf(notes[b])) % 12; if (pc != 9 && pc != 4) all = false; }
    int d = dominant(sim_t - 1.0f, sim_t, NULL), pc = ((int)lroundf(notes[d])) % 12;
    check(all && (pc == 9 || pc == 4) && m_rms(sim_t - 1.0f, sim_t) > 0.05f, "MIDI keys A3 + E4 held: every band is an A or an E and it howls on %s", chord_name);
    sim_shot("keys");
    front_midi(0x80, 57, 0); front_midi(0x80, 64, 0);
    sim_run(2.0f);
    check(strcmp(chord_name, "KEYS") != 0, "keys let go (HOLD): back to the sound's chord (%s)", chord_name);
    int ons = 0, ccs = 0;
    for (int i = 0; i < midi_n; i++) { if ((midi_log[i].st & 0xF0) == 0x90 && midi_log[i].d2) ons++; if ((midi_log[i].st & 0xF0) == 0xB0 && midi_log[i].d1 == 11) ccs++; }
    check(ons > 3 && ccs > 10, "MIDI out: %d notes for the howling band, %d level CCs (CC 11)", ons, ccs);
    uint8_t lit[3] = { sim_led[0], sim_led[1], sim_led[2] };
    sim_tilt(0, 0); sim_run(3.0f);
    check(lit[2] > sim_led[2] + 10 && lit[0] > sim_led[0] + 10, "the LED glows while it howls (%d %d %d at LED LOW) and dims when it stops (%d %d %d)", lit[0], lit[1], lit[2], sim_led[0], sim_led[1], sim_led[2]);
}

static void loop_layers(void)
{
    room_only(); load(0);
    sim_tilt(0, 30); sim_run(2.0f);
    sim_rec(2.0f); sim_run(0.3f);
    check(looper_state() == LOOP_PLAYING, "REC held 2 s catches the howl in a loop");
    sim_tilt(0, 0); sim_run(3.0f);
    check(m_rms(sim_t - 1.0f, sim_t) > 0.03f, "flat again: the howl is gone but the loop plays it on (rms %.3f)", (double)m_rms(sim_t - 1.0f, sim_t));
    sim_shot("loop");
    sim_menu_param(CP(CP_LOOP_CLEAR)); sim_tap(); sim_menu_close();
}

static void cpu(void)
{
    room_only(); load(5);                                // CATHEDRAL: 8 bands and the room
    sim_tilt(0, 30); sim_run(2.0f);
    param_set(P_SHIFT, 3.0f); param_set(P_FOLD, 50);   // and the shifter and the glow on top: the worst case
    float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
    for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.002f * sim_noise();
    struct timespec a, b;
    const int N = 20000;
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int k = 0; k < N; k++) front_audio(in, out, AUDIO_BLOCK);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double us = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / N;
    metric("host_us_per_block", us);
    check(us < 20.0, "worst case (8 bands, room, shifter, glow, looper, master): %.1f us per 64-frame block on the host", us);
    param_set(P_SHIFT, 0); param_set(P_FOLD, 0);
}

void inst_tests(void)
{
    sim_menu_close();
    tip_and_lean();
    chords_and_sounds();
    air_mode();
    freeze_and_tide();
    shift_and_evolve();
    keys_and_midi();
    loop_layers();
    abuse();
    cpu();
    load(0); room_only(); sim_tilt(0, 26); sim_run(2.0f);
    sim_gif_begin("play");
    for (int i = 0; i < 600; i++) { sim_tilt(-30.0f * sinf((float)i / 600.0f * 6.2832f), 20.0f + 10.0f * sinf((float)i / 120.0f)); sim_tick(); }
    sim_gif_end();
    sim_tilt(0, 0);
}
