// MANTRA on rev E (the Vessel), run by test_inst.c after everything else. No rev E board exists: this is how the left
// knob and the hand are checked.
//   on the board with the joystick the new settings, a left knob's pulses and a sensor's reading change nothing, the
//   stick does what it did, and the two new hints never show;
//   rev E: a hand over the sensor shuts the mouth to the stick's hum, more the lower it is (formants as test_voice.c
//   measures the vowels, the level, what is above 800 Hz), and opens again when it leaves; the amps dive with it;
//   the left knob moves every voice and the amps a semitone a detent, an octave either way, stops there, stays, glides
//   as a sung note does, shows on the rail; HANDS > HAND: VIBRATO gives the hand the stick's vibrato instead; no click
//   from any of it; SYSTEM > HAND and LEFT KNOB OFF; the two hints; the cost with the hand down
#include "sim.h"
#include "mantra.h"
#include "oled.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern mv_ctl_t sc_last_ctl;                                      // what the voices were handed last (inst_score.c)
extern float   *dr_tap; extern int dr_tap_cap, dr_tap_n;          // the amps alone (inst_drone.c)
float mv_test_hz(int v);
void  tv_formants(const float *x, int n, double *f12);            // test_voice.c
void  front_midi(uint8_t st, uint8_t d1, uint8_t d2);
int   mn_cover(void);
enum { S_OM, S_MONKS, S_OMMANI, S_KHOOMEI, S_SYGYT, S_KARGYRAA, S_DAISY, S_DAISY8, S_VOWELS, S_HUM, S_SUNN, S_DOOM, S_ORGAN };

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }
static float dbf(float a, float b) { return 20.0f * log10f((a + 1e-9f) / (b + 1e-9f)); }
static float cents(float f, float ref) { return f > 0 && ref > 0 ? 1200.0f * log2f(f / ref) : 9999.0f; }
static int at(float t) { int i = (int)(t * AUDIO_SR); return i < 0 ? 0 : i > sim_n ? sim_n : i; }

// what is above the first formant of a hum: the level above 800 Hz (two second-order high-passes in a row, Q 0.54
// and 1.31: a fourth-order Butterworth), the first 50 ms left out for the filter to settle
static float highs(float t0, float t1)
{
    int a = at(t0 - 0.05f), b = at(t1), skip = at(t0) - a;
    double w = 2 * M_PI * 800.0 / AUDIO_SR, cw = cos(w), sw = sin(w), z[2][4] = { { 0 } }, e = 0;
    static const double Q[2] = { 0.5412, 1.3066 };
    for (int i = a; i < b; i++) {
        double x = sim_out[i];
        for (int k = 0; k < 2; k++) {
            double al = sw / (2 * Q[k]), a0 = 1 + al, b0 = (1 + cw) / 2 / a0, b1 = -(1 + cw) / a0, a1 = -2 * cw / a0, a2 = (1 - al) / a0;
            double y = b0 * x + b1 * z[k][0] + b0 * z[k][1] - a1 * z[k][2] - a2 * z[k][3];
            z[k][1] = z[k][0]; z[k][0] = x; z[k][3] = z[k][2]; z[k][2] = y; x = y;
        }
        if (i - a >= skip) e += x * x;
    }
    return b - a > skip ? (float)sqrt(e / (b - a - skip)) : 0.0f;
}
static void fmts(float t0, float t1, double *f12) { int a = at(t0), b = at(t1); f12[0] = f12[1] = 0; if (b - a > 4000) tv_formants(sim_out + a, b - a, f12); }
// the worst click between two times: m_click (the first difference's jump against the signal's own) every 2 ms
static float clicks(float t0, float t1) { float w = 0; for (float t = t0; t < t1; t += 0.002f) { float r = m_click(t, NULL, NULL); if (r > w) w = r; } return w; }

// the left knob's rail on the frame drawn last: the row its bead is on (0: no bead, -1: more than one)
static int bead_row(void)
{
    int row = 0;
    for (int y = 26; y <= 62; y++)
        if (oled_get(1, y) && oled_get(5, y) && oled_get(2, y - 1) && oled_get(4, y - 1) && oled_get(2, y + 1) && oled_get(4, y + 1)) row = row ? -1 : y;
    return row;
}
// ... and whether the rail is there at all: its top tick, the dots under it, nothing beside them
static int rail_there(void) { return oled_get(2, 28) && oled_get(3, 28) && oled_get(4, 28) && oled_get(3, 30) && oled_get(3, 32) && !oled_get(3, 31) && !oled_get(6, 30); }
// the hand lying over his mouth in VOWELS (the words' strip is up: his head's top is at row 23, his mouth at 38): its
// thumb above it, its lower edge across his chin: 11 pixels that are never all lit without it
static int hand_there(void) { int n = 0; for (int x = 65; x <= 67; x++) n += oled_get(x, 34) + oled_get(x, 35); for (int x = 62; x <= 66; x++) n += oled_get(x, 42); return n; }

