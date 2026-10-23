// AIR on the host: strokes as gyro profiles (a half sine of rate along the stroke's axis, then the hand coming
// back the other way, slower) with the accelerometer's jolt that goes with them (the box on a 15 cm arm: r w^2 and
// r dw/dt); one stroke per pad and from both sideways axes; the return swing that must not count; velocity; a slow
// turn that is not a hit; CONFIRM (a fast turn without a jolt is not a hit); the nearest-line quantize (late hits
// sound at once, early ones wait); the loops (whole bars from the bar after the press, four stacked, the stick and
// the menu switching them, the knob stopping them all, a tempo change they follow, erase, the newest replaced);
// a board on the link (the loop on its grid, its tempo kept under a clash, TEMPO locked by a peer's loop, HANDS
// AUTO); sampling by a tap on REC, clockwise; SWING SETUP; MIDI in; the stick's roll; every sound; screens and GIFs.
#include "sim.h"
#include "inst_air.h"
#include "inst_kit.h"
#include "link.h"
#include <math.h>
#include <string.h>
#include <time.h>

int air_gest_state(void);
int air_peers(void);
int air_rec_state(void);
int air_hands(void);
float air_jolt(void);

const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", NULL };   // the radio's switches, not the sound

// ---------------------------------------------------------------- the strokes
typedef struct { float t; vec3_t dir; float peak, dur, ret; } stroke_t;
static stroke_t script[256];
static int nscript, first;
static float excite_period;                 // inst_excite: a stroke this often, by itself
static bool acc_flat;                       // the accelerometer reads 1 g whatever the gyro does (a turn with no jolt)

static const vec3_t D_FRONT = { 1, 0, 0 }, D_BACK = { -1, 0, 0 }, D_LEFT = { 0, -1, 0 }, D_RIGHT = { 0, 1, 0 }, D_LEFT_Z = { 0, 0, -1 }, D_RIGHT_Z = { 0, 0, 1 };

static void stroke(float t, vec3_t dir, float peak, float dur, float ret) { if (nscript < 256) script[nscript++] = (stroke_t){ t, dir, peak, dur, ret }; }
static void swing(float t, vec3_t dir, float peak) { stroke(t, dir, peak, 0.12f, 0.5f); }

// the gyro at time t: each stroke a half sine of its peak over dur, then the return: ret x peak the other way
// over 1.5 x dur, after a 20 ms pause at the bottom
static vec3_t gyro_at(float t)
{
    vec3_t g = { 0, 0, 0 };
    while (first < nscript && t - script[first].t > script[first].dur * 2.5f + 0.1f) first++;
    for (int i = first; i < nscript; i++) {
        const stroke_t *s = &script[i];
        float dt = t - s->t, a = 0;
        if (dt < 0) continue;
        if (dt < s->dur) a = s->peak * sinf(3.14159f * dt / s->dur);
        else if (dt > s->dur + 0.02f && dt < s->dur * 2.5f + 0.02f) a = -s->ret * s->peak * sinf(3.14159f * (dt - s->dur - 0.02f) / (1.5f * s->dur));
        g.x += a * s->dir.x; g.y += a * s->dir.y; g.z += a * s->dir.z;
    }
    if (excite_period > 0) {                 // the click check's steady playing: four strokes round and round
        float ph = fmodf(t, excite_period); int k = (int)(t / excite_period) & 3;
        const vec3_t d[4] = { D_FRONT, D_LEFT, D_RIGHT, D_BACK };
        if (ph < 0.1f) { float a = 700.0f * sinf(3.14159f * ph / 0.1f); g.x += a * d[k].x; g.y += a * d[k].y; g.z += a * d[k].z; }
    }
    return g;
}
static float gyro_mag_at(float t) { vec3_t g = gyro_at(t); return sqrtf(g.x * g.x + g.y * g.y + g.z * g.z); }

// the accelerometer at 1 kHz: gravity plus the jolt of the swing, the box 15 cm from the wrist
static void burst_fill(float t0)
{
    const float r = 0.15f, g0 = 9.81f;
    for (int i = 0; i < 10; i++) {
        float t = t0 + 0.001f * (float)i;
        float w = gyro_mag_at(t) * 0.0174533f, w1 = gyro_mag_at(t + 0.001f) * 0.0174533f;
        float a = acc_flat ? 0.0f : (r * w * w + r * fabsf(w1 - w) * 1000.0f) / g0;
        SI.burst[i] = (vec3_t){ 0, 0, 1.0f + a };
    }
    SI.nburst = 10;
}

// ---------------------------------------------------------------- the mic: room noise, a burst to sample, the motion as a side effect
static float burst_t = -1, burst_len = 0.3f, burst_amp;
static float mic(float t, void *u)
{
    (void)u;
    SI.gyro = gyro_at(t);                    // the sim has no gyro hook of its own: the mic callback runs every tick before the controls
    burst_fill(t);
    float x = 0.0008f * sim_noise();
    if (burst_t >= 0 && t >= burst_t && t < burst_t + burst_len) {
        float dt = t - burst_t;
        x += burst_amp * sim_noise() * expf(-dt / (burst_len * 0.3f)) + 0.5f * burst_amp * sinf(6.2831853f * 220.0f * dt) * expf(-dt / (burst_len * 0.5f));
    }
    return x;
}
static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static void rec_hold(float s) { SI.rec = true; run(s); SI.rec = false; sim_tick(); }
static void rec_tap(void) { rec_hold(0.1f); }
static void run_until(float t) { while (sim_t < t) sim_tick(); }

