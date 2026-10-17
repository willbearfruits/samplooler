// KNOCK on the host: knocks into the mic, each with the push it gives the box in the 1 kHz accelerometer
// stream, from each side in turn; by tone; a loop recorded with REC and checked for timing, the grid, adding,
// stop / play, undo; the stick's roll and stutter; the BODY's notes per side and its zero-latency live mode;
// rubs; velocity; the box's own sound coming back into the mic; MIDI in; the engine's time per block.
#include "sim.h"
#include "inst_engine.h"
#include "oled.h"
#include <math.h>
#include <string.h>
#include <time.h>

const char *const inst_click_skip[] = { NULL };

// ---------------------------------------------------------------- the knocks
enum { K_DULL, K_SHARP, K_RUB };
typedef struct { float t; vec3_t push; int kind; float amp, dur; } knock_t;
static knock_t script[512];
static int nscript;
static float lp_state, hp_prev, bleed, rub_lp;
static float excite_period;                     // inst_excite: a knock this often, by itself

static void knock(float t, vec3_t push, int kind, float amp) { if (nscript < 512) script[nscript++] = (knock_t){ t, push, kind, amp, 0.06f }; }
static void rub(float t, float dur, float amp) { if (nscript < 512) script[nscript++] = (knock_t){ t, { 0, 0, 0 }, K_RUB, amp, dur }; }

static int first;                              // knocks before this one are over
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
        else { rub_lp += 0.3f * (n - rub_lp); float env = fminf(1.0f, dt / 0.02f) * fminf(1.0f, (script[i].dur - dt) / 0.02f); x += a * (n - rub_lp) * env; }
    }
    if (excite_period > 0) {                    // the click check's steady knocking: dull and sharp in turn
        float ph = fmodf(t, excite_period);
        int k = (int)(t / excite_period);
        float n = sim_noise();
        if (k & 1) { float h = n - hp_prev; hp_prev = n; x += 0.4f * h * expf(-ph / 0.006f); }
        else { lp_state += 0.05f * (n - lp_state); x += 1.6f * lp_state * expf(-ph / 0.02f); }
    }
    if (bleed > 0) { int k = (int)(t * (float)AUDIO_SR) - 192; if (k >= 0 && k < sim_n) x += bleed * sim_out[k]; }   // the speaker, heard 6 ms later (the DMA buffers both ways)
    return x;
}

// the accelerometer in screen axes at time t: gravity plus each knock's push and a little ringing
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

static const vec3_t P_FACE_PUSH = { 0, 0, -1 }, P_LEFT_PUSH = { 1, 0, 0 }, P_RIGHT_PUSH = { -1, 0, 0 }, P_TOP_PUSH = { 0, 1, 0 }, P_BOT_PUSH = { 0, -1, 0 };

static int notes_between(float t0, float t1, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < midi_n && n < max; i++)
        if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t0 && midi_log[i].t < t1) out[n++] = midi_log[i].d1;
    return n;
}
static int vel_of_note_after(float t0)
{
    for (int i = 0; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t0) return midi_log[i].d2;
    return -1;
}

// onsets in what was played: a fast envelope jumping over a slow one
static int out_onsets(float t0, float t1, float *at, int max)
{
    int n = 0, hold = 0; float fast = 0, slow = 0;
    for (int i = (int)(t0 * AUDIO_SR); i < (int)(t1 * AUDIO_SR) && i < sim_n; i++) {
        float a = fabsf(sim_out[i]);
        fast += (a > fast ? 0.3f : 0.003f) * (a - fast);
        slow += 0.0005f * (a - slow);
        if (hold > 0) hold--;
        else if (i > (int)((t0 + 0.1f) * AUDIO_SR) && fast > 2.2f * slow + 0.008f) { if (n < max) at[n++] = (float)i / AUDIO_SR; hold = AUDIO_SR / 8; }
    }
    return n;
}
static float first_sound(float t0, float level)    // seconds after t0 until the output passes level
{
    for (int i = (int)(t0 * AUDIO_SR); i < sim_n; i++) if (fabsf(sim_out[i]) > level) return (float)i / AUDIO_SR - t0;
    return 1.0f;
}

static int preset_index(const char *name) { for (int i = 0; i < INST.npresets; i++) if (!strcmp(INST.presets[i].name, name)) return i; return 0; }
static void load(const char *name) { front_midi(0xC0, (uint8_t)preset_index(name), 0); run(0.3f); }