// VOWELS, dry (no hall, no echo: what is measured is the mouth), flat on the table: one voice holding AH on the key's
// note. steady: no vibrato, drift or roughness either, for the pitch to the cent
static void vowels(bool steady)
{
    front_midi(0xC0, S_VOWELS, 0);
    set("frev", 0); set("fech", 0); set("hbld", 0); set("mdrn", 0); set("mvoi", 85);
    if (steady) { set("vvib", 0); set("vdri", 0); set("vrou", 0); set("vbre", 20); }
    sim_tilt(0, 0); SI.gyro = (vec3_t){ 0, 0, 0 }; SI.mic = NULL;
    sim_run(1.5f);
    for (int g = 0; g < 400 && mn_cover() > 0; g++) sim_tick();     // the sound's card goes
    sim_run(0.2f);
}
static float pitch_now(void) { return m_pitch_at(sim_t - 0.4f, sim_t, 80.0f, 520.0f); }

// ---------------------------------------------------------------- the amps alone (as test_drone.c listens to them)
static float *tap;
static float amps_pitch(void)                                      // the bass alone, a second of it
{
    int n = (int)(1.2f * AUDIO_SR) + 8192;
    tap = realloc(tap, (size_t)n * sizeof(float));
    dr_tap = tap; dr_tap_cap = n; dr_tap_n = 0;
    sim_run(1.0f);
    dr_tap = NULL;
    int a = (int)(0.1f * AUDIO_SR), b = dr_tap_n;
    return b > a ? m_pitch(tap + a, b - a, 12.0f, 200.0f) : 0.0f;
}
static void amps_plain(void)                                        // test_drone.c's plain(), the bass alone
{
    set("dorg", 0); set("dfbk", 0); set("dhum", 0); set("dmod", 0); set("dsub", 0); set("dvoc", 1); set("dscl", 0);
    set("dkno", 0); set("dpnd", 0); set("dchg", 0); set("dswl", 0.5f); set("dgan", 70); set("dtun", 3);
    set("sprg", 0); set("schd", 0);                                 // one chord held (test_inst.c's BAND left a progression)
    set("mvoi", 0); set("mdrn", 85);
    sim_run(2.5f);
}

// ---------------------------------------------------------------- the board with the joystick: nothing changed
static void joystick_board(void)
{
    section("mantra: rev E's controls on the board with the joystick");
    const ctl_t *c = front_ctl();
    sim_turn(1); sim_turn(-1);                                    // a touch wakes the screen
    vowels(true);
    float f0 = pitch_now(), r0 = m_rms(sim_t - 1.0f, sim_t); double F0[2]; fmts(sim_t - 1.0f, sim_t, F0);
    // NOTE and HAND at their other ends from the menu, a left knob's pulses and a sensor's reading on the inputs
    float worst = 0; int rail = 0, frames = 0;
    for (int mode = 1; mode >= 0; mode--) {
        set("hnot", mode ? 7 : -12); set("hhnd", (float)mode);
        SI.hand_mm = 40;
        for (int i = 0; i < 150; i++) {
            SI.lk = i % 10 == 0 ? (i % 20 ? -2 : 3) : 0; ui_awake(); sim_tick();
            float m = fabsf(hands.note_s) + fabsf((float)hands.note) + hands.hand + fabsf(sc_last_ctl.bend) + sc_last_ctl.hum + sc_last_ctl.vib_depth + (float)hands.note_show;
            if (m > worst) worst = m;
            if (i % 4 == 3) { rail += rail_there() || bead_row(); frames++; }
        }
        if (mode) sim_shot("revc_note_7_hand_vibrato");
    }
    float f1 = pitch_now(), r1 = m_rms(sim_t - 1.0f, sim_t); double F1[2]; fmts(sim_t - 1.0f, sim_t, F1);
    check(!c->has_lknob && !c->has_hand && worst == 0.0f && rail == 0 && fabsf(cents(f1, f0)) < 3.0f && fabsf(dbf(r1, r0)) < 0.5f && fabs(F1[0] / F0[0] - 1) < 0.03 && fabs(F1[1] / F0[1] - 1) < 0.03,
          "the board with the joystick, HANDS > NOTE at +7 then -12, HAND at VIBRATO then HUM, pulses on the left knob's pins, a reading of 40 mm: no note, no hand, no hum, no vibrato reach the voices (sum %.3f), no rail on the screen (%d of %d frames); AH sings on at %.1f Hz (%+.1f cents, %+.2f dB, F1 %.0f -> %.0f, F2 %.0f -> %.0f)",
          (double)worst, rail, frames, (double)f0, (double)cents(f1, f0), (double)dbf(r1, r0), F0[0], F1[0], F0[1], F1[1]);
    SI.hand_mm = 0;
    // the stick does what it did (NOTE still at -12 in the menu)
    SI.jx = 1.0f; sim_run(0.8f); float bend = sc_last_ctl.bend, fb = pitch_now();
    SI.jx = 0.0f; SI.jy = -1.0f; sim_run(0.8f); float hum = sc_last_ctl.hum;
    SI.jy = 1.0f; sim_run(0.8f); float vib = sc_last_ctl.vib_depth;
    SI.jy = 0.0f; sim_run(0.5f);
    check(bend == 2.0f && fabsf(cents(fb, f0) - 200.0f) < 6.0f && hum == 1.0f && vib == 0.6f,
          "its stick as before: right bends two semitones (%.2f, %+.0f cents), down hums (%.2f), up is the vibrato (%.2f st)", (double)bend, (double)cents(fb, f0), (double)hum, (double)vib);
    // the hints for the left knob and the hand never show here: everything else learned, 15 s idle
    uint32_t keep = meta.learned; float keep_h = pv[CP(CP_HINTS)];
    pv[CP(CP_HINTS)] = 1; meta.learned = ~((1u << LEARN_LKNOB) | (1u << LEARN_HAND));
    int shown = 0;
    for (int i = 0; i < 1500; i++) { sim_tick(); if (mn_cover() > 0) shown++; }
    check(shown == 0 && !(meta.learned & (1u << LEARN_LKNOB)) && !(meta.learned & (1u << LEARN_HAND)), "the hints for the left knob and the hand never show on this board (15 s idle, nothing else to learn: %d frames with a hint)", shown);
    meta.learned = keep; pv[CP(CP_HINTS)] = keep_h;
    set("hnot", 0); set("hhnd", 0);
}