static int notes_between(float t0, float t1, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < midi_n && n < max; i++)
        if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t0 && midi_log[i].t < t1) out[n++] = midi_log[i].d1;
    return n;
}
static int note_times_between(float t0, float t1, int note, float *at, int max)
{
    int n = 0;
    for (int i = 0; i < midi_n && n < max; i++)
        if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t0 && midi_log[i].t < t1 && (note < 0 || midi_log[i].d1 == note)) at[n++] = midi_log[i].t;
    return n;
}
static float note_time_after(float t0)
{
    for (int i = 0; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t0) return midi_log[i].t;
    return -1;
}
static int vel_of_note_after(float t0)
{
    for (int i = 0; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t0) return midi_log[i].d2;
    return -1;
}
static float first_sound(float t0, float level)    // seconds after t0 until the output passes level
{
    for (int i = (int)(t0 * AUDIO_SR); i < sim_n; i++) if (fabsf(sim_out[i]) > level) return (float)i / AUDIO_SR - t0;
    return 1.0f;
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

// onsets by level: the first sample over thr, then 250 ms of nothing (the drums are the same each pass, so this lands
// on the same sample of the same hit)
static int onsets_level(float t0, float t1, float thr, float *at, int max)
{
    int n = 0, hold = 0;
    for (int i = (int)(t0 * AUDIO_SR); i < (int)(t1 * AUDIO_SR) && i < sim_n; i++) {
        if (hold > 0) { hold--; continue; }
        if (fabsf(sim_out[i]) > thr) { if (n < max) at[n++] = (float)i / AUDIO_SR; hold = AUDIO_SR * 25 / 100; }
    }
    return n;
}

static int preset_index(const char *name) { for (int i = 0; i < INST.npresets; i++) if (!strcmp(INST.presets[i].name, name)) return i; return 0; }
static void load(const char *name) { front_midi(0xC0, (uint8_t)preset_index(name), 0); run(0.3f); }

// the grid, in seconds: the next bar line (or the next line of a division of the beat) after t
static float frame_s(uint32_t f) { return (float)f / (float)AUDIO_SR; }
static float next_line_after(float t, int ticks)
{
    grid_t g; eng_get_grid(&g);
    int64_t tk = eng_tick_at(&g, (uint32_t)(t * AUDIO_SR));
    int64_t line = (eng_floor_div(tk, ticks) + 1) * ticks;
    return frame_s(eng_frame_of(&g, line));
}
static float next_bar_after(float t) { return next_line_after(t, BAR_TICKS); }
static float ms_off_line(float t, int ticks)       // how far t is from the nearest line of that division, ms
{
    grid_t g; eng_get_grid(&g);
    int64_t tk = eng_tick_at(&g, (uint32_t)(t * AUDIO_SR));
    int64_t a = eng_floor_div(tk, ticks) * ticks, b = a + ticks;
    float ta = frame_s(eng_frame_of(&g, a)), tb = frame_s(eng_frame_of(&g, b));
    float d = fminf(fabsf(t - ta), fabsf(t - tb));
    return d * 1000.0f;
}
static int loops_used(void) { int n = 0; for (int k = 0; k < NLOOP; k++) n += eng_loops[k].bars > 0; return n; }

void inst_excite(void)
{
    nscript = 0; first = 0; burst_t = -1; acc_flat = false;
    excite_period = 0.2f;
    SI.mic = mic; SI.jx = SI.jy = 0;
    sim_tilt(0, 0);
}

// ---------------------------------------------------------------- the tests
void inst_tests(void)
{
    excite_period = 0; nscript = 0; first = 0; burst_t = -1; acc_flat = false;
    SI.mic = mic; SI.jx = SI.jy = 0;
    sim_tilt(0, 0);
    load("SYNTH KIT");                                                 // KNOCK's voices: one GM note per pad
    param_set(P_QUANT, 0);                                             // hits as they come; the grid has its own section
    run(1.0f);
    check(m_rms(sim_t - 0.5f, sim_t) < 0.003f, "no strokes: silence (rms %.4f)", (double)m_rms(sim_t - 0.5f, sim_t));

    // ---- four strokes, four drums
    section("strokes");
    sim_gif_begin("play");
    const vec3_t dirs[4] = { D_FRONT, D_LEFT, D_RIGHT, D_BACK };
    const int want[4] = { 42, 36, 38, 46 };
    float t0 = sim_t + 0.3f;
    for (int i = 0; i < 4; i++) swing(t0 + 0.5f * (float)i, dirs[i], 600.0f);
    run(0.3f + 0.5f + 0.09f); sim_shot("kick");
    run(0.5f); sim_shot("snare");
    run(1.3f);
    int got[16], n = notes_between(t0 - 0.05f, t0 + 2.2f, got, 16), right = 0;
    for (int i = 0; i < 4 && i < n; i++) right += got[i] == want[i];
    check(n == 4 && right == 4, "forward, left, right, back give hat, kick, snare, open hat (%d heard, %d right)", n, right);
    // when the stroke sounds: soon after the rate's peak
    float lat = 0; int nl = 0;
    for (int i = 0; i < 4; i++) { float tn = note_time_after(t0 + 0.5f * (float)i); if (tn > 0) { lat += tn - (t0 + 0.5f * (float)i + 0.06f); nl++; } }
    lat = nl ? lat / (float)nl : 1.0f;
    metric("hit_after_peak_ms", (double)(lat * 1000.0f));
    check(lat >= 0.0f && lat < 0.045f, "a drum sounds %.0f ms after the stroke's fastest point (under 45)", (double)(lat * 1000.0f));
    float snd = first_sound(t0 + 0.06f, 0.02f);
    metric("sound_after_peak_ms", (double)(snd * 1000.0f));

    // sideways about the other axis (the box held flat and swung) is the same drum
    t0 = sim_t + 0.2f;
    swing(t0, D_LEFT_Z, 600.0f); swing(t0 + 0.5f, D_RIGHT_Z, 600.0f);
    run(1.3f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 2 && got[0] == 36 && got[1] == 38, "swung flat (about z) left and right: kick and snare too (%d heard: %d %d)", n, n > 0 ? got[0] : 0, n > 1 ? got[1] : 0);

    // ---- the hand coming back is not a hit; a real stroke back is
    t0 = sim_t + 0.2f;
    for (int i = 0; i < 4; i++) stroke(t0 + 0.5f * (float)i, D_FRONT, 600.0f, 0.12f, 0.7f);   // returns at 70 %
    run(2.3f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    int hats = 0; for (int i = 0; i < n; i++) hats += got[i] == 42;
    check(n == 4 && hats == 4, "four hat strokes, each with the hand coming back at 70 %%: four hats, no open hats (%d notes, %d hats)", n, hats);
    t0 = sim_t + 0.2f;
    stroke(t0, D_FRONT, 600.0f, 0.12f, 0.0f); stroke(t0 + 0.16f, D_BACK, 600.0f, 0.12f, 0.0f);   // a flick: forward then back as hard
    run(1.0f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 2 && got[0] == 42 && got[1] == 46, "a flick forward and back as hard: hat then open hat (%d notes)", n);

    // ---- velocity: a hard swing is louder than a soft one
    float th = sim_t + 0.2f; swing(th, D_RIGHT, 1000.0f);
    float ts = th + 1.0f; swing(ts, D_RIGHT, 420.0f);
    run(2.0f);
    int vh = vel_of_note_after(th - 0.01f), vs = vel_of_note_after(ts - 0.01f);
    float ph = m_peak(th, th + 0.3f), ps = m_peak(ts, ts + 0.3f);
    check(vs > 0 && vh > vs + 30 && ph > 1.4f * ps, "a hard swing is louder than a soft one (velocity %d vs %d, peak %.2f vs %.2f)", vh, vs, (double)ph, (double)ps);

    // ---- a slow turn is not a hit, a wobble is not a hit
    t0 = sim_t + 0.2f;
    stroke(t0, D_FRONT, 90.0f, 1.0f, 0.0f);                              // tilting it to tune: 90 deg/s at most
    stroke(t0 + 1.5f, D_LEFT, 150.0f, 0.1f, 0.0f);                       // a nudge under the hit threshold
    run(2.2f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 0, "a slow turn (90 deg/s for a second) and a nudge (150 deg/s) play nothing (%d notes)", n);

    // ---- CONFIRM: a turn as fast as a stroke but with no jolt in the accelerometer is not a hit
    acc_flat = true;
    t0 = sim_t + 0.2f; swing(t0, D_RIGHT, 600.0f); run(0.8f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 0, "CONFIRM: a 600 deg/s turn with the accelerometer flat at 1 g plays nothing (%d notes, jolt %.2f g)", n, (double)air_jolt());
    param_set(P_CONFIRM, 0);
    t0 = sim_t + 0.2f; swing(t0, D_RIGHT, 600.0f); run(0.8f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 1, "CONFIRM off: the same turn is a snare (%d notes)", n);
    param_set(P_CONFIRM, 1); acc_flat = false;

    // ---- the reset: one long turn is one hit; the same pad again needs the box turned back, or held still a moment
    t0 = sim_t + 0.2f;
    for (int i = 0; i < 3; i++) stroke(t0 + 0.3f * (float)i, D_FRONT, 600.0f, 0.12f, 0.0f);   // three pushes on, no return, 0.3 s apart
    run(1.5f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 1, "three turns the same way 0.3 s apart, never back: one hat, not three (%d)", n);
    t0 = sim_t + 0.2f;
    for (int i = 0; i < 3; i++) stroke(t0 + 0.5f * (float)i, D_FRONT, 600.0f, 0.12f, 0.0f);   // 0.5 s apart: still in between
    run(2.0f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 3, "the same, held still 0.4 s between them: three hats (%d)", n);
    t0 = sim_t + 0.2f;
    for (int i = 0; i < 3; i++) stroke(t0 + 0.35f * (float)i, D_FRONT, 600.0f, 0.1f, 0.5f);   // with the hand coming back: a roll
    run(1.5f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 3, "three strokes 0.35 s apart with the hand coming back each time: three hats (%d)", n);
    t0 = sim_t + 0.2f;
    stroke(t0, D_FRONT, 600.0f, 0.12f, 0.0f); stroke(t0 + 0.3f, D_RIGHT, 600.0f, 0.12f, 0.0f);   // another pad needs no reset
    run(1.0f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 2 && got[1] == 38, "a hat then a snare straight after, no reset needed (%d notes)", n);

    // ---- the tilt tunes: bottom edge down, the kick is higher
    param_set(P_FRONT + PD_LEFT, V_TOM);
    sim_tilt(0, 0); run(0.3f);
    float ta = sim_t + 0.1f; swing(ta, D_LEFT, 600.0f); run(0.5f);
    float fa = m_pitch_at(ta + 0.1f, ta + 0.3f, 60, 300);
    sim_tilt(0, 25); run(0.6f);
    float tb = sim_t + 0.1f; swing(tb, D_LEFT, 600.0f); run(0.5f);
    float fb = m_pitch_at(tb + 0.1f, tb + 0.3f, 60, 300);
    sim_tilt(0, 0); param_set(P_FRONT + PD_LEFT, V_KICK); run(0.3f);
    check(fa > 0 && fb > fa * 1.15f, "bottom edge down 25 degrees: the tom is tuned up (%.0f -> %.0f Hz)", (double)fa, (double)fb);
    sim_gif_end();                                                     // the play clip: the strokes

    // ---- SWING SETUP: four swings, then the thresholds fit them
    section("setup");
    INST.action(P_SETUP); run(0.3f); sim_shot("setup");
    t0 = sim_t + 0.2f;
    for (int i = 0; i < 4; i++) swing(t0 + 0.6f * (float)i, D_FRONT, 500.0f);
    run(0.3f); int nset = notes_between(t0 - 0.1f, sim_t + 2.2f, got, 16); run(2.2f);
    nset += notes_between(t0 - 0.1f, sim_t, got, 16);
    check(nset == 0 && pv[P_CALV] > 600.0f && pv[P_CALV] < 700.0f && pv[P_CALH] > 180.0f && pv[P_CALH] < 220.0f,
          "four swings at 500 deg/s, none sounding: full velocity at %.0f, a hit from %.0f", (double)pv[P_CALV], (double)pv[P_CALH]);
    run(2.0f);
    t0 = sim_t + 0.2f; swing(t0, D_FRONT, 280.0f); run(0.8f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    check(n == 1, "after it a 280 deg/s swing is a hit (it was under the 324 before) (%d notes)", n);
    param_set(P_CALV, 0); param_set(P_CALH, 0);

    // ---- quantize to the nearest sixteenth: a late hit sounds at once, an early one waits for the line
    section("quantize");
    param_set(P_QUANT, 2); param_set(P_TEMPO, 120); run(0.5f);
    {
        int late_now = 0, early_on = 0;
        for (int i = 0; i < 3; i++) {
            float line = next_line_after(sim_t + 0.5f, PPB / 4);
            float tq = line - 0.07f;                                    // fires about 30 ms after the line: late
            swing(tq, D_LEFT, 700.0f); run_until(line + 0.4f);
            float at[4]; int nn = onsets_level(tq, tq + 0.3f, 0.1f, at, 4);
            if (nn && at[0] - (tq + 0.06f) < 0.06f) late_now++;
            line = next_line_after(sim_t + 0.5f, PPB / 4);
            tq = line - 0.115f;                                         // fires about 20 ms before the line: early
            swing(tq, D_LEFT, 700.0f); run_until(line + 0.4f);
            nn = onsets_level(tq, tq + 0.3f, 0.1f, at, 4);
            if (nn && fabsf(at[0] - line) < 0.004f) early_on++;
        }
        check(late_now == 3 && early_on == 3, "QUANTIZE 1/16 at 120: a hit just after a line sounds at once (%d of 3), one just before waits for it (%d of 3)", late_now, early_on);
    }

    // ---- the loops: hold REC, the loop runs from the bar after the press to the bar you let go in
    section("loops");
    load("SYNTH KIT"); param_set(P_TEMPO, 120); run(0.5f);
    float bar = next_bar_after(sim_t + 1.0f);
    float at[64];
    sim_gif_begin("loop");
    swing(bar + 0.0f - 0.12f, D_LEFT, 800.0f); swing(bar + 0.5f - 0.12f, D_LEFT, 800.0f); swing(bar + 1.0f - 0.12f, D_LEFT, 800.0f);   // three kicks on beats 1 2 3
    swing(bar + 2.0f - 0.12f, D_RIGHT, 800.0f);                        // a snare on the one of bar 2
    run_until(bar - 0.6f); SI.rec = true;                              // pressed 0.6 s before the bar: it waits for it
    run(0.35f);
    check(air_rec_state() == 2, "REC held 0.6 s before a bar: it counts in to the bar (state %d)", air_rec_state());
    sim_shot("count_in");
    run_until(bar + 1.2f); sim_shot("recording");
    run_until(bar + 3.8f); SI.rec = false; sim_tick();                  // let go 0.2 s before the end of bar 2: it records on to it
    check(air_rec_state() == 4, "let go 0.2 s before the end of bar 2: it records on to the bar (state %d)", air_rec_state());
    run_until(bar + 4.3f);
    check(air_rec_state() == 0 && eng_loops[0].bars == 2 && eng_loops[0].n == 4 && eng_loops[0].on && (int)pv[P_LOOP1] == LP_ON,
          "loop 1: 2 bars, 4 hits, on (bars %d, hits %d, LOOP 1 = %d)", eng_loops[0].bars, eng_loops[0].n, (int)pv[P_LOOP1]);
    check(link_time()->beats == 8 && (link_flags_any() & LINKF_LOOP), "it tells the flock: 8 beats, a loop here (beats %d)", link_time()->beats);
    run_until(bar + 8.2f); sim_shot("looping");
    sim_gif_end();
    {
        float kt[16]; int nk = note_times_between(bar + 3.9f, bar + 7.9f, 36, kt, 16);
        float st[16]; int ns = note_times_between(bar + 3.9f, bar + 7.9f, 38, st, 16);
        float worst = 0; const float want_k[3] = { 4.0f, 4.5f, 5.0f };
        for (int i = 0; i < nk && i < 3; i++) { float d = fabsf(kt[i] - (bar + want_k[i])) * 1000.0f; if (d > worst) worst = d; }
        if (ns) { float d = fabsf(st[0] - (bar + 6.0f)) * 1000.0f; if (d > worst) worst = d; }
        check(nk == 3 && ns == 1 && worst < 8.0f, "the pass after: three kicks on beats 1 2 3 and the snare on the one of bar 2 (%d kicks, %d snares, worst %.1f ms off)", nk, ns, (double)worst);
    }
    // a second loop on top: one bar of hats on the offbeats; loop 1 keeps playing
    float bar2 = next_bar_after(sim_t + 0.5f);
    run_until(bar2 - 0.1f); SI.rec = true;
    swing(bar2 + 0.25f - 0.12f, D_FRONT, 700.0f); swing(bar2 + 0.75f - 0.12f, D_FRONT, 700.0f); swing(bar2 + 1.25f - 0.12f, D_FRONT, 700.0f); swing(bar2 + 1.75f - 0.12f, D_FRONT, 700.0f);
    run_until(bar2 + 1.6f); SI.rec = false; sim_tick();                  // let go at 0.8 of a bar: one bar
    run_until(bar2 + 2.3f); sim_shot("two_loops");
    check(eng_loops[1].bars == 1 && eng_loops[1].n == 4 && eng_loops[1].on && eng_loops[0].on, "loop 2: 1 bar of 4 hats, loop 1 still on (bars %d, hits %d)", eng_loops[1].bars, eng_loops[1].n);
    run_until(bar2 + 6.0f);
    {
        float ht[32]; int nh = note_times_between(bar2 + 1.9f, bar2 + 5.9f, 42, ht, 32);
        float kt[32]; int nk = note_times_between(bar2 + 1.9f, bar2 + 5.9f, 36, kt, 32);
        float worst = 0;
        for (int i = 0; i < nh; i++) { float d = ms_off_line(ht[i], PPB / 2); if (d > worst) worst = d; }
        check(nh == 8 && nk >= 2 && worst < 8.0f, "two bars on: 8 hats on the offbeat eighths (worst %.1f ms off), the kicks under them (%d)", (double)worst, nk);
    }
    // the stick's right switches loop 2 off and on; the menu's LOOP 2 too
    SI.jx = 0.9f; run(0.1f); SI.jx = 0; run(0.1f);
    check(eng_loops[1].on == 0 && (int)pv[P_LOOP2] == LP_OFF, "the stick pushed right: loop 2 off (LOOP 2 = %d)", (int)pv[P_LOOP2]);
    {   float tq = sim_t; run(2.1f); int hh[32]; int nh = notes_between(tq, sim_t, hh, 32), h2 = 0; for (int i = 0; i < nh; i++) h2 += hh[i] == 42;
        check(h2 == 0 && nh >= 3, "off: no hats, the kicks go on (%d hats, %d notes)", h2, nh); }
    param_set(P_LOOP2, LP_ON); run(0.1f);
    check(eng_loops[1].on == 1, "LOOP 2 ON in the menu: it plays again");
    param_set(P_LOOP3, LP_ON); run(0.1f);
    check((int)pv[P_LOOP3] == LP_EMPTY, "LOOP 3 ON with nothing in it: it stays EMPTY");
    // the knob held: everything stops, and goes again
    sim_hold_knob(); run(0.1f);
    check(eng_run == 0, "the knob held: the loops stop");
    sim_shot("stopped");
    {   float tq = sim_t; run(2.1f); int hh[8]; int nn = notes_between(tq, sim_t, hh, 8);
        check(nn == 0, "stopped: nothing plays (%d notes)", nn); }
    sim_hold_knob(); run(0.1f);
    check(eng_run == 1, "held again: they go");
    // the tempo moves and the loops follow: the three kicks a beat apart at the new speed
    param_set(P_TEMPO, 150); run(0.3f);
    {   float tq = sim_t; run(3.5f); float kt[32]; int nk = note_times_between(tq, sim_t, 36, kt, 32);
        float gap = 0; int ng = 0; for (int i = 1; i < nk; i++) { float d = kt[i] - kt[i - 1]; if (d < 0.6f) { gap += d; ng++; } }
        gap = ng ? gap / (float)ng : 0;
        check(ng >= 2 && fabsf(gap - 0.4f) < 0.01f && link_fpb() == 12800, "TEMPO 150: the kicks come %.3f s apart (a beat at 150: 0.400), the flock told (%u frames a beat)", (double)gap, link_fpb()); }
    param_set(P_TEMPO, 120); run(0.3f);
    // erase the newest, erase all
    INST.action(P_ERASE_LAST); run(0.1f);
    check(eng_loops[1].bars == 0 && (int)pv[P_LOOP2] == LP_EMPTY && eng_loops[0].bars == 2, "ERASE LAST: loop 2 gone, loop 1 stays");
    INST.action(P_ERASE_ALL); run(0.2f);
    check(loops_used() == 0 && !(link_flags_any() & LINKF_LOOP) && link_time()->beats == 0, "ERASE ALL: no loops, the flock's tempo is free again (beats %d)", link_time()->beats);
    // four loops, then a fifth: the newest is recorded over
    for (int k = 0; k < 5; k++) {
        float b = next_bar_after(sim_t + 0.3f);
        run_until(b - 0.1f); SI.rec = true;
        swing(b + 0.5f * (float)(k % 4) - 0.12f, dirs[k % 4], 700.0f);
        run_until(b + 1.5f); SI.rec = false; sim_tick();
        run_until(b + 2.1f);
    }
    check(loops_used() == 4 && eng_loops[3].serial == 5 && eng_loops[0].serial == 1 && eng_loops[2].serial == 3, "five loops recorded: four kept, the fifth took the newest's place (serials %u %u %u %u)",
          eng_loops[0].serial, eng_loops[1].serial, eng_loops[2].serial, eng_loops[3].serial);
    INST.action(P_ERASE_ALL); run(0.2f);
    // a hold with no hits in it leaves nothing
    rec_hold(2.5f); run(2.5f);
    check(loops_used() == 0, "REC held through a bar with no hits: no loop");

    // ---- with a board on the link: the loop is on its grid; its tempo kept under a clash; TEMPO locked by its loop
    section("link");
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); link_set_harm(0, 0, 0, 0, 0, -1); }   // 120 BPM
    sim_peer_use(-1);
    run(3.0f);
    check(air_peers() == 1 && link_fpb() == 16000 && (int)pv[P_TEMPO] == 120, "a BAND at 120 BPM is heard (%d peers, %u frames a beat, TEMPO %d)", air_peers(), link_fpb(), (int)pv[P_TEMPO]);
    {
        float b = next_bar_after(sim_t + 0.5f);
        run_until(b - 0.2f); SI.rec = true;
        swing(b + 0.0f - 0.12f, D_LEFT, 700.0f); swing(b + 1.0f - 0.12f, D_RIGHT, 700.0f);
        run_until(b + 1.7f); SI.rec = false; sim_tick();
        run_until(b + 2.2f);
        uint32_t o; float f; link_origin(&o, &f);
        int64_t st = eng_loops[0].start;
        check(eng_loops[0].bars == 1 && st % BAR_TICKS == 0, "a bar held on the link: a 1-bar loop on the flock's bar (bars %d, start tick %lld)", eng_loops[0].bars, (long long)st);
        run_until(b + 4.1f);
        int no2 = onsets_level(b + 1.9f, b + 3.9f, 0.1f, at, 32);      // the sound itself: MIDI out is stamped on the 10 ms tick
        float worst = 0; for (int i = 0; i < no2; i++) { float d = ms_off_line(at[i], PPB); if (d > worst) worst = d; }
        check(no2 == 2 && worst < 8.0f, "the loop plays on the flock's grid (%d hits in a pass, worst %.1f ms off a beat)", no2, (double)worst);
    }
    sim_shot("linked");
    // the BAND moves the tempo under our loop: by the link's rules a loop keeps its tempo, and the clash is flagged
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 4); }   // 100 BPM
    sim_peer_use(-1);
    run(1.0f);
    check(link_fpb() == 16000 && link_tempo_clash(), "the BAND slows to 100 BPM under our loop: our beat stays and the clash shows (fpb %u, clash %d)", link_fpb(), (int)link_tempo_clash());
    INST.action(P_ERASE_ALL); run(0.5f);
    // a peer with a loop: TEMPO here is not ours to turn
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 4); link_set_flags(LINKF_LOOP); }
    sim_peer_use(-1);
    run(1.5f);
    param_set(P_TEMPO, 140); run(0.3f);
    check(link_fpb() == 19200 && (int)pv[P_TEMPO] == 100, "the BAND has a loop at 100: TEMPO turned to 140 goes back to 100 (fpb %u, TEMPO %d)", link_fpb(), (int)pv[P_TEMPO]);
    sim_peer_use(k); link_set_flags(0); sim_peer_use(-1);
    sim_peer_power(k, false);
    run(2.0f);
    // HANDS AUTO: another AIR, and this one takes a hand by its id
    int k2 = sim_peer_add("AIR", 1, -20.0, 3.0e9);
    run(2.0f);
    check(air_peers() == 1 && (air_hands() == HANDS_LEFT || air_hands() == HANDS_RIGHT) && eng_part_bar >= 0, "another AIR heard: HANDS AUTO makes this one %s (part bar %d)", air_hands() == HANDS_LEFT ? "LEFT" : "RIGHT", eng_part_bar);
    sim_peer_power(k2, false);
    run(2.0f);
    check(air_hands() == HANDS_ONE && eng_part_bar == -1, "it left: one hand again");

    // ---- sampling: tap REC, make a sound; the next pad clockwise is that sound
    section("sample");
    load("SYNTH KIT"); param_set(P_QUANT, 0); param_set(P_NEXT, 0); run(0.3f);
    sim_gif_begin("sample");
    rec_tap(); run(0.3f);
    check(eng_cap_state == CAP_ARMED && eng_cap_pad == PD_FRONT, "tap REC: the front pad listens (state %d, pad %d)", eng_cap_state, eng_cap_pad);
    run(0.5f); sim_shot("sample_listen");
    float tq2 = sim_t;
    burst_t = sim_t + 0.1f; burst_len = 0.35f; burst_amp = 0.4f;
    run(0.3f); sim_shot("sample_rec");
    run(0.6f);
    float quiet = m_rms(tq2, tq2 + 0.6f);
    check(quiet < 0.002f, "while it listens the drums are quiet (rms %.4f)", (double)quiet);
    run(0.5f);
    int plen = eng_pad_len[PD_FRONT];
    check(eng_cap_state == CAP_IDLE && plen > (int)(0.3f * AUDIO_SR) && plen < (int)(0.7f * AUDIO_SR), "a 0.35 s sound: the pad holds %.2f s, cut when it died", (double)plen / AUDIO_SR);
    check((int)pv[P_FRONT] == V_SAMPLE && (int)pv[P_NEXT] == 1, "the front pad is now SAMPLE and the next is the right pad");
    sim_shot("sampled");
    // the sample plays on a forward stroke, at the drums' level
    float tr = sim_t + 0.2f; swing(tr, D_FRONT, 600.0f); run(0.6f);
    float pk = m_peak(tr, tr + 0.5f);
    check(pk > 0.1f && pk < 0.9f, "a forward stroke plays the sample (peak %.2f)", (double)pk);
    float f220 = m_goertzel(tr + 0.08f, tr + 0.2f, 220.0f), f330 = m_goertzel(tr + 0.08f, tr + 0.2f, 330.0f);
    check(f220 > 3.0f * f330, "... and it is the sound that was heard (220 Hz, %.1f x the power at 330)", (double)(f220 / (f330 + 1e-9f)));
    sim_gif_end();
    // the next tap goes clockwise: the right pad
    rec_tap(); run(0.3f);
    check(eng_cap_state == CAP_ARMED && eng_cap_pad == PD_RIGHT, "the next tap: the right pad listens (pad %d)", eng_cap_pad);
    sim_tap(); run(0.2f);
    check(eng_cap_state == CAP_IDLE && front_menu_level() == 0 && (int)pv[P_RIGHT] == V_SNARE, "a tap on the knob: never mind, the right pad is still the snare");
    // tuned up, it plays faster: higher
    param_set(P_TUNE, 12); run(0.2f);
    float tr2 = sim_t + 0.1f; swing(tr2, D_FRONT, 600.0f); run(0.5f);
    float f440 = m_goertzel(tr2 + 0.05f, tr2 + 0.12f, 440.0f), f220b = m_goertzel(tr2 + 0.05f, tr2 + 0.12f, 220.0f);
    check(f440 > f220b, "TUNE +12: the sample plays an octave up (440 Hz over 220: %.1f x)", (double)(f440 / (f220b + 1e-9f)));
    param_set(P_TUNE, 0);
    // FORGET ALL
    INST.action(P_FORGET); run(0.2f);
    check(eng_pad_len[PD_FRONT] == 0 && (int)pv[P_FRONT] == V_PART3 && (int)pv[P_NEXT] == 0, "FORGET ALL: the pad is empty, the printed hat is back, the next pad is the front again");

    // ---- MIDI in: a kick note plays the kick pad
    section("midi");
    int m0 = midi_n;
    front_midi(0x90, 36, 100); run(0.2f);
    int got1[4]; int ng1 = notes_between(sim_t - 0.25f, sim_t, got1, 4);
    check(ng1 == 1 && got1[0] == 36 && midi_n > m0, "MIDI note 36 in plays the kick (and sends it out on channel 10)");

    // ---- the stick as ROLL+REPEAT: up rolls the last drum at the grid's sixteenths
    section("stick");
    param_set(P_STICK, STICK_ROLL); param_set(P_TEMPO, 120); run(0.3f);
    swing(sim_t + 0.1f, D_RIGHT, 600.0f);
    run(0.4f);
    SI.jy = 0.75f; float trl = sim_t; run(1.0f); sim_shot("roll");
    int rolls[64]; int nr = notes_between(trl + 0.05f, sim_t, rolls, 64), snares = 0;
    for (int i = 0; i < nr; i++) snares += rolls[i] == 38;
    check(nr >= 6 && nr <= 9 && snares == nr, "stick up: a roll of the last drum (snare), sixteenths at 120 (%d in 0.95 s)", nr);
    SI.jy = 0; run(0.5f);
    param_set(P_STICK, STICK_LOOPS);

    // ---- the printed kit: the four parts of the DRUMS print are a kick, a snare, a hat and an open hat
    section("parts");
    load("AIR KIT");
    for (int i = 0; i < 400 && (kit_rendering() || strcmp(kit_break()->name, "DRUMS")); i++) run(0.05f);
    check(!strcmp(kit_break()->name, "DRUMS") && eng_part_kind(0) == KS_KICK && eng_part_kind(1) == KS_SNARE && eng_part_kind(2) == KS_HAT,
          "AIR KIT: the DRUMS print is there (%s), its parts a kick, a snare, a hat (%d %d %d)", kit_break()->name, eng_part_kind(0), eng_part_kind(1), eng_part_kind(2));
    t0 = sim_t + 0.2f;
    for (int i = 0; i < 4; i++) swing(t0 + 0.5f * (float)i, dirs[i], 700.0f);
    run(2.3f);
    n = notes_between(t0 - 0.05f, sim_t, got, 16);
    float pks[4]; for (int i = 0; i < 4; i++) pks[i] = m_peak(t0 + 0.5f * (float)i, t0 + 0.5f * (float)i + 0.4f);
    check(n == 4 && got[0] == 42 && got[1] == 36 && got[2] == 38 && pks[0] > 0.05f && pks[1] > 0.1f && pks[2] > 0.1f && pks[3] > 0.05f,
          "the four pads play the printed hat, kick, snare, open hat (notes %d %d %d %d; peaks %.2f %.2f %.2f %.2f)", got[0], got[1], got[2], got[3], (double)pks[0], (double)pks[1], (double)pks[2], (double)pks[3]);
    sim_shot("drums_print");
    // the Amen in four: each pad a quarter, stretched to the grid's tempo
    load("AMEN");
    sim_gif_begin("amen");
    for (int i = 0; i < 600 && (kit_rendering() || strcmp(kit_break()->name, "AMEN")); i++) run(0.05f);
    check(!strcmp(kit_break()->name, "AMEN") && kit_break()->steps == 32, "AMEN: printed (%s, %d steps)", kit_break()->name, kit_break()->steps);
    param_set(P_TEMPO, 136); run(0.2f);
    t0 = sim_t + 0.2f;
    for (int i = 0; i < 4; i++) swing(t0 + 0.5f * (float)i, dirs[i], 700.0f);
    run(2.3f); sim_shot("amen");
    sim_gif_end();
    int loud = 0; for (int i = 0; i < 4; i++) loud += m_peak(t0 + 0.5f * (float)i, t0 + 0.5f * (float)i + 0.45f) > 0.1f;
    check(loud == 4 && eng_part_rate > 0.99f && eng_part_rate < 1.01f, "the four quarters of the Amen play (%d loud) at its own speed at TEMPO 136 (rate %.2f)", loud, (double)eng_part_rate);
    param_set(P_TEMPO, 68); run(0.2f);
    check(eng_part_rate > 0.49f && eng_part_rate < 0.51f, "TEMPO 68: the parts play at half speed (rate %.2f)", (double)eng_part_rate);
    param_set(P_STRETCH, 0); run(0.2f);
    check(eng_part_rate == 1.0f, "STRETCH off: as printed");
    param_set(P_STRETCH, 1); param_set(P_TEMPO, 136); run(0.2f);
    {
        param_set(P_HANDS, HANDS_LEFT); run(0.2f);
        float tl = sim_t + 0.1f; swing(tl, D_FRONT, 700.0f); run(0.8f);
        param_set(P_HANDS, HANDS_RIGHT); run(0.2f);
        float tr3 = sim_t + 0.1f; swing(tr3, D_FRONT, 700.0f); run(0.8f);
        int a = (int)(tl * AUDIO_SR) + 1600, L = AUDIO_SR / 3, bidx = (int)(tr3 * AUDIO_SR) + 1600;
        double c = 0, e0 = 0, e1 = 0;
        for (int i = 0; i < L; i++) { float x0 = sim_out[a + i], x1 = sim_out[bidx + i]; c += x0 * x1; e0 += x0 * x0; e1 += x1 * x1; }
        float r = (float)(c / sqrt(e0 * e1 + 1e-12));
        check(eng_part_bar == 1 && e0 > 1.0 && e1 > 1.0 && r < 0.9f, "HANDS LEFT and RIGHT: part 1 is bar 1 for one hand and bar 2 for the other (correlation %.2f)", (double)r);
        param_set(P_HANDS, HANDS_AUTO); run(0.2f);
    }

    // ---- effects: crush and grain over the printed kit, nothing breaks and the grain fills the gaps
    section("effects");
    load("AIR KIT"); run(0.3f);
    param_set(P_CRUSH, 100); run(0.1f);
    t0 = sim_t + 0.1f; swing(t0, D_LEFT, 700.0f); run(0.6f);
    float pcr = m_peak(t0, t0 + 0.4f);
    param_set(P_CRUSH, 0);
    check(pcr > 0.1f && pcr < 1.0f, "CRUSH 100: the kick still plays, crushed (peak %.2f)", (double)pcr);
    param_set(P_GRAIN, 0); run(0.5f);
    t0 = sim_t + 0.1f; swing(t0, D_RIGHT, 700.0f); run(1.6f);
    float dry_tail = m_rms(t0 + 0.9f, t0 + 1.4f);
    param_set(P_GRAIN, 100); param_set(P_GSIZE, 60); run(0.3f);
    t0 = sim_t + 0.1f; swing(t0, D_RIGHT, 700.0f); run(1.6f);
    float grain_tail = m_rms(t0 + 0.9f, t0 + 1.4f);
    sim_shot("grain");
    param_set(P_GRAIN, 0); run(1.0f);
    check(grain_tail > 4.0f * dry_tail + 0.002f, "GRAIN 100: pieces of the snare keep coming after it (rms %.4f against %.4f dry)", (double)grain_tail, (double)dry_tail);

    // ---- every sound makes sound, none clips by itself
    section("sounds");
    param_set(P_QUANT, 0);
    int quietest = 0; float qv = 1, loudest = 0;
    for (int p = 0; p < INST.npresets; p++) {
        front_midi(0xC0, (uint8_t)p, 0); run(0.3f);
        for (int i = 0; i < 600 && kit_rendering(); i++) run(0.05f);
        bool samples = pv[P_FRONT] < 1.0f && pv[P_LEFT] < 1.0f;
        float tp0 = sim_t + 0.1f;
        for (int i = 0; i < 4; i++) swing(tp0 + 0.3f * (float)i, dirs[i], 700.0f);
        run(1.6f);
        float pkp = m_peak(tp0, sim_t), rms = m_rms(tp0, tp0 + 1.2f);
        note("%-14s peak %.2f rms %.3f%s", INST.presets[p].name, (double)pkp, (double)rms, samples ? " (sample pads, empty)" : "");
        if (!samples && rms < qv) { qv = rms; quietest = p; }
        if (pkp > loudest) loudest = pkp;
        run(1.0f);
    }
    check(qv > 0.01f && loudest < 1.0f, "every kit sounds (quietest %s, rms %.3f) and none clips (loudest peak %.2f)", INST.presets[quietest].name, (double)qv, (double)loudest);
    load("AIR KIT"); param_set(P_QUANT, 2);

    // ---- a few more screens
    section("screens");
    sim_tilt(0, 25); run(0.5f); sim_shot("tilt_tune"); sim_tilt(0, 0); run(0.3f);

    // ---- time per block
    {
        float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
        for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.001f * sim_noise();
        swing(sim_t + 0.01f, D_LEFT, 700.0f); swing(sim_t + 0.02f, D_RIGHT, 700.0f); swing(sim_t + 0.03f, D_FRONT, 700.0f); run(0.08f);
        param_set(P_ECHO, 40); param_set(P_ROOM, 50);
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        for (int i = 0; i < 2000; i++) INST.audio(in, out, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double us = ((double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec)) / 2000.0 / 1000.0;
        metric("host_us_per_block", us);
        note("engine: %.1f us per 64-frame block on the host with three voices, echo and room", us);
        param_set(P_ECHO, 0); param_set(P_ROOM, 18);
    }
}
