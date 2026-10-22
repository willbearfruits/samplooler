// AIR on the host: strokes as gyro profiles (a half sine of rate along the stroke's axis, then the hand coming
// back the other way, slower), one per pad and from both sideways axes; the return swing that must not count;
// velocity; a slow turn that is not a hit; the loop alone (it sets the flock's tempo) and with a board on the
// link (whole beats, on the beat, stretched when the tempo moves); sampling a pad from the mic and playing it;
// FORGET ALL; MIDI in; the stick's roll; every sound; screens and GIFs.
#include "sim.h"
#include "inst_air.h"
#include "inst_kit.h"
#include "link.h"
#include <math.h>
#include <string.h>
#include <time.h>

int air_gest_state(void);
int air_peers(void);

const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", NULL };   // the radio's switches, not the sound

// ---------------------------------------------------------------- the strokes
typedef struct { float t; vec3_t dir; float peak, dur, ret; } stroke_t;
static stroke_t script[256];
static int nscript, first;
static float excite_period;                 // inst_excite: a stroke this often, by itself

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

// ---------------------------------------------------------------- the mic: room noise, a burst to sample, the gyro as a side effect
static float burst_t = -1, burst_len = 0.3f, burst_amp;
static float mic(float t, void *u)
{
    (void)u;
    SI.gyro = gyro_at(t);                    // the sim has no gyro hook of its own: the mic callback runs every tick before the controls
    float x = 0.0008f * sim_noise();
    if (burst_t >= 0 && t >= burst_t && t < burst_t + burst_len) {
        float dt = t - burst_t;
        x += burst_amp * sim_noise() * expf(-dt / (burst_len * 0.3f)) + 0.5f * burst_amp * sinf(6.2831853f * 220.0f * dt) * expf(-dt / (burst_len * 0.5f));
    }
    return x;
}
static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static void rec_hold(float s) { SI.rec = true; run(s); SI.rec = false; sim_tick(); }

static int notes_between(float t0, float t1, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < midi_n && n < max; i++)
        if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0 && midi_log[i].t >= t0 && midi_log[i].t < t1) out[n++] = midi_log[i].d1;
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

void inst_excite(void)
{
    nscript = 0; first = 0; burst_t = -1;
    excite_period = 0.2f;
    SI.mic = mic; SI.jx = SI.jy = 0;
    sim_tilt(0, 0);
}