// ---------------------------------------------------------------- the hand: a hand over his mouth
typedef struct { float hum, rms, hi, f, mouth; double F[2]; int ph; } mouth_t;
static void listen(mouth_t *m)                                     // the last second
{
    mv_view_t v; mv_view(&v);
    m->hum = sc_last_ctl.hum; m->rms = m_rms(sim_t - 1.0f, sim_t); m->hi = highs(sim_t - 1.0f, sim_t);
    m->f = m_pitch_at(sim_t - 0.5f, sim_t, 80.0f, 520.0f); m->mouth = v.mouth; m->ph = v.ph[0];
    fmts(sim_t - 1.0f, sim_t, m->F);
}
static void hand_hums(void)
{
    section("mantra: rev E, a hand over his mouth");
    const ctl_t *c = front_ctl();
    sim_rev_e();
    vowels(true);
    check(c->has_lknob && c->has_hand && !c->has_joy && hands.hand == 0.0f && sc_last_ctl.hum == 0.0f, "rev E: the left knob and the sensor, no joystick; no hand, no hum");
    static const int mm[6] = { 0, 240, 200, 145, 90, 40 };
    mouth_t m[6], back;
    float t_down[6];
    for (int k = 0; k < 6; k++) {
        t_down[k] = sim_t;
        ui_awake(); sim_hand(mm[k], 1.6f); listen(&m[k]);
        if (k == 0) sim_shot("reve_open");
        if (k == 3) sim_shot("reve_hand_half");
        if (k == 5) sim_shot("reve_hand_down");
        note("hand %3d mm: hum %.2f, level %+.1f dB, above 800 Hz %+.1f dB, F1 %.0f F2 %.0f, mouth %.2f, %s, %.1f Hz", mm[k], (double)m[k].hum, (double)dbf(m[k].rms, m[0].rms), (double)dbf(m[k].hi, m[0].hi),
             m[k].F[0], m[k].F[1], (double)m[k].mouth, mv_ph_name(m[k].ph), (double)m[k].f);
    }
    // lower each time: more hum, the mouth further shut, F1 further down, less above 800 Hz (until that is 30 dB down:
    // from there the little that is left moves a dB or two either way with M's own resonances)
    bool more = true;
    for (int k = 1; k < 6; k++) {
        bool hi_ok = m[k].hi < m[k - 1].hi || (dbf(m[k].hi, m[0].hi) < -30.0f && dbf(m[k - 1].hi, m[0].hi) < -30.0f);
        if (!(m[k].hum > m[k - 1].hum && hi_ok && m[k].mouth < m[k - 1].mouth && m[k].F[0] < m[k - 1].F[0] + 5.0)) more = false;
    }
    check(more, "VOWELS on AH, the lower the hand the more the mouth shuts: no hand, then 240, 200, 145, 90, 40 mm: the hum %.2f %.2f %.2f %.2f %.2f %.2f, the mouth %.2f %.2f %.2f %.2f %.2f %.2f, F1 %.0f %.0f %.0f %.0f %.0f %.0f Hz, above 800 Hz 0 %.1f %.1f %.1f %.1f %.1f dB, the level 0 %.1f %.1f %.1f %.1f %.1f dB",
          (double)m[0].hum, (double)m[1].hum, (double)m[2].hum, (double)m[3].hum, (double)m[4].hum, (double)m[5].hum,
          (double)m[0].mouth, (double)m[1].mouth, (double)m[2].mouth, (double)m[3].mouth, (double)m[4].mouth, (double)m[5].mouth,
          m[0].F[0], m[1].F[0], m[2].F[0], m[3].F[0], m[4].F[0], m[5].F[0],
          (double)dbf(m[1].hi, m[0].hi), (double)dbf(m[2].hi, m[0].hi), (double)dbf(m[3].hi, m[0].hi), (double)dbf(m[4].hi, m[0].hi), (double)dbf(m[5].hi, m[0].hi),
          (double)dbf(m[1].rms, m[0].rms), (double)dbf(m[2].rms, m[0].rms), (double)dbf(m[3].rms, m[0].rms), (double)dbf(m[4].rms, m[0].rms), (double)dbf(m[5].rms, m[0].rms));
    check(fabs(m[0].F[0] - 730) < 73 && fabs(m[0].F[1] - 1090) < 109 && m[0].mouth > 0.5f, "no hand: the AH of a flat box (F1 %.0f, F2 %.0f; Peterson and Barney's AA, 730 and 1090, within 10 %%), the mouth open (%.2f)", m[0].F[0], m[0].F[1], (double)m[0].mouth);
    check(m[5].hum == 1.0f && c->hand_mm == 40 && m[5].ph == PH_M && m[5].mouth < 0.05f && m[5].F[0] < 360 && dbf(m[5].hi, m[0].hi) < -15.0f && dbf(m[5].rms, m[0].rms) < -1.0f && dbf(m[5].rms, m[0].rms) > -15.0f && fabsf(cents(m[5].f, m[0].f)) < 25.0f,
          "fully down (40 mm) it hums with the mouth shut: the hum %.2f, the lead on %s, the mouth %.2f, F1 %.0f Hz (M's is 270, AH's 730), above 800 Hz %.1f dB, the level %.1f dB: still singing, on the same note (%+.0f cents)",
          (double)m[5].hum, mv_ph_name(m[5].ph), (double)m[5].mouth, m[5].F[0], (double)dbf(m[5].hi, m[0].hi), (double)dbf(m[5].rms, m[0].rms), (double)cents(m[5].f, m[0].f));
    float t_up = sim_t;
    sim_hand(0, 2.6f); listen(&back);
    check(back.hum == 0.0f && hands.hand == 0.0f && fabs(back.F[0] / m[0].F[0] - 1) < 0.05 && fabs(back.F[1] / m[0].F[1] - 1) < 0.05 && fabsf(dbf(back.rms, m[0].rms)) < 1.0f && fabsf(dbf(back.hi, m[0].hi)) < 1.5f && back.mouth > 0.5f,
          "the hand leaves: the mouth opens again: AH (F1 %.0f, F2 %.0f), the level %+.1f dB, above 800 Hz %+.1f dB, the mouth %.2f", back.F[0], back.F[1], (double)dbf(back.rms, m[0].rms), (double)dbf(back.hi, m[0].hi), (double)back.mouth);
    // no click: straight down to 40 mm from nothing, sitting there, leaving; then five quick slaps
    float t0 = sim_t; sim_hand(40, 1.2f); float t1 = sim_t; sim_hand(0, 1.6f);
    float c_down = clicks(t0 - 0.02f, t0 + 0.5f), c_sit = clicks(t0 + 0.5f, t1 - 0.02f), c_up = clicks(t1 - 0.02f, t1 + 1.2f);
    float c_steps = 0; for (int k = 1; k < 6; k++) { float r = clicks(t_down[k] - 0.02f, t_down[k] + 0.5f); if (r > c_steps) c_steps = r; }
    float c_first = clicks(t_up - 0.02f, t_up + 1.2f);
    float t2 = sim_t; for (int k = 0; k < 5; k++) { sim_hand(40, 0.12f); sim_hand(0, 0.2f); }
    sim_run(0.6f);
    float c_slap = clicks(t2 - 0.02f, sim_t - 0.2f), c_ref = clicks(t_down[0] + 0.3f, t_down[0] + 1.4f);
    check(c_down < 3 && c_sit < 3 && c_up < 3 && c_steps < 3 && c_first < 3 && c_slap < 3,
          "no click: the hand straight down to 40 mm %.2f, sitting %.2f, leaving %.2f and %.2f, from height to height %.2f, five slaps %.2f (a click is over 3; AH by itself %.2f)",
          (double)c_down, (double)c_sit, (double)c_up, (double)c_first, (double)c_steps, (double)c_slap, (double)c_ref);
    sim_wav("-reve-hand", t_down[0], sim_t);
    // the hand on the screen: nothing of it without a hand, it lies over his mouth when it is down
    int h_none = 0, h_down = 0;
    for (int i = 0; i < 40; i++) { ui_awake(); sim_tick(); if (hand_there() > h_none) h_none = hand_there(); }
    sim_hand(40, 0.6f); h_down = 11;
    for (int i = 0; i < 40; i++) { ui_awake(); sim_tick(); if (hand_there() < h_down) h_down = hand_there(); }
    sim_hand(0, 2.0f);
    check(h_down == 11 && h_none < 11, "the screen: the hand lies over his mouth while it is down (its thumb and its lower edge, 11 pixels: %d lit in every frame; without a hand at most %d)", h_down, h_none);

    // the chant and Daisy: the hand shuts every mouth, the score's own M included (OM already hums a third of every
    // phrase by itself, so there the hand takes less away). Daisy's words have spikes of their own: the stops' bursts
    // (P, T, K, D, B) measure 5 to 9 on the click scale with no hand anywhere, so a click cannot be told from a
    // consonant there by this measure. Her numbers are printed, not judged: the chants and VOWELS carry the no-click
    // checks, through the same voices
    static const int snd[3] = { S_OM, S_MONKS, S_DAISY };
    for (int k = 0; k < 3; k++) {
        front_midi(0xC0, (uint8_t)snd[k], 0); set("frev", 0); set("fech", 0);
        ui_awake(); sim_run(8.0f); float h0 = highs(sim_t - 6.0f, sim_t), r0 = m_rms(sim_t - 6.0f, sim_t);
        float ta = sim_t; ui_awake(); sim_hand(40, 8.0f); float h1 = highs(sim_t - 6.0f, sim_t), r1 = m_rms(sim_t - 6.0f, sim_t), hum = sc_last_ctl.hum;
        if (k == 1) sim_shot("reve_hand_monks");
        float tb = sim_t; sim_hand(0, 2.0f);
        float cl = fmaxf(clicks(ta - 0.02f, ta + 0.5f), clicks(tb - 0.02f, tb + 1.0f));
        float own = clicks(ta - 6.0f, ta - 0.05f), sit = clicks(ta + 0.5f, tb - 0.05f);
        if (k < 2)
            check(hum == 1.0f && dbf(h1, h0) < -6.0f && r1 > 0.003f && cl < 3, "%s, a hand at 40 mm for 8 s: every mouth shut (the hum %.2f), above 800 Hz %.1f dB, the level %+.1f dB, no click coming or going (%.2f; the chant by itself %.2f)",
                  INST.presets[snd[k]].name, (double)hum, (double)dbf(h1, h0), (double)dbf(r1, r0), (double)cl, (double)own);
        else
            check(hum == 1.0f && dbf(h1, h0) < -6.0f && r1 > 0.003f, "%s, a hand at 40 mm for 8 s: every mouth shut (the hum %.2f), above 800 Hz %.1f dB, the level %+.1f dB (not judged: her consonants' bursts measure %.2f on the click scale by themselves, %.2f with the hand resting, %.2f while it comes and goes)",
                  INST.presets[snd[k]].name, (double)hum, (double)dbf(h1, h0), (double)dbf(r1, r0), (double)own, (double)sit, (double)cl);
    }
    // the amps: the stick's hum was their dive, and the hand's is
    vowels(true); amps_plain();
    float a0 = amps_pitch();
    sim_hand(145, 1.0f); float a_half = amps_pitch();
    sim_hand(40, 1.0f); float a1 = amps_pitch();
    float tb = sim_t; sim_hand(0, 2.0f); float a2 = amps_pitch();
    check(a0 > 0 && fabsf(a1 / a0 - 0.5f) < 0.006f && fabsf(cents(a_half, a0) + 1200.0f * 0.3025f) < 15.0f && fabsf(cents(a2, a0)) < 5.0f,
          "the amps dive with the hand as with the stick down: the bass at %.1f Hz, %.1f at 145 mm (%+.0f cents: the hum there is 0.30), %.1f at 40 mm (an octave), %.1f when it leaves", (double)a0, (double)a_half, (double)cents(a_half, a0), (double)a1, (double)a2);
    (void)tb;
    // SYSTEM > HAND OFF
    set("mvoi", 85); set("mdrn", 0); sim_run(0.5f);
    param_set(CP(CP_HAND), 0); sim_hand(40, 1.0f);
    float off = sc_last_ctl.hum + hands.hand;
    param_set(CP(CP_HAND), 1); sim_hand(0, 0.5f);
    check(off == 0.0f, "SYSTEM > HAND OFF: a hand at 40 mm shuts nothing");
}