void inst_excite(void)
{
    nscript = 0; first = 0; bleed = 0;
    excite_period = 0.125f;
    SI.mic = mic; SI.jx = SI.jy = 0;
    sim_tilt(0, 0);
}

// ---------------------------------------------------------------- the tests
void inst_tests(void)
{
    excite_period = 0; nscript = 0; first = 0; bleed = 0;
    SI.mic = mic; SI.jx = SI.jy = 0;
    sim_tilt(0, 0);
    load("STREET KIT");
    param_set(P_GRID, 0);
    run(1.0f);
    check(m_rms(sim_t - 0.5f, sim_t) < 0.003f, "no knocks: silence (rms %.4f)", (double)m_rms(sim_t - 0.5f, sim_t));

    // ---- each side is its own drum
    sim_gif_begin("play");
    const vec3_t pushes[5] = { P_FACE_PUSH, P_LEFT_PUSH, P_RIGHT_PUSH, P_TOP_PUSH, P_BOT_PUSH };
    const int want[5] = { 36, 38, 42, 45, 39 };
    float t0 = sim_t + 0.3f;
    for (int i = 0; i < 5; i++) knock(t0 + 0.45f * (float)i, pushes[i], K_DULL, 1.0f);
    run(0.3f + 0.45f * 2.0f + 0.05f); sim_shot("knocked_right");
    run(1.3f);
    int got[16], n = notes_between(t0 - 0.05f, t0 + 2.4f, got, 16), right = 0;
    for (int i = 0; i < 5 && i < n; i++) right += got[i] == want[i];
    check(n == 5 && right == 5, "SIDE: face, left, right, top, bottom give kick, snare, hat, tom, clap (%d heard, %d right)", n, right);
    float at5[8]; int na = out_onsets(t0 - 0.15f, t0 + 2.4f, at5, 8);
    float lat = 0; for (int i = 0; i < na && i < 5; i++) lat += at5[i] - (t0 + 0.45f * (float)i);
    metric("first_sound_latency_ms", (double)(lat / (float)(na ? (na < 5 ? na : 5) : 1) * 1000.0f));   // the BODY rings at once

    // ---- a loop: REC down, kick, snare, kick, REC up after 1.2 s
    float ts = sim_t + 0.1f;
    knock(ts + 0.2f, P_FACE_PUSH, K_DULL, 1.0f);
    knock(ts + 0.6f, P_LEFT_PUSH, K_DULL, 1.0f);
    knock(ts + 0.9f, P_FACE_PUSH, K_DULL, 1.0f);
    run(0.1f);
    SI.rec = true; run(0.5f); sim_shot("recording"); run(0.7f); SI.rec = false; step();
    float len = (float)eng_loop_len / AUDIO_SR;
    float rec_err = 0;
    const float knock_t[3] = { 0.2f, 0.6f, 0.9f };
    for (int i = 0; i < eng_nhits && i < 3; i++) {
        float e = fabsf((float)(eng_loop_start + eng_hits[i].off) / AUDIO_SR - (ts + knock_t[i]));
        if (e > rec_err) rec_err = e;
    }
    check(fabsf(len - 1.2f) < 0.02f && eng_nhits == 3, "REC held 1.2 s: a 1.2 s loop with 3 hits (%.3f s, %d hits)", (double)len, eng_nhits);
    run(3.7f); sim_shot("looping");
    sim_gif_end();
    float at[32]; int no = out_onsets(ts + 1.25f, ts + 4.8f, at, 32);
    int matched = 0;
    for (int i = 0; i < no; i++) {
        float ph = fmodf(at[i] - ts, 1.2f), best = 1;
        for (int k = 0; k < 3; k++) { float d = fabsf(ph - knock_t[k]); if (d < best) best = d; }
        if (best < 0.03f) matched++;
    }
    int lp[32]; int nlp = notes_between(ts + 1.25f, ts + 4.8f, lp, 32);
    check(nlp == 9 && no >= 5 && matched == no, "the loop repeats its 3 hits (%d notes in 3 passes; %d onsets heard, %d on time)", nlp, no, matched);
    float jit = 0;                                                  // the same hit, one pass later
    for (int i = 0; i + 3 < no; i++) { float d = fabsf(at[i + 3] - at[i] - len); if (d > jit) jit = d; }
    check(rec_err < 0.002f && jit < 0.002f, "loop hits are kept within 2 ms of the knocks (%.1f ms) and replay the same each pass (jitter %.1f ms)",
          (double)(rec_err * 1000.0f), (double)(jit * 1000.0f));

    // ---- the grid: 4 beats, 1/16, full strength: every hit on a sixteenth
    param_set(P_BEATS, 4); param_set(P_GRID, 4); param_set(P_SNAP, 100); param_set(P_SWING, 0);   // 4 beats: sixteenths of the whole loop
    run(0.1f);
    int on = 0, total = 0;
    for (int i = 0; i < eng_nhits; i++) {
        if (eng_hits[i].dead) continue;
        float g = (float)eng_loop_len / 16.0f, q = (float)eng_qoff(&eng_hits[i]);
        float r = fmodf(q, g); if (r > g / 2) r -= g;
        total++; if (fabsf(r) < 1.5f) on++; else note("hit at %.0f of %u: %.1f off a 1/16 of %.1f (beats %d grid %.0f snap %.0f)", (double)q, eng_loop_len, (double)r, (double)g, eng_beats(), (double)pv[P_GRID], (double)pv[P_SNAP]);
    }
    check(total == 3 && on == total, "GRID 1/16 at full strength: every hit on a sixteenth (%d of %d)", on, total);
    param_set(P_SWING, 50);
    float g16 = (float)eng_loop_len / 16.0f; int swung = 0;
    for (int i = 0; i < eng_nhits; i++) {
        float q = (float)eng_qoff(&eng_hits[i]);
        int k = (int)(q / g16 + 0.5f);
        if (k & 1) swung++;
        (void)k;
    }
    note("%d of the 3 hits fall on offbeat sixteenths (SWING moves only those)", swung);
    param_set(P_SWING, 0); param_set(P_GRID, 0);

    // ---- add a hat, stop, play, undo, clear
    knock(sim_t + 0.3f, P_RIGHT_PUSH, K_DULL, 1.0f);
    rec_hold(0.6f);
    check(eng_nhits == 4, "held REC again over the loop: a 4th hit added (%d)", eng_nhits);
    run(1.3f);
    sim_shot("added");
    rec_hold(0.1f); run(0.2f);
    check(eng_loop_run == 0, "a tap on REC stops the loop");
    float tq = sim_t; run(1.3f);
    int none[8]; int ns = notes_between(tq, sim_t, none, 8);
    check(ns == 0, "stopped: nothing plays (%d notes)", ns);
    rec_hold(0.1f); run(1.3f);
    check(eng_loop_run == 1 && notes_between(sim_t - 1.3f, sim_t, none, 8) >= 3, "another tap plays it from the top");
    int live = 0; for (int i = 0; i < eng_nhits; i++) live += !eng_hits[i].dead;
    INST.action(P_UNDO); run(0.1f);
    int live2 = 0; for (int i = 0; i < eng_nhits; i++) live2 += !eng_hits[i].dead;
    check(live == 4 && live2 == 3, "UNDO LAST takes the added hat away (%d -> %d hits)", live, live2);
    INST.action(P_CLEAR); run(0.2f);
    check(eng_loop_len == 0 && eng_nhits == 0, "CLEAR LOOP erases it");
    int pinned = 0; for (int i = 0; i < NSNIP; i++) pinned += eng_pinned[i];
    check(pinned == 0, "no snippets are held after a clear (%d)", pinned);

    // ---- a press shorter than a hold is never a recording
    knock(sim_t + 0.05f, P_FACE_PUSH, K_DULL, 1.0f);
    rec_hold(0.15f); run(0.5f);
    check(eng_loop_len == 0 && eng_nhits == 0, "a short tap with no loop records nothing");

    // ---- the stick: up rolls the last drum, down repeats the last slice
    sim_gif_begin("stick");
    knock(sim_t + 0.1f, P_LEFT_PUSH, K_DULL, 1.0f);
    run(0.4f);
    SI.jy = 0.75f; float tr = sim_t; run(1.0f); sim_shot("roll");
    int rolls[64]; int nr = notes_between(tr + 0.05f, sim_t, rolls, 64), snares = 0;
    for (int i = 0; i < nr; i++) snares += rolls[i] == 38;
    check(nr >= 10 && nr <= 14 && snares == nr, "stick up: a roll of the last drum (snare), 12 a second without a loop (%d in 0.95 s)", nr);
    SI.jy = 0; run(0.5f);
    // a loop to stutter
    float tl = sim_t + 0.05f;
    knock(tl + 0.05f, P_FACE_PUSH, K_DULL, 1.0f); knock(tl + 0.3f, P_RIGHT_PUSH, K_SHARP, 1.0f);
    knock(tl + 0.55f, P_LEFT_PUSH, K_DULL, 1.0f); knock(tl + 0.8f, P_RIGHT_PUSH, K_SHARP, 1.0f);
    run(0.05f); rec_hold(1.0f); run(1.0f);
    SI.jy = -0.75f; run(0.3f);
    float ts0 = sim_t; run(0.6f); sim_shot("stutter");
    SI.jy = 0; run(0.4f);
    sim_gif_end();
    {   // the stutter repeats a slice of a quarter of a beat: the output matches itself one slice later
        float slice = (float)eng_loop_len / AUDIO_SR / (float)eng_beats() / 4.0f;
        int L = (int)(slice * AUDIO_SR), a = (int)(ts0 * AUDIO_SR);
        double c = 0, e0 = 0, e1 = 0;
        for (int i = 0; i < 4 * L; i++) { float x0 = sim_out[a + i], x1 = sim_out[a + i + L]; c += x0 * x1; e0 += x0 * x0; e1 += x1 * x1; }
        float r = (float)(c / sqrt(e0 * e1 + 1e-12));
        check(r > 0.9f, "stick down: the last 1/16 repeats (correlation %.2f at %.0f ms)", (double)r, (double)(slice * 1000.0f));
    }
    INST.action(P_CLEAR); run(0.2f);

    // ---- MIDI in: a snare note plays the snare side
    int m0 = midi_n;
    front_midi(0x90, 38, 100); run(0.2f);
    int got1[4]; int ng1 = notes_between(sim_t - 0.25f, sim_t, got1, 4);
    check(ng1 == 1 && got1[0] == 38 && midi_n > m0, "MIDI note 38 in plays the snare (and sends it out on channel 10)");

    // ---- velocity: a hard knock louder than a soft one
    for (int i = 0; i < 6; i++) knock(sim_t + 0.1f + 0.3f * (float)i, P_FACE_PUSH, K_DULL, 0.6f);
    run(2.0f);
    float th = sim_t + 0.1f; knock(th, P_FACE_PUSH, K_DULL, 1.4f);
    float tsft = sim_t + 1.3f; knock(tsft, P_FACE_PUSH, K_DULL, 0.4f);
    run(1.6f);
    int vh = vel_of_note_after(th - 0.01f), vs = vel_of_note_after(tsft - 0.01f);
    float ph = m_peak(th, th + 0.2f), ps = m_peak(tsft, tsft + 0.2f);
    check(vs > 0 && vh > vs + 30 && ph > 1.5f * ps, "a hard knock is louder than a soft one (velocity %d vs %d, peak %.2f vs %.2f)", vh, vs, (double)ph, (double)ps);

    // ---- the box's own sound back into the mic (a speaker at -6 dB, 2 ms away) starts nothing
    bleed = 0.5f;
    load("808 PARTY");
    float tb = sim_t + 0.1f;
    for (int i = 0; i < 4; i++) knock(tb + 0.4f * (float)i, pushes[i], K_DULL, 1.0f);
    run(2.0f);
    int nb = notes_between(tb - 0.05f, sim_t, got, 16);
    check(nb == 4, "with the speaker heard by the mic, 4 knocks are still 4 drums (%d)", nb);
    bleed = 0;
    run(2.0f);
    float tv = sim_t + 0.05f; knock(tv, P_FACE_PUSH, K_DULL, 1.0f); run(0.4f);
    metric("drum_voice_latency_ms", (double)(first_sound(tv, 0.02f) * 1000.0f));   // 808 PARTY: no body, the voice waits for the side

    // ---- BODY by side: a marimba, each side its own note of the pentatonic scale
    load("MARIMBA");
    run(2.5f);                                                     // the 808's kick and room die away first
    sim_gif_begin("marimba");
    const int deg[5] = { 0, 2, 4, 7, 9 };
    int good = 0;
    for (int i = 0; i < 5; i++) {
        float tk = sim_t + 0.05f;
        knock(tk, pushes[i], K_DULL, 1.0f);
        run(0.6f);
        if (i == 2) sim_shot("marimba");
        float f = m_pitch_at(tk + 0.12f, tk + 0.5f, 150, 900);
        float want_m = 60.0f + (float)deg[i];
        if (fabsf(hz_to_midi(f) - want_m) < 0.3f) good++;
        else note("side %d: %.1f Hz (midi %.2f), wanted %.0f", i, (double)f, (double)hz_to_midi(f), (double)want_m);
    }
    sim_gif_end();
    check(good == 5, "MARIMBA: the five sides ring C4 D4 E4 G4 A4 (%d right)", good);
    run(3.0f);                                                     // quiet first: the tails would count
    float tk = sim_t + 0.05f; knock(tk, P_LEFT_PUSH, K_DULL, 1.0f); run(0.4f);
    float lat_side = first_sound(tk, 0.01f);
    metric("body_by_side_latency_ms", (double)(lat_side * 1000.0f));
    check(lat_side > 0.008f && lat_side < 0.03f, "BY SIDE rings %.1f ms after the knock (it waits for the side; under 30)", (double)(lat_side * 1000.0f));

    // ---- BODY in one pitch rings at once, and rubbing the box keeps it ringing
    load("SCRATCH BOX");
    run(3.0f);
    tk = sim_t + 0.05f; knock(tk, P_FACE_PUSH, K_DULL, 1.0f); run(0.5f);
    float lat_one = first_sound(tk, 0.01f);
    metric("body_live_latency_ms", (double)(lat_one * 1000.0f));
    check(lat_one < 0.004f, "ONE pitch: the knock rings the body at once (%.1f ms)", (double)(lat_one * 1000.0f));
    run(7.0f);                                                     // the glass rings 4 s: let it die
    float tq2 = sim_t;
    run(0.3f);
    float quiet = m_rms(tq2, sim_t);
    float trb = sim_t + 0.05f; rub(trb, 1.2f, 0.08f);
    sim_gif_begin("rub"); run(1.4f); sim_gif_end();
    float rubbed = m_rms(trb + 0.3f, trb + 1.1f);
    check(rubbed > 0.008f && rubbed > 8.0f * quiet, "rubbing the box keeps the glass ringing (rms %.3f, %.4f before)", (double)rubbed, (double)quiet);
    sim_shot("rubbed");

    // ---- a few sounds on screen
    load("BELL TOWER"); knock(sim_t + 0.05f, P_TOP_PUSH, K_DULL, 1.0f); run(0.3f); sim_shot("bell_tower");
    load("ZAP GUN"); knock(sim_t + 0.05f, P_RIGHT_PUSH, K_SHARP, 1.0f); run(0.15f); sim_shot("zap_gun");
    load("STREET KIT");

    // ---- every sound makes sound, none clips by itself
    int quietest = 0; float qv = 1, loudest = 0;
    for (int p = 0; p < INST.npresets; p++) {
        front_midi(0xC0, (uint8_t)p, 0); run(0.3f);
        float tp = sim_t + 0.05f;
        for (int i = 0; i < 5; i++) knock(tp + 0.25f * (float)i, pushes[i], K_DULL, 1.0f);
        run(1.6f);
        float r = m_rms(tp, tp + 1.4f), pk = m_peak(tp, tp + 1.4f);
        note("%-14s peak %.2f  rms %.3f", INST.presets[p].name, (double)pk, (double)r);
        if (r < qv) { qv = r; quietest = p; }
        if (pk > loudest) loudest = pk;
    }
    check(qv > 0.01f, "every sound answers a knock (quietest %s, rms %.3f)", INST.presets[quietest].name, (double)qv);
    metric("loudest_sound_peak", loudest);
    load("STREET KIT");

    // ---- the engine's time per 64-frame block on this machine, busy (knocks, bodies, echo, room)
    {
        load("BELL TOWER");
        param_set(P_FACE, V_KICK); param_set(P_LEFT, V_SNARE); param_set(P_RIGHT, V_OHAT); param_set(P_ROOM, 60);
        float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
        excite_period = 0.1f;
        struct timespec a, b;
        double tsum = 0; int nblk = 4000;
        float tt = sim_t;
        for (int k = 0; k < nblk; k++) {
            for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = mic(tt + (float)i / (float)AUDIO_SR, NULL);
            tt += (float)AUDIO_BLOCK / (float)AUDIO_SR;
            if (k % 50 == 0) { trig_t t = { (uint8_t)(k / 50 % 5), 100, SRC_MIDI, -1 }; eng_trigger(&t); }
            clock_gettime(CLOCK_MONOTONIC, &a);
            front_audio(in, out, AUDIO_BLOCK);
            clock_gettime(CLOCK_MONOTONIC, &b);
            tsum += (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
        }
        excite_period = 0;
        double us = tsum / nblk;
        metric("host_us_per_block", us);
        check(us < 20.0, "the engine takes %.1f us per 64-frame block on this machine (under 20)", us);
        load("STREET KIT");
    }
    nscript = 0;
    run(0.5f);
}