// ---------------------------------------------------------------- the tests
void inst_tests(void)
{
    excite_period = 0; nscript = 0; first = 0; burst_t = -1;
    SI.mic = mic; SI.jx = SI.jy = 0;
    sim_tilt(0, 0);
    load("SYNTH KIT");                                                 // KNOCK's voices: one GM note per pad
    param_set(P_GRID, 0); param_set(P_QUANT, 0);                       // hits as they come; the grid has its own section
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

    // ---- quantize: alone, hits wait for the next sixteenth of TEMPO
    section("quantize");
    param_set(P_QUANT, 2); param_set(P_TEMPO, 120); run(0.3f);
    {
        int on_grid = 0, nq = 0; float worst = 0;
        for (int i = 0; i < 4; i++) {
            float tq = sim_t + 0.137f + 0.021f * (float)i;             // off the grid on purpose
            swing(tq, D_LEFT, 700.0f);
            run(0.6f);
            float atq[4]; int nn = onsets_level(tq, tq + 0.5f, 0.1f, atq, 4);
            if (nn) { nq++; int fr = (int)(atq[0] * AUDIO_SR + 0.5f) % 4000; float off = (float)(fr > 2000 ? fr - 4000 : fr) / AUDIO_SR * 1000.0f; if (fabsf(off) > worst) worst = fabsf(off); if (fabsf(off) < 3.0f) on_grid++; }
        }
        check(nq == 4 && on_grid == 4, "QUANTIZE 1/16 at 120 BPM: four kicks swung off the grid all land on a sixteenth (%d of %d, worst %.1f ms off)", on_grid, nq, (double)worst);
    }
    param_set(P_QUANT, 0); run(0.3f);

    // ---- the printed kit: the four parts of the DRUMS print are a kick, a snare, a hat and an open hat
    section("parts");
    load("AIR KIT");
    for (int k = 0; k < 400 && (kit_rendering() || strcmp(kit_break()->name, "DRUMS")); k++) run(0.05f);
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
    // the Amen in four: each pad a quarter, stretched to the grid's tempo, a bar each with two hands
    load("AMEN");
    sim_gif_begin("amen");
    for (int k = 0; k < 600 && (kit_rendering() || strcmp(kit_break()->name, "AMEN")); k++) run(0.05f);
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
    check(eng_part_rate == 1.0f, "FOLLOW TEMPO off: as printed");
    param_set(P_STRETCH, 1); param_set(P_TEMPO, 136); run(0.2f);
    {
        param_set(P_HANDS, HANDS_LEFT); run(0.2f);
        float ta = sim_t + 0.1f; swing(ta, D_FRONT, 700.0f); run(0.8f);
        param_set(P_HANDS, HANDS_RIGHT); run(0.2f);
        float tb = sim_t + 0.1f; swing(tb, D_FRONT, 700.0f); run(0.8f);
        int a = (int)(ta * AUDIO_SR) + 1600, L = AUDIO_SR / 3, bidx = (int)(tb * AUDIO_SR) + 1600;
        double c = 0, e0 = 0, e1 = 0;
        for (int i = 0; i < L; i++) { float x0 = sim_out[a + i], x1 = sim_out[bidx + i]; c += x0 * x1; e0 += x0 * x0; e1 += x1 * x1; }
        float r = (float)(c / sqrt(e0 * e1 + 1e-12));
        check(eng_part_bar == 1 && e0 > 1.0 && e1 > 1.0 && r < 0.9f, "HANDS LEFT and RIGHT: part 1 is bar 1 for one hand and bar 2 for the other (correlation %.2f)", (double)r);
        param_set(P_HANDS, HANDS_ONE); run(0.2f);
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
    load("SYNTH KIT"); param_set(P_GRID, 0); param_set(P_QUANT, 0); run(0.3f);

    // ---- a loop alone: REC down, kick, snare, kick, REC up after 1.2 s; it sets the flock's tempo
    section("loop");
    param_set(P_GRID, 0);
    float tl = sim_t + 0.1f;
    swing(tl + 0.2f, D_LEFT, 900.0f); swing(tl + 0.6f, D_LEFT, 900.0f); swing(tl + 0.9f, D_LEFT, 900.0f);   // three kicks: their starts are the same each pass
    run(0.1f);
    sim_gif_begin("loop");
    SI.rec = true; run(0.5f); sim_shot("recording"); run(0.7f); SI.rec = false; sim_tick();
    float len = (float)eng_loop_len / AUDIO_SR;
    check(fabsf(len - 1.2f) < 0.02f && eng_nhits == 3, "REC held 1.2 s: a 1.2 s loop with 3 hits (%.3f s, %d hits)", (double)len, eng_nhits);
    const link_time_t *lt = link_time();
    check(lt->beats == eng_beats() && lt->fpb * lt->beats == eng_loop_len, "alone, the loop sets the flock's tempo: %d beats of %u frames (%.0f BPM)", lt->beats, lt->fpb, (double)eng_bpm());
    run(3.7f); sim_shot("looping");
    sim_gif_end();
    float at[32]; int no = out_onsets(tl + 1.3f, tl + 4.8f, at, 32);
    int lp[32]; int nlp = notes_between(tl + 1.3f, tl + 4.8f, lp, 32);
    check(nlp == 9 && no >= 6, "the loop repeats its 3 hits (%d notes in 3 passes, %d onsets heard)", nlp, no);
    float jit = 0, atl[32]; int nol = onsets_level(tl + 1.3f, tl + 4.8f, 0.12f, atl, 32);
    int matched = 0;
    for (int i = 0; i < nol; i++) for (int j = i + 1; j < nol; j++) { float d = fabsf(atl[j] - atl[i] - len); if (d < 0.05f) { matched++; if (d > jit) jit = d; } }
    check(matched >= 6 && jit < 0.003f, "each pass plays the same (%d kicks over the passes, %d one pass apart, at most %.1f ms off)", nol, matched, (double)(jit * 1000.0f));
    // add, stop, play, undo, clear
    swing(sim_t + 0.3f, D_FRONT, 600.0f);
    rec_hold(0.6f);
    check(eng_nhits == 4, "held REC again over the loop: a 4th hit added (%d)", eng_nhits);
    run(1.3f);
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
    check(eng_loop_len == 0 && eng_nhits == 0 && !(link_flags_any() & LINKF_LOOP), "CLEAR LOOP erases it and tells the flock");

    // ---- with a board on the link: the loop is whole beats of its tempo and starts on its beat
    section("link");
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); link_set_harm(0, 0, 0, 0, 0, -1); }   // 120 BPM
    sim_peer_use(-1);
    run(3.0f);
    check(air_peers() == 1 && link_fpb() == 16000, "a BAND at 120 BPM is heard (%d peers, %u frames a beat)", air_peers(), link_fpb());
    float tg = sim_t + 0.1f;
    swing(tg + 0.1f, D_LEFT, 600.0f); swing(tg + 0.6f, D_RIGHT, 600.0f); swing(tg + 1.1f, D_LEFT, 600.0f); swing(tg + 1.6f, D_RIGHT, 600.0f);
    run(0.1f);
    SI.rec = true; run(1.9f); SI.rec = false; sim_tick();              // held 1.9 s: 4 beats of the flock
    {
        uint32_t o; float f; link_origin(&o, &f);
        uint32_t on_beat = (eng_loop_start - o) % link_fpb();
        check(eng_loop_len == 64000 && eng_beats() == 4 && on_beat == 0, "held 1.9 s on the link: a 4-beat loop of 64000 frames starting on a beat (%u frames, %d beats, %u off the beat)", eng_loop_len, eng_beats(), on_beat);
    }
    run(2.1f);
    float at2[32]; int no2 = out_onsets(sim_t - 2.0f, sim_t, at2, 32);
    check(no2 >= 3, "the loop plays on the flock's grid (%d onsets in a pass)", no2);
    // the BAND moves the tempo under our loop: by the link's rules a loop keeps its tempo, and the clash is flagged
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 4); }   // 100 BPM
    sim_peer_use(-1);
    run(1.0f);
    check(eng_loop_len == 64000 && link_tempo_clash(), "the BAND slows to 100 BPM under our loop: the loop keeps its tempo and the clash shows (loop %u, clash %d)", eng_loop_len, (int)link_tempo_clash());
    sim_shot("linked");
    INST.action(P_CLEAR); run(0.2f);
    sim_peer_power(k, false);
    run(2.0f);

    // ---- sampling a pad: hold the knob, hit right, make a sound; the snare pad is now the sound
    section("sample");
    load("SYNTH KIT"); param_set(P_GRID, 0); param_set(P_QUANT, 0);
    sim_gif_begin("sample");
    sim_hold_knob(); run(0.3f);
    sim_shot("sample_pick");
    float tp = sim_t + 0.1f; swing(tp, D_RIGHT, 600.0f); run(0.4f);
    int pick_notes[4]; int npick = notes_between(tp - 0.05f, sim_t, pick_notes, 4);
    check(eng_cap_state == CAP_ARMED && eng_cap_pad == PD_RIGHT, "hold the knob, hit right: the right pad listens (state %d, pad %d)", eng_cap_state, eng_cap_pad);
    run(0.5f); sim_shot("sample_listen");
    float tq2 = sim_t;
    burst_t = sim_t + 0.1f; burst_len = 0.35f; burst_amp = 0.4f;
    run(0.3f); sim_shot("sample_rec");
    run(0.6f);
    float quiet = m_rms(tq2, tq2 + 0.6f);
    check(quiet < 0.002f, "while it listens the drums are quiet (rms %.4f)", (double)quiet);
    run(0.5f);
    int plen = eng_pad_len[PD_RIGHT];
    check(eng_cap_state == CAP_IDLE && plen > (int)(0.3f * AUDIO_SR) && plen < (int)(0.7f * AUDIO_SR), "a 0.35 s sound: the pad holds %.2f s, cut when it died", (double)plen / AUDIO_SR);
    check((int)pv[P_RIGHT] == V_SAMPLE, "the right pad is now SAMPLE");
    check(npick == 0, "the stroke that picked the pad played nothing (%d notes)", npick);
    sim_shot("sampled");
    // the sample plays on a right stroke, at the drums' level
    float tr = sim_t + 0.2f; swing(tr, D_RIGHT, 600.0f); run(0.6f);
    float pk = m_peak(tr, tr + 0.5f);
    check(pk > 0.1f && pk < 0.9f, "a right stroke plays the sample (peak %.2f)", (double)pk);
    float f220 = m_goertzel(tr + 0.08f, tr + 0.2f, 220.0f), f330 = m_goertzel(tr + 0.08f, tr + 0.2f, 330.0f);
    check(f220 > 3.0f * f330, "... and it is the sound that was heard (220 Hz, %.1f x the power at 330)", (double)(f220 / (f330 + 1e-9f)));
    sim_gif_end();
    // tuned up, it plays faster: higher
    param_set(P_TUNE, 12); run(0.2f);
    float tr2 = sim_t + 0.1f; swing(tr2, D_RIGHT, 600.0f); run(0.5f);
    float f440 = m_goertzel(tr2 + 0.05f, tr2 + 0.12f, 440.0f), f220b = m_goertzel(tr2 + 0.05f, tr2 + 0.12f, 220.0f);
    check(f440 > f220b, "TUNE +12: the sample plays an octave up (440 Hz over 220: %.1f x)", (double)(f440 / (f220b + 1e-9f)));
    param_set(P_TUNE, 0);
    // never mind: a tap during the pick
    sim_hold_knob(); run(0.2f); sim_tap(); run(0.2f);
    check(eng_cap_state == CAP_IDLE && front_menu_level() == 0, "hold the knob then tap it: no sample, back to play");
    // FORGET ALL
    INST.action(P_FORGET); run(0.2f);
    check(eng_pad_len[PD_RIGHT] == 0 && (int)pv[P_RIGHT] == V_PART2, "FORGET ALL: the pad is empty and the printed snare is back");

    // ---- MIDI in: a kick note plays the kick pad
    section("midi");
    int m0 = midi_n;
    front_midi(0x90, 36, 100); run(0.2f);
    int got1[4]; int ng1 = notes_between(sim_t - 0.25f, sim_t, got1, 4);
    check(ng1 == 1 && got1[0] == 36 && midi_n > m0, "MIDI note 36 in plays the kick (and sends it out on channel 10)");

    // ---- the stick: up rolls the last drum
    section("stick");
    swing(sim_t + 0.1f, D_RIGHT, 600.0f);
    run(0.4f);
    SI.jy = 0.75f; float trl = sim_t; run(1.0f); sim_shot("roll");
    int rolls[64]; int nr = notes_between(trl + 0.05f, sim_t, rolls, 64), snares = 0;
    for (int i = 0; i < nr; i++) snares += rolls[i] == 38;
    check(nr >= 10 && nr <= 14 && snares == nr, "stick up: a roll of the last drum (snare), 12 a second without a loop (%d in 0.95 s)", nr);
    SI.jy = 0; run(0.5f);

    // ---- every sound makes sound, none clips by itself
    section("sounds");
    int quietest = 0; float qv = 1, loudest = 0;
    for (int p = 0; p < INST.npresets; p++) {
        front_midi(0xC0, (uint8_t)p, 0); run(0.3f);
        for (int k = 0; k < 600 && kit_rendering(); k++) run(0.05f);
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
    load("AIR KIT"); param_set(P_QUANT, 0);

    // ---- a few more screens
    section("screens");
    SI.jx = -0.8f; run(0.3f); sim_shot("filter"); SI.jx = 0; run(0.3f);
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