// ---------------------------------------------------------------- HANDS > HAND: VIBRATO
static float swing(float seconds)                                  // the lead's pitch, end to end, in cents
{
    float lo = 1e9f, hi = 0;
    for (int i = 0; i < (int)(seconds * 100.0f); i++) { sim_tick(); float f = mv_test_hz(0); if (f < lo) lo = f; if (f > hi) hi = f; }
    return cents(hi, lo);
}
static void hand_vibrato(void)
{
    section("mantra: rev E, HANDS > HAND: VIBRATO");
    vowels(true);
    mouth_t m0, m1; listen(&m0);
    float p0 = swing(1.0f);
    set("hhnd", 1);
    float ta = sim_t; ui_awake(); sim_hand(40, 1.2f);
    float vib = sc_last_ctl.vib_depth, hum = sc_last_ctl.hum, p1 = swing(1.0f);
    listen(&m1); sim_shot("reve_hand_vibrato");
    sim_hand(145, 1.2f); float vib_half = sc_last_ctl.vib_depth, p_half = swing(1.0f);
    float tb = sim_t; sim_hand(0, 2.0f); float p2 = swing(1.0f);
    check(vib == 0.6f && fabsf(vib_half - 0.33f) < 0.005f && hum == 0.0f && p1 > 100.0f && p1 < 140.0f && p_half > 0.4f * p1 && p_half < 0.7f * p1 && p0 < 5.0f && p2 < 5.0f && m1.mouth > 0.5f && fabs(m1.F[0] / m0.F[0] - 1) < 0.08,
          "a hand at 40 mm is the stick pushed up: the vibrato %.2f st, the note swings %.0f cents end to end (at 145 mm %.2f st, %.0f cents; no hand %.0f, after it %.0f); nothing hums (%.2f), the mouth stays open (%.2f, F1 %.0f)",
          (double)vib, (double)p1, (double)vib_half, (double)p_half, (double)p0, (double)p2, (double)hum, (double)m1.mouth, m1.F[0]);
    // the setting changed with the hand down: from the vibrato to the hum and back
    sim_hand(40, 1.0f);
    float t1 = sim_t; set("hhnd", 0); sim_run(1.0f); float hum1 = sc_last_ctl.hum, vib1 = sc_last_ctl.vib_depth;
    float t2 = sim_t; set("hhnd", 1); sim_run(1.0f); float hum2 = sc_last_ctl.hum, vib2 = sc_last_ctl.vib_depth;
    float t3 = sim_t; sim_hand(0, 2.0f);
    float cl = fmaxf(fmaxf(clicks(ta - 0.02f, ta + 0.5f), clicks(tb - 0.02f, tb + 1.0f)), fmaxf(clicks(t1 - 0.02f, t1 + 0.5f), fmaxf(clicks(t2 - 0.02f, t2 + 0.5f), clicks(t3 - 0.02f, t3 + 1.0f))));
    check(hum1 == 1.0f && vib1 == 0.0f && hum2 == 0.0f && vib2 == 0.6f && cl < 3, "HAND switched to HUM and back to VIBRATO under a hand at 40 mm: the hum %.0f then %.0f, the vibrato %.1f then %.1f; no click there, nor when the hand comes and goes (%.2f)",
          (double)hum1, (double)hum2, (double)vib1, (double)vib2, (double)cl);
    set("hhnd", 0);
}

// ---------------------------------------------------------------- the left knob: the note
static void knob_note(void)
{
    section("mantra: rev E, the left knob is the note");
    vowels(true);
    float f0 = pitch_now(), worst = 0;
    int right = 0, row0 = bead_row();
    // up a detent at a time to the octave
    float t_first = sim_t;
    for (int n = 1; n <= 12; n++) {
        sim_lturn(1); ui_awake(); sim_run(0.7f);
        float e = cents(pitch_now(), f0) - 100.0f * (float)n;
        if (fabsf(e) > worst) worst = fabsf(e);
        if ((int)get("hnot") == n && fabsf(e) < 8.0f) right++;
        if (n == 3) { sim_shot("reve_note_up_3"); }
    }
    int row3 = 0, row12 = bead_row();
    sim_shot("reve_note_up_12");
    sim_lturn(1); sim_run(0.3f); sim_lturn(3); sim_run(0.7f);
    float f_top = pitch_now(); int top = (int)get("hnot");
    // five detents on one tick, then down a detent at a time to the octave below
    sim_lturn(-5); sim_run(0.7f); float f7 = pitch_now(); int n7 = (int)get("hnot");
    for (int n = 6; n >= -12; n--) {
        sim_lturn(-1); ui_awake(); sim_run(0.7f);
        float e = cents(pitch_now(), f0) - 100.0f * (float)n;
        if (fabsf(e) > worst) worst = fabsf(e);
        if ((int)get("hnot") == n && fabsf(e) < 8.0f) right++;
        if (n == 3) row3 = bead_row();
    }
    sim_lturn(-4); sim_run(0.7f);
    float f_bot = pitch_now(); int bot = (int)get("hnot"), rowm12 = bead_row();
    sim_shot("reve_note_down_12");
    check(right == 31 && worst < 8.0f, "each detent of the left knob moves the sung note a semitone: 12 up, then 19 down, one at a time (%d of 31 on the note, the worst %.1f cents off; AH at %.1f Hz)", right, (double)worst, (double)f0);
    check(top == 12 && fabsf(cents(f_top, f0) - 1200.0f) < 8.0f && bot == -12 && fabsf(cents(f_bot, f0) + 1200.0f) < 8.0f && n7 == 7 && fabsf(cents(f7, f0) - 700.0f) < 8.0f,
          "it stops an octave either way: four more detents up from +12 leave %+d (%+.0f cents), four more down from -12 leave %+d (%+.0f cents); five detents on one tick from +12: %+d (%+.0f cents)",
          top, (double)cents(f_top, f0), bot, (double)cents(f_bot, f0), n7, (double)cents(f7, f0));
    // it stays: 6 s untouched, another sound and back, the menu's own setting
    sim_run(6.0f); float f_stay = pitch_now();
    front_midi(0xC0, S_OM, 0); sim_run(1.0f); int in_om = (int)get("hnot"); float bend_om = sc_last_ctl.bend;
    vowels(true); float f_again = pitch_now();
    check(fabsf(cents(f_stay, f_bot)) < 5.0f && in_om == -12 && bend_om == -12.0f && fabsf(cents(f_again, f_bot)) < 5.0f && (int)get("hnot") == -12,
          "it stays where it is left: 6 s later %+.0f cents, in another sound (OM: NOTE %+d, the voices' bend %.1f) and back in VOWELS %+.0f cents: it is HANDS > NOTE, a setting of the device, not of a sound",
          (double)cents(f_stay, f0), in_om, (double)bend_om, (double)cents(f_again, f0));
    check(row0 == 44 && row3 == 40 && row12 == 28 && rowm12 == 60, "the screen: the bead on the rail at row %d for 0, %d for +3, %d for +12, %d for -12 (44, 40, 28, 60: the middle, and an octave to each end)", row0, row3, row12, rowm12);
    // the glide: one detent from rest, the lead's pitch every 10 ms
    set("hnot", 0); sim_run(1.0f);
    float h0 = mv_test_hz(0), was = 0, step = 0; int t50 = 0, t95 = 0; bool mono = true;
    float tg = sim_t;
    SI.lk = 1;
    for (int i = 1; i <= 40; i++) {
        sim_tick(); float cnow = cents(mv_test_hz(0), h0);
        if (cnow - was > step) step = cnow - was;
        if (cnow < was - 0.5f) mono = false;
        if (!t50 && cnow >= 50.0f) t50 = i * 10;
        if (!t95 && cnow >= 95.0f) t95 = i * 10;
        was = cnow;
    }
    float glide = get("vgld");
    check(mono && step < 45.0f && t50 >= 20 && t95 > 0 && t95 <= 150 && fabsf(was - 100.0f) < 1.0f,
          "a detent glides as a sung note does (VOWELS' GLIDE %.0f ms): half way after %d ms, 95 cents after %d ms, never back, the largest move in 10 ms %.0f cents, then on the note (%.1f cents)", (double)glide, t50, t95, (double)step, (double)was);
    // no click: single detents, the whole range at once from the menu or MIDI, and with the reverb and echo of the sound
    float c1 = clicks(t_first - 0.02f, t_first + 3.0f), cg = clicks(tg - 0.02f, tg + 0.4f);
    float ta = sim_t; set("hnot", 12); sim_run(0.8f); float tb = sim_t; set("hnot", -12); sim_run(0.8f); float tc = sim_t; set("hnot", 0); sim_run(0.8f);
    float cj = fmaxf(clicks(ta - 0.02f, ta + 0.5f), fmaxf(clicks(tb - 0.02f, tb + 0.5f), clicks(tc - 0.02f, tc + 0.5f)));
    front_midi(0xC0, S_MONKS, 0); ui_awake(); sim_run(6.0f);
    float td = sim_t; for (int k = 0; k < 6; k++) { sim_lturn(k < 3 ? 2 : -2); sim_run(0.5f); }
    float cm = clicks(td - 0.02f, sim_t - 0.2f);
    sim_shot("reve_note_monks");
    check(c1 < 3 && cg < 3 && cj < 3 && cm < 3, "no click: detents on a held AH %.2f and %.2f, NOTE jumped from 0 to +12, to -12 and back from the menu %.2f, the MONKS with their hall and echo turned up and down by twos %.2f (a click is over 3)",
          (double)c1, (double)cg, (double)cj, (double)cm);
    sim_wav("-reve-knob", t_first, sim_t);
    // every sound: the note reaches the voices
    int moved = 0;
    for (int k = 0; k < INST.npresets; k++) {
        front_midi(0xC0, (uint8_t)k, 0); set("hnot", 0); sim_run(2.0f); float b0 = sc_last_ctl.bend;
        sim_lturn(5); sim_run(2.0f);
        if (fabsf(sc_last_ctl.bend - b0 - 5.0f) < 0.001f) moved++; else note("%s: the bend %.2f -> %.2f", INST.presets[k].name, (double)b0, (double)sc_last_ctl.bend);
    }
    check(moved == INST.npresets, "in every sound five detents move every voice five semitones (%d of %d)", moved, INST.npresets);
    // the amps follow
    set("hnot", 0); vowels(true); amps_plain();
    float a0 = amps_pitch();
    float te = sim_t; sim_lturn(7); sim_run(0.6f); float a7 = amps_pitch();
    sim_lturn(-19); sim_run(0.6f); float am = amps_pitch();
    set("hnot", 0); sim_run(0.6f); float ab = amps_pitch();
    (void)te;
    check(a0 > 0 && fabsf(cents(a7, a0) - 700.0f) < 8.0f && fabsf(cents(am, a0) + 1200.0f) < 8.0f && fabsf(cents(ab, a0)) < 5.0f,
          "the amps move with the note: the bass at %.1f Hz, %+.0f cents at +7, %+.0f at -12, %+.0f back at 0", (double)a0, (double)cents(a7, a0), (double)cents(am, a0), (double)cents(ab, a0));
    set("mvoi", 85); set("mdrn", 0);
    // SYSTEM > LEFT KNOB OFF: the note it set is not sung, and is kept
    vowels(true); float g0 = pitch_now();
    sim_lturn(4); sim_run(0.8f); float g4 = pitch_now();
    param_set(CP(CP_LKNOB), 0); sim_run(0.8f); float goff = pitch_now(); sim_lturn(2); sim_run(0.5f); int kept = (int)get("hnot"); int rail = rail_there() || bead_row();
    param_set(CP(CP_LKNOB), 1); sim_run(0.8f); float gon = pitch_now();
    check(fabsf(cents(g4, g0) - 400.0f) < 8.0f && fabsf(cents(goff, g0)) < 5.0f && kept == 4 && rail == 0 && fabsf(cents(gon, g0) - 400.0f) < 8.0f,
          "SYSTEM > LEFT KNOB OFF: the note goes back (%+.0f cents from +4: %+.0f), turning does nothing, the rail is gone, NOTE is kept (%+d); AUTO again: %+.0f cents", (double)cents(g4, g0), (double)cents(goff, g0), kept, (double)cents(gon, g0));
    set("hnot", 0); sim_run(0.5f);
}

// ---------------------------------------------------------------- the two hints
static void hints(void)
{
    section("mantra: rev E, the hints");
    front_midi(0xC0, S_OM, 0); sim_run(0.5f);
    uint32_t keep = meta.learned; float keep_h = pv[CP(CP_HINTS)];
    int g = 0; while (mn_cover() > 0 && ++g < 1000) sim_tick();   // the sound's card goes first
    pv[CP(CP_HINTS)] = 1; meta.learned = ~((1u << LEARN_LKNOB) | (1u << LEARN_HAND));
    g = 0; while (mn_cover() == 0 && ++g < 2000) sim_tick();
    bool first = mn_cover() > 0; sim_run(0.3f); sim_shot("reve_hint_hand");
    sim_hand(145, 0.5f); bool half = (meta.learned >> LEARN_HAND) & 1u;       // just short of half: not yet
    sim_hand(100, 0.5f); bool hand_done = (meta.learned >> LEARN_HAND) & 1u;
    sim_hand(0, 2.0f);
    g = 0; while (mn_cover() > 0 && ++g < 1000) sim_tick();
    g = 0; while (mn_cover() == 0 && ++g < 2000) sim_tick();
    bool second = mn_cover() > 0; sim_run(0.3f); sim_shot("reve_hint_knob");
    sim_lturn(1); bool knob_done = (meta.learned >> LEARN_LKNOB) & 1u; sim_lturn(-1);
    g = 0; while (mn_cover() > 0 && ++g < 1000) sim_tick();
    int more = 0; for (int i = 0; i < 1500; i++) { sim_tick(); if (mn_cover() > 0) more++; }
    check(first && second && !half && hand_done && knob_done && more == 0,
          "rev E's two hints show to an idle player (the hand's %s, the left knob's %s); the hand's goes once a hand has come past half way (%s), the knob's once it has turned (%s); then none (%d frames in 15 s)",
          first ? "yes" : "NO", second ? "yes" : "NO", hand_done && !half ? "yes" : "NO", knob_done ? "yes" : "NO", more);
    meta.learned = keep; pv[CP(CP_HINTS)] = keep_h;
}

// ---------------------------------------------------------------- the cost with the hand down and the note up
static double block_us(int nb)
{
    float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
    struct timespec a, b; double t = 0;
    for (int i = 0; i < nb; i++) {
        for (int j = 0; j < AUDIO_BLOCK; j++) in[j] = 0.05f * sinf((float)(i * AUDIO_BLOCK + j) * 0.03f);
        if (i % 5 == 0) sim_tick();
        clock_gettime(CLOCK_MONOTONIC, &a); front_audio(in, out, AUDIO_BLOCK); clock_gettime(CLOCK_MONOTONIC, &b);
        t += (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
    }
    return t / nb;
}
static void cost(void)
{
    section("mantra: rev E, cost");
    front_midi(0xC0, S_SUNN, 0); sim_run(1.5f);
    double plain = block_us(1500);
    SI.hand_mm = 40; sim_lturn(12); sim_run(1.5f);
    double down = block_us(1500);
    set("hhnd", 1); sim_run(1.0f);
    double vib = block_us(1500);
    SI.hand_mm = 0; set("hhnd", 0); set("hnot", 0); sim_run(0.5f);
    metric("mantra_reve_us_per_block", down > vib ? down : vib);
    check(down < 20.0 && vib < 20.0, "SUNN, the busiest sound: %.1f us per 64-frame block; the hand at 40 mm and the note an octave up %.1f, as the vibrato %.1f (the same budget: 20)", plain, down, vib);
}

void test_rev_e(void)
{
    sim_in_t keep = SI;
    uint32_t keep_l = meta.learned; float keep_hints = pv[CP(CP_HINTS)];
    float keep_bld = get("hbld"), keep_prg = get("sprg"), keep_hold = get("schd");
    SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); SI.gyro = (vec3_t){ 0, 0, 0 };
    front_midi(0xC0, S_OM, 0); sim_run(0.5f);
    pv[CP(CP_HINTS)] = 0;                                          // no hint over the scenes below (hints() turns them on)
    joystick_board();
    hand_hums();
    hand_vibrato();
    knob_note();
    hints();
    cost();
    // back on the board with the joystick, NOTE left at +5 and HAND at VIBRATO: nothing of rev E is left
    section("mantra: back on the board with the joystick");
    vowels(true);
    sim_lturn(5); set("hhnd", 1); sim_hand(40, 1.0f); float f_e = pitch_now();
    SI = keep; SI.mic = NULL; SI.jx = SI.jy = 0; sim_tilt(0, 0);
    sim_run(1.5f);
    float f_c = pitch_now();
    const ctl_t *c = front_ctl();
    check(!c->has_lknob && !c->has_hand && (int)get("hnot") == 5 && fabsf(cents(f_e, f_c) - 500.0f) < 20.0f && hands.note_s == 0.0f && hands.hand == 0.0f && sc_last_ctl.bend == 0.0f && sc_last_ctl.vib_depth == 0.0f && !rail_there() && !bead_row(),
          "back on the board with the joystick with NOTE at +5 and HAND at VIBRATO: no knob, no hand, no rail; the note is the key's again (rev E sang %+.0f cents above it)", (double)cents(f_e, f_c));
    set("hnot", 0); set("hhnd", 0); set("hbld", keep_bld); set("sprg", keep_prg); set("schd", keep_hold);
    meta.learned = keep_l; pv[CP(CP_HINTS)] = keep_hints;
    free(tap); tap = NULL;
    front_midi(0xC0, S_OM, 0); sim_run(0.5f);
}
