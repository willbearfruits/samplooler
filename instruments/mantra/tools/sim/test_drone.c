// DRONE's checks (inst_drone.c), run by test_inst.c. They listen to the amps alone (inst_drone.c copies its own output
// here while dr_tap is set) and to the whole mix for the WAVs.
//   silent and free with MIX > DRONE at 0; it swells in over SWELL (on the bass, as WRECK's test measures it) and
//   the same drone every time from silence; FEEDBACK lifts the 12th to the root; the chord follows the song's chords
//   on the bar lines at CHANGE's pace, sliding (no click); a knock is a hit, the dive an octave, the breath opens it;
//   linked to a pretend WRECK the 4-bar changes land on its bars; no click when a param jumps end to end; peak under
//   0.9 with everything up, no NaN; the cost; WAVs: SUNN with the monks, DOOM DAISY, ORGAN, the amps alone
#include "sim.h"
#include "mantra.h"
#include "link.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t frame; int kind, note; } dr_ev_t;   // as inst_drone.c's
extern float   *dr_tap; extern int dr_tap_cap, dr_tap_n;
extern dr_ev_t  dr_evlog[1024]; extern int dr_evn;
extern float    dr_pre_peak, dr_fb_hz, dr_fb_lvl;
void mn_prof(float *us, int n);
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
enum { EV_DRONE = 300, EV_HIT = 301, EV_CHANGE = 302 };
enum { S_OM, S_MONKS, S_OMMANI, S_KHOOMEI, S_SYGYT, S_KARGYRAA, S_DAISY, S_DAISY8, S_VOWELS, S_HUM, S_SUNN, S_DOOM, S_ORGAN };
#define PR_DRONE 3                 // instrument.c's cost slot

static int ptag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = ptag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = ptag(t); return p >= 0 ? pv[p] : 0.0f; }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); SI.gyro = (vec3_t){ 0, 0, 0 }; }
static void sound(int k) { front_midi(0xC0, (uint8_t)k, 0); sim_run(0.05f); }
static float fpb(void) { return (float)(song.fpb ? song.fpb : 19200); }

// ---------------------------------------------------------------- the amps alone
static float *tap; static uint32_t tap_f0;
static void tap_on(float seconds)
{
    int n = (int)(seconds * AUDIO_SR) + 8192;
    tap = realloc(tap, (size_t)n * sizeof(float));
    dr_tap = tap; dr_tap_cap = n; dr_tap_n = 0; tap_f0 = audio_frames(); dr_evn = 0;
}
static void tap_off(void) { dr_tap = NULL; }
static int ti(float t) { int i = (int)(t * AUDIO_SR); return i < 0 ? 0 : i > dr_tap_n ? dr_tap_n : i; }
static float t_of(uint32_t frame) { return (float)(int32_t)(frame - tap_f0) / (float)AUDIO_SR; }
static float a_rms(float t0, float t1)
{
    int a = ti(t0), b = ti(t1); double e = 0;
    for (int i = a; i < b; i++) e += (double)tap[i] * tap[i];
    return b > a ? (float)sqrt(e / (b - a)) : 0.0f;
}
static float a_peak(float t0, float t1) { int a = ti(t0), b = ti(t1); float p = 0; for (int i = a; i < b; i++) if (fabsf(tap[i]) > p) p = fabsf(tap[i]); return p; }
static float a_goertzel(float t0, float t1, float f)
{
    int a = ti(t0), b = ti(t1);
    double w = 2 * M_PI * f / AUDIO_SR, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = a; i < b; i++) { double h = 0.5 - 0.5 * cos(2 * M_PI * (i - a) / (double)(b - a)); double s0 = tap[i] * h + c * s1 - s2; s2 = s1; s1 = s0; }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return (float)(p / ((double)(b - a) * (b - a) + 1));
}
// the power within +-0.6 % of f (the feedback wobbles +-0.3 % in pitch, so its line is spread: bins 0.1 % apart, summed)
static float a_band(float t0, float t1, float f)
{
    float sum = 0;
    for (int k = -6; k <= 6; k++) sum += a_goertzel(t0, t1, f * (1.0f + 0.001f * (float)k));
    return sum;
}
// the pitch of the amps alone over a stretch (autocorrelation: the bass alone is one saw, clean to measure)
static float a_pitch(float t0, float t1, float fmin, float fmax) { int a = ti(t0), b = ti(t1); return b > a ? m_pitch(tap + a, b - a, fmin, fmax) : 0.0f; }
// the strongest line between fmin and fmax (Goertzel, Hann, steps of 1/64 semitone): a saw's fundamental
static float a_peak_hz(float t0, float t1, float fmin, float fmax)
{
    float best = -1, at = 0;
    for (float f = fmin; f <= fmax; f *= 1.00090f) { float p = a_goertzel(t0, t1, f); if (p > best) { best = p; at = f; } }
    return at;
}
// a click at a time: the jump in the first 3 ms against the signal's own jumps just before and after
static float a_click(float t)
{
    int c = ti(t);
    float sp = 0, cx = 0;
    for (int i = c > 0 ? c : 1; i < ti(t + 0.003f); i++) { float d = fabsf(tap[i] - tap[i - 1]); if (d > sp) sp = d; }
    for (int i = ti(t - 0.025f) > 0 ? ti(t - 0.025f) : 1; i < ti(t - 0.001f); i++) { float d = fabsf(tap[i] - tap[i - 1]); if (d > cx) cx = d; }
    for (int i = ti(t + 0.005f); i < ti(t + 0.030f); i++) { float d = fabsf(tap[i] - tap[i - 1]); if (d > cx) cx = d; }
    return sp / (cx + 0.002f);
}
static void wav_solo(const char *name)                              // out/mantra-drone-solo-<name>.wav: the amps alone
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s-drone-solo-%s.wav", sim_name, name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t sr = AUDIO_SR, br = sr * 2, ds = (uint32_t)dr_tap_n * 2, rs = 36 + ds; uint16_t one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fl = 16; fwrite(&fl, 4, 1, f); fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < dr_tap_n; i++) { float v = clampf(tap[i], -1.0f, 1.0f); int16_t q = (int16_t)(v * 32767.0f); fwrite(&q, 2, 1, f); }
    fclose(f);
}

// the amps on their own terms: a held chord, nothing else on. On VOWELS: the host passes the score's hum (the monks'
// mouths closing to M in OM) to the amps as the dive, so under the chant the amps dip an octave with every M
// (reported); VOWELS' score never hums, so the amps can be measured alone
static void plain(void)
{
    if (strcmp(host_sound_name(), "VOWELS")) sound(S_VOWELS);
    set("dorg", 0); set("dfbk", 0); set("dhum", 0); set("dmod", 0); set("dsub", 0); set("dvoc", 0); set("dscl", 0);
    set("dkno", 0); set("dpnd", 0); set("dchg", 2); set("dswl", 3); set("dgan", 70); set("dtun", 3);
}
static float midi_hz_d(int n) { return 440.0f * powf(2.0f, (float)(n - 69) / 12.0f); }
static int expect_root(int pc, int floor_) { return floor_ + ((pc - floor_) % 12 + 12) % 12; }
static float drone_us(void)                                        // the drone's cost, best of three seconds
{
    float best = 1e9f;
    for (int run = 0; run < 3; run++) {
        float junk[8]; mn_prof(junk, 8);
        sim_run(1.0f);
        float parts[8]; mn_prof(parts, 8);
        float us = parts[PR_DRONE] * 0.96f;
        if (us < best) best = us;
    }
    return best;
}
// the bar line at or before a frame, and the bar's number (as inst_drone.c counts: from the song's beat 0)
static uint32_t bar_of(uint32_t frame, uint32_t *bar)
{
    uint32_t bl = (uint32_t)fpb() * 4u;
    int32_t pos = mn_mod((int32_t)(frame - song.origin), (int32_t)((uint32_t)fpb() * SONG_WRAP_BEATS));
    *bar = (uint32_t)pos / bl;
    return frame - (uint32_t)pos % bl;
}

// ---------------------------------------------------------------- silent and free
static void silent(void)
{
    section("drone: silent and free with MIX > DRONE at 0");
    quiet(); sound(S_OM); sim_run(2.0f);
    tap_on(2.5f); sim_run(2.0f); tap_off();
    float p = a_peak(0, 2.0f), us = drone_us();
    dr_view_t v; dr_view(&v);
    check(p == 0.0f && !v.playing && v.note < 0, "OM (MIX > DRONE 0): the amps add nothing at all (peak %.5f, the screen: nothing held)", (double)p);
    metric("drone_us_silent", us);
    check(us < 0.3f, "and cost %.2f us per block (a few checks)", (double)us);
}

// ---------------------------------------------------------------- the drone
// One run, the amps alone tapped for 12 s from MIX > DRONE turned up (struck from silence, the strings at rest).
typedef struct {
    float t0;                  // the strike, seconds into the tap
    float w[8];                // rms per half second from it
    float plateau;             // rms over 4 .. 9.5 s
    float sec[6];              // rms per second from 4 s
    float hz;                  // the feedback partial at 10 s
    float fbv1, fbv2;          // the screen's feedback at 2.5 s and 9 s
    float sw1, sw2;            // the screen's swell at 0.5 s and 9 s
    float part[8];             // the partials' shares late: 110, 165, 220, 330, 440, 550, 660, 880 Hz
} drone_t;
static void drone_run(int voice, float swell, float fbk, float hum, float settle, drone_t *m)
{
    quiet(); plain(); set("dvoc", (float)voice); set("dsub", 50);
    set("dgan", 90); set("dfbk", fbk); set("dhum", hum); set("dswl", swell);
    set("mdrn", 0); sim_run(settle);                               // silent
    tap_on(13.0f);
    set("mdrn", 70);
    float fbv[48], swv[48]; m->hz = 0;
    for (int k = 0; k < 48; k++) { sim_run(0.25f); dr_view_t v; dr_view(&v); fbv[k] = v.feedback; swv[k] = v.swell; if (k == 39) m->hz = dr_fb_hz; }
    tap_off();
    m->t0 = -1;
    for (int e = 0; e < dr_evn && e < 1024; e++) if (dr_evlog[e].kind == EV_DRONE) { m->t0 = t_of(dr_evlog[e].frame); break; }
    float t0 = m->t0 < 0 ? 0.0f : m->t0;
    for (int k = 0; k < 8; k++) m->w[k] = a_rms(t0 + 0.5f * (float)k, t0 + 0.5f * (float)(k + 1));
    m->plateau = a_rms(t0 + 4.0f, t0 + 9.5f);
    for (int k = 0; k < 6; k++) m->sec[k] = a_rms(t0 + 4.0f + (float)k, t0 + 5.0f + (float)k);
    float r = a_rms(t0 + 6.5f, t0 + 9.5f);
    { static const float pf[8] = { 110, 165, 220, 330, 440, 550, 660, 880 }; for (int q = 0; q < 8; q++) m->part[q] = a_band(t0 + 6.5f, t0 + 9.5f, pf[q]) / (r * r + 1e-12f); }
    int k1 = (int)((t0 + 2.5f) / 0.25f) - 1, k2 = (int)((t0 + 9.0f) / 0.25f) - 1, k0 = (int)((t0 + 0.5f) / 0.25f) - 1;   // sample k is at 0.25 (k + 1) s
    if (k2 > 47) k2 = 47;
    if (k0 < 0) k0 = 0;
    m->fbv1 = fbv[k1]; m->fbv2 = fbv[k2]; m->sw1 = swv[k0]; m->sw2 = swv[k2];
}
static void drone(void)
{
    section("drone: the swell, the same drone every time, the feedback");
    set("sprg", 0); set("schd", 0); set("skey", 9);                // A, DROP C: A2, 110 Hz; the feedback's 12th is 330 Hz
    drone_t a, b, b2, c, d;
    drone_run(2, 3.0f, 100, 40, 2.5f, &a);                         // the feedback, full
    wav_solo("a2-feedback");
    drone_run(2, 3.0f, 0, 40, 2.5f, &b);                           // the same without it: the feedback's control
    drone_run(2, 3.0f, 0, 40, 2.63f, &b2);                         // ... again, from another moment
    drone_run(1, 3.0f, 0, 0, 2.5f, &c);                            // the bass alone (one saw and a sine: no doubled
    drone_run(1, 12.0f, 0, 0, 2.5f, &d);                           // strings beating): the swell, and the slowest one
    {   const drone_t *runs[5] = { &a, &b, &b2, &c, &d };
        static const char *const nm[5] = { "FEEDBACK 100", "FEEDBACK 0", "FEEDBACK 0 again", "bass, SWELL 3", "bass, SWELL 12" };
        for (int r = 0; r < 5; r++) {
            char line[200]; int w = snprintf(line, sizeof line, "%-16s struck %.2f s, rms per half second:", nm[r], (double)runs[r]->t0);
            for (int j = 0; j < 8; j++) w += snprintf(line + w, sizeof line - (size_t)w, " %.3f", (double)runs[r]->w[j]);
            note("%s; settled %.3f", line, (double)runs[r]->plateau);
        }
        for (int r = 0; r < 2; r++)
            note("%-16s partials late, dB of the sound: 110 %.1f 165 %.1f 220 %.1f 330 %.1f 440 %.1f 660 %.1f 880 %.1f", nm[r],
                 (double)(10.0f * log10f(runs[r]->part[0] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[1] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[2] + 1e-12f)),
                 (double)(10.0f * log10f(runs[r]->part[3] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[4] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[6] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[7] + 1e-12f)));
    }
    check(a.t0 >= 0.0f && a.t0 < 0.1f, "MIX > DRONE turned up: the chord is struck at once (%.2f s after)", (double)a.t0);
    float lo = 1e9f, hi = 0;
    for (int j = 0; j < 6; j++) { if (a.sec[j] < lo) lo = a.sec[j]; if (a.sec[j] > hi) hi = a.sec[j]; }
    check(lo > 0.75f * a.plateau && hi < 1.25f * a.plateau, "it holds: 4 to 10 s in, each second's rms is %.3f .. %.3f (%.3f over the stretch)", (double)lo, (double)hi, (double)a.plateau);
    float same = 0;
    for (int j = 0; j < 8; j++) { float dd = fabsf(b2.w[j] / (b.w[j] + 1e-9f) - 1.0f); if (dd > same) same = dd; }
    check(b.t0 >= 0.0f && b2.t0 >= 0.0f && same < 0.03f, "struck from silence at another moment it is the same drone (rms within %.1f %% in every half second)", (double)(same * 100.0f));
    // the swell (on the bass: its level is the swell's): a one-pole over a third of SWELL
    float p = c.plateau;
    bool rising = c.w[0] < 0.4f * p && c.w[1] > 0.35f * p && c.w[1] < 0.7f * p && c.w[2] > c.w[1] && c.w[3] > 0.7f * p && c.w[3] < 0.95f * p && c.w[7] > 0.9f * p;
    check(rising && c.sw1 < 0.5f && c.sw2 > 0.95f, "SWELL 3 s: it swells in (rms %.3f at 0.5 s, %.3f at 1 s, %.3f at 1.5 s, %.3f at 2 s, %.3f at 4 s; settled %.3f; the screen's swell %.2f -> %.2f)",
          (double)c.w[0], (double)c.w[1], (double)c.w[2], (double)c.w[3], (double)c.w[7], (double)p, (double)c.sw1, (double)c.sw2);
    float g3 = c.w[7] / (c.w[3] + 1e-9f), g12 = d.w[7] / (d.w[3] + 1e-9f);
    check(g3 < 1.3f && g12 > 1.4f && d.w[3] < 0.55f * d.plateau, "SWELL 12 s is slower: at 2 s it is %.2f of where it gets by 10 s, and from 2 s to 4 s it still grows %.2fx (SWELL 3 s: %.2fx)",
          (double)(d.w[3] / d.plateau), (double)g12, (double)g3);
    // the feedback: the 12th against the root, FEEDBACK 100 against 0, two runs the same in every other way
    float twelfth_on = 10.0f * log10f(a.part[3] / (a.part[0] + 1e-12f)), twelfth_off = 10.0f * log10f(b.part[3] / (b.part[0] + 1e-12f));
    check(fabsf(a.hz / 330.0f - 1.0f) < 0.01f && a.fbv2 > 0.4f && a.fbv1 < a.fbv2 && twelfth_on > -4.0f && twelfth_on - twelfth_off > 4.0f,
          "the feedback grows and sings: FEEDBACK 100 lifts the 12th (%.0f Hz) to %.1f dB of the root 6.5 to 9.5 s in (without: %.1f dB); the screen's feedback %.2f -> %.2f",
          (double)a.hz, (double)twelfth_on, (double)twelfth_off, (double)a.fbv1, (double)a.fbv2);
    // in key: the root at a few keys and tunings (the bass alone: the lowest partial is the root)
    quiet(); plain(); set("dvoc", 0); set("dgan", 0); set("dsub", 0); set("mdrn", 70);   // the guitar, clean: the lowest partial is the root
    static const int keys[3] = { 9, 4, 0 }, tunings[3] = { 0, 3, 5 }, floors[7] = { 40, 38, 37, 36, 35, 33, 31 };
    int ok = 0, n = 0;
    for (int k = 0; k < 3; k++) for (int t = 0; t < 3; t++) {
        set("skey", (float)keys[k]); set("dtun", (float)tunings[t]);
        set("mdrn", 0); sim_run(1.5f); set("mdrn", 70); sim_run(1.5f);   // struck fresh: the doubled strings start in phase
        tap_on(2.2f); sim_run(2.0f); tap_off();
        int want = expect_root(keys[k], floors[tunings[t]]);
        float f = midi_hz_d(want);
        // a line at the root: well over the semitones beside it, and over the octave under it (the register)
        float at = a_goertzel(0.1f, 2.0f, f), beside = 0;
        for (int d = -2; d <= 2; d++) if (d) { float q = a_goertzel(0.1f, 2.0f, f * powf(2.0f, (float)d / 12.0f)); if (q > beside) beside = q; }
        float below = a_goertzel(0.1f, 2.0f, 0.5f * f);
        dr_view_t v; dr_view(&v);
        n++;
        if (at > 4.0f * beside && v.note == want) ok++;
        else note("key %d tuning %d: want MIDI %d (%.1f Hz): the view's root %d, the line %.2g, beside it %.2g, an octave under %.2g (the power amp's difference tone)", keys[k], tunings[t], want, (double)f, v.note, (double)at, (double)beside, (double)below);
    }
    check(ok == n, "the root is the song's key, at or above the lowest string of each tuning (%d of %d: a line there, 6 dB over the semitones beside it)", ok, n);
    set("mdrn", 0); sim_run(1.0f);
}

// ---------------------------------------------------------------- the chord follows the song
static void chords(void)
{
    section("drone: the chord follows the song's chords on the bar lines");
    quiet(); plain(); set("dvoc", 1); set("dgan", 40); set("dsub", 30);
    set("skey", 9); set("sprg", 1); set("sbar", 0); set("dtun", 3);   // A major, I V vi IV, a chord a bar: A2 E2 F#2 D2
    static const int want[4] = { 45, 40, 42, 38 };                 // the roots of I V vi IV there
    static const int pers[4] = { 1, 2, 4, 0 }, expect_n[4] = { 8, 4, 0, 0 }, dchg[4] = { 0, 1, 2, 4 };   // changes in 8 bars from a 4-bar line (4 BARS: bars 0 and 4 are both I)
    static const char *const nm[4] = { "EVERY BAR", "2 BARS", "4 BARS", "NEVER" };
    float bar_s = 4.0f * fpb() / AUDIO_SR;
    for (int c = 0; c < 4; c++) {
        set("dchg", (float)dchg[c]); set("mdrn", 0); sim_run(1.0f);
        // start just after a 4-bar line, with the chord of that bar (I)
        { uint32_t bar; while (audio_frames() - bar_of(audio_frames(), &bar) > (uint32_t)(0.2f * fpb()) || bar % 4 != 0) sim_tick(); }
        set("mdrn", 70); sim_run(0.2f);
        dr_view_t v0; dr_view(&v0);
        int bars = 8;
        tap_on(bar_s * (float)bars + 1.0f); sim_run(bar_s * (float)bars); tap_off();
        // a change is the last EV_CHANGE on its bar line (the song's chord can come a tick after the line: the bar's
        // chord is taken as it stood, then the late one, 10 ms on; the glide is 150 ms long)
        int nch = 0, on_bar = 0, in_tune = 0, clean = 0; float worst_ms = 0, worst_click = 0; uint32_t last_line = 0; int last_e = -1, held = v0.note;
        for (int e = 0; e <= dr_evn && e <= 1024; e++) {
            uint32_t b = 0, line = 0;
            if (e < dr_evn && e < 1024) { if (dr_evlog[e].kind != EV_CHANGE) continue; line = bar_of(dr_evlog[e].frame, &b); }
            if (last_e >= 0 && (e >= dr_evn || e >= 1024 || line != last_line)) {   // the last change on the line before
                const dr_ev_t *E = &dr_evlog[last_e];
                uint32_t bb; uint32_t ll = bar_of(E->frame, &bb);
                if (E->note == held) { if (e < dr_evn && e < 1024) { last_e = e; last_line = line; } continue; }   // back where it was: no change
                held = E->note;
                nch++;
                float ms = (float)(int32_t)(E->frame - ll) / (AUDIO_SR / 1000.0f);
                if (ms > worst_ms) worst_ms = ms;
                if (ms >= 0.0f && ms < 25.0f && (pers[c] == 0 || bb % (uint32_t)pers[c] == 0)) on_bar++;
                if (E->note == want[bb % 4]) in_tune++;
                float cl = a_click(t_of(ll));
                if (cl > worst_click) worst_click = cl;
                if (cl < 3.0f) clean++;
            }
            if (e < dr_evn && e < 1024) { last_e = e; last_line = line; }
        }
        int expect = expect_n[c];
        dr_view_t v1; dr_view(&v1);
        if (pers[c] == 0 || pers[c] == 4)
            check(nch == expect && v1.note == v0.note, "CHANGE %s: over %d bars of I V vi IV the chord stays (%d changes; the root %d)", nm[c], bars, nch, v1.note);
        else
            check(nch == expect && on_bar == nch && in_tune == nch && clean == nch,
                  "CHANGE %s: %d changes in %d bars, each on its bar line (worst %.1f ms late), each the bar's chord in the tuning's register, no click (worst %.1fx)",
                  nm[c], nch, bars, (double)worst_ms, (double)worst_click);
    }
    // 4 BARS with a chord every 2 bars: the 4-bar lines take I, vi, I, vi
    set("sbar", 1); set("dchg", 2); set("mdrn", 0); sim_run(1.0f);
    { uint32_t bar; while (audio_frames() - bar_of(audio_frames(), &bar) > (uint32_t)(0.2f * fpb()) || bar % 4 != 0) sim_tick(); }
    set("mdrn", 70); sim_run(0.2f);
    tap_on(bar_s * 16.0f + 1.0f); sim_run(bar_s * 16.0f); tap_off();
    int nch = 0, good = 0;
    for (int e = 0; e < dr_evn && e < 1024; e++) {
        const dr_ev_t *E = &dr_evlog[e];
        if (E->kind != EV_CHANGE) continue;
        uint32_t b; uint32_t line = bar_of(E->frame, &b);
        if (e + 1 < dr_evn && e + 1 < 1024 && dr_evlog[e + 1].kind == EV_CHANGE) { uint32_t b2; if (bar_of(dr_evlog[e + 1].frame, &b2) == line) continue; }   // the last on its line
        nch++;
        int w = want[(b / 2) % 4];
        if (b % 4 == 0 && E->note == w && (E->frame - line) < (uint32_t)(0.025f * AUDIO_SR)) good++;
    }
    check(nch == 4 && good == 4, "CHANGE 4 BARS, a chord every 2 bars: the 4-bar lines take I and vi in turn (%d changes, %d right)", nch, good);
    // the glide: a change slides over about 0.15 s (the bass's lowest partial, before and after)
    set("sbar", 0); set("dchg", 0); set("mdrn", 0); sim_run(1.0f);
    { uint32_t bar; while (audio_frames() - bar_of(audio_frames(), &bar) > (uint32_t)(0.2f * fpb())) sim_tick(); }
    set("mdrn", 70); sim_run(0.5f);
    int n_from; { dr_view_t v; dr_view(&v); n_from = v.note; }
    tap_on(bar_s * 2.0f + 1.0f); sim_run(bar_s * 1.5f); tap_off();
    float t_ch = -1; int n_to = -1;
    for (int e = 0; e < dr_evn && e < 1024; e++) if (dr_evlog[e].kind == EV_CHANGE) { t_ch = t_of(dr_evlog[e].frame); n_to = dr_evlog[e].note; }
    // the bass's pitch (one saw: its autocorrelation is clean) before, on the way and after: an octave under the root
    float want_b = 0.5f * midi_hz_d(n_from), want_a = 0.5f * midi_hz_d(n_to);
    float lo = 0.9f * fminf(want_a, want_b), hi = 1.1f * fmaxf(want_a, want_b);
    float f_before = t_ch > 0.5f ? a_peak_hz(t_ch - 0.5f, t_ch - 0.02f, lo, hi) : 0;
    float f_mid = t_ch > 0 ? a_peak_hz(t_ch + 0.03f, t_ch + 0.13f, lo, hi) : 0;
    float f_after = t_ch > 0 ? a_peak_hz(t_ch + 0.6f, t_ch + 1.1f, lo, hi) : 0;
    bool between = (f_mid - fminf(want_a, want_b)) * (fmaxf(want_a, want_b) - f_mid) > 0.0f;
    check(t_ch > 0 && fabsf(f_before / want_b - 1.0f) < 0.01f && fabsf(f_after / want_a - 1.0f) < 0.01f && between,
          "a new chord slides in: the bass at %.1f Hz before, %.1f Hz on the way (0.03 .. 0.13 s), %.1f Hz after (the root MIDI %d -> %d)", (double)f_before, (double)f_mid, (double)f_after, n_from, n_to);
    set("mdrn", 0); set("sprg", 0); sim_run(1.0f);
}

// ---------------------------------------------------------------- a knock, the dive, the breath, stop
// a knock, as test_inst.c's. The host's knock detector rests 150 ms after a knock, counted on ticks that carry a burst
// (every tick on the device; here only ticks a test gives one), so 16 still bursts go first.
static void knock(void)
{
    for (int k = 0; k < 16; k++) { for (int i = 0; i < 10; i++) SI.burst[i] = (vec3_t){ 0, 0, 1.0f }; SI.nburst = 10; sim_tick(); }
    for (int i = 0; i < 10; i++) SI.burst[i] = (vec3_t){ 0, 0, 1.0f + (i == 3 ? 3.0f : i == 4 ? -1.8f : 0.0f) };
    SI.nburst = 10; sim_tick();
}

static void hands_(void)
{
    section("drone: a knock, the dive, the breath, stopped");
    quiet(); plain(); set("dvoc", 2); set("dgan", 40); set("dsub", 30); set("skey", 9); set("sprg", 0);
    set("dkno", 1); set("mdrn", 70); sim_run(6.0f);
    tap_on(2.0f); sim_run(0.5f); float before = a_peak(0.1f, 0.5f);
    knock(); sim_run(1.0f); tap_off();
    int hits = 0; float t_hit = -1;
    for (int e = 0; e < dr_evn && e < 1024; e++) if (dr_evlog[e].kind == EV_HIT) { hits++; t_hit = t_of(dr_evlog[e].frame); }
    float after = t_hit >= 0 ? a_peak(t_hit, t_hit + 0.15f) : 0;
    set("dkno", 0); sim_run(0.5f);
    tap_on(1.0f); knock(); sim_run(0.5f); tap_off();
    int hits_off = 0; for (int e = 0; e < dr_evn && e < 1024; e++) if (dr_evlog[e].kind == EV_HIT) hits_off++;
    check(hits == 1 && after > 1.15f * before && hits_off == 0, "KNOCK HIT: a knock strikes the chord again (%d hit, the peak %.3f -> %.3f); KNOCK OFF: it leaves the amps alone (%d)",
          hits, (double)before, (double)after, hits_off);
    // POUND: a hit on every bar line
    set("dpnd", 1); sim_run(0.5f);
    tap_on(10.0f); sim_run(8.0f * fpb() / AUDIO_SR + 0.2f); tap_off();
    int pounds = 0, on_line = 0;
    for (int e = 0; e < dr_evn && e < 1024; e++) if (dr_evlog[e].kind == EV_HIT) { pounds++; uint32_t b; if (dr_evlog[e].frame - bar_of(dr_evlog[e].frame, &b) < 64) on_line++; }
    check(pounds == 2 && on_line == 2, "POUND EVERY BAR: a hit on each bar line (%d in two bars, %d on the line)", pounds, on_line);
    set("dpnd", 0);
    // the dive: the stick down, an octave
    set("dvoc", 1); set("dsub", 0); sim_run(1.5f);
    tap_on(1.2f); sim_run(1.0f); tap_off();
    float f0 = a_pitch(0.1f, 1.0f, 20.0f, 200.0f);
    SI.jy = -1.0f; sim_run(0.6f);
    tap_on(1.2f); sim_run(1.0f); tap_off();
    float f1 = a_pitch(0.1f, 1.0f, 20.0f, 200.0f);
    SI.jy = 0.0f; sim_run(0.6f);
    check(f0 > 0 && fabsf(f1 / f0 - 0.5f) < 0.006f, "the stick down, full: an octave lower (%.1f Hz -> %.1f Hz)", (double)f0, (double)f1);
    // the bend: the stick right, two semitones up (the host's)
    SI.jx = 1.0f; sim_run(0.6f);
    tap_on(1.2f); sim_run(1.0f); tap_off();
    float f2 = a_pitch(0.1f, 1.0f, 20.0f, 200.0f);
    SI.jx = 0.0f; sim_run(0.6f);
    check(f0 > 0 && fabsf(f2 / f0 - 1.12246f) < 0.01f, "the stick right: two semitones up (%.1f Hz -> %.1f Hz)", (double)f0, (double)f2);
    // the breath: the box pumped, the gain opens and the swell lifts
    set("dvoc", 2); set("dsub", 30); set("dgan", 30); sim_run(2.0f);
    tap_on(1.2f); sim_run(1.0f); tap_off();
    float r_still = a_rms(0.1f, 1.0f);
    float pump_max = 0;
    for (int k = 0; k < 300; k++) { float ph = (float)k * 0.01f * 3.0f; SI.acc = (vec3_t){ 0, 0, 1.0f + 0.7f * sinf(6.2831853f * ph) }; sim_tick(); if (hands.pump > pump_max) pump_max = hands.pump; }
    tap_on(1.2f);
    for (int k = 0; k < 100; k++) { float ph = (float)(300 + k) * 0.01f * 3.0f; SI.acc = (vec3_t){ 0, 0, 1.0f + 0.7f * sinf(6.2831853f * ph) }; sim_tick(); }
    tap_off();
    float r_pumped = a_rms(0.1f, 1.0f);
    sim_tilt(0, 0); sim_run(1.0f);
    check(pump_max > 0.3f && r_pumped > 1.05f * r_still, "pumped (breath %.2f): the amps open up (rms %.3f -> %.3f)", (double)pump_max, (double)r_still, (double)r_pumped);
    // stopped: what rings fades and nothing new starts; started again, the chord comes back
    sim_run(1.0f);
    tap_on(12.0f);
    sim_run(1.0f);
    float on = a_rms(0.2f, 1.0f);
    hands.running = false;                                         // (the host's; REC is the looper's in MANTRA)
    sim_run(3.0f);
    float off = a_rms(2.5f, 3.9f);
    hands.running = true; sim_run(3.0f);
    tap_off();
    float back = a_rms(5.5f, 6.9f);
    check(off < 0.01f * on && back > 0.2f * on, "stopped, the drone fades (rms %.4f of %.3f, 2.5 s on); started again it comes back (%.3f)", (double)off, (double)on, (double)back);
    set("mdrn", 0); sim_run(1.0f);
}

// ---------------------------------------------------------------- linked to a WRECK
static void with_wreck(void)
{
    section("drone: linked to a WRECK");
    quiet(); plain(); set("dvoc", 1); set("dgan", 40); set("dsub", 30); set("dchg", 2); set("mdrn", 0); sim_run(0.5f);
    int k = sim_peer_add("WRECK", 2, 20.0, 5.0e9);
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(20000, o, 4, 0); link_set_harm(9, 1, 1, 0, 1, -1); }   // 96 BPM, A minor, I V vi IV, 2 bars each
    sim_peer_use(-1);
    sim_run(2.5f);
    bool synced = !link_is_clock() && link_synced() && song.fpb == 20000 && (int)get("sprg") == 1 && (int)get("sbar") == 1;
    if (!synced) note("link: clock %d synced %d fpb %u sprg %d sbar %d skey %d sscl %d", (int)link_is_clock(), (int)link_synced(), song.fpb, (int)get("sprg"), (int)get("sbar"), (int)get("skey"), (int)get("sscl"));
    set("mdrn", 70); sim_run(0.2f);
    float bar_s = 4.0f * fpb() / AUDIO_SR;
    tap_on(bar_s * 13.0f); sim_run(bar_s * 12.5f); tap_off();
    int nch = 0, on_wreck_bar = 0, in_tune = 0; float worst = 0;
    static const int want[4] = { 45, 40, 41, 38 };                 // A minor, DROP C: A2 E2 F2 D2
    for (int e = 0; e < dr_evn && e < 1024; e++) {
        const dr_ev_t *E = &dr_evlog[e];
        if (E->kind != EV_CHANGE) continue;
        nch++;
        // where WRECK was at that frame: its beats now, less the frames since
        double pb = sim_peer_beats(k) - (double)(int32_t)(audio_frames() - E->frame) / (double)song.fpb;
        double off = fabs(pb / 16.0 - floor(pb / 16.0 + 0.5)) * 16.0 * 60000.0 / 96.0;   // ms from WRECK's nearest machine bar (16 song beats)
        if (off > worst) worst = (float)off;
        if (off < 25.0) on_wreck_bar++;                            // the take waits 12 ms for the song's chord (host_degree_at would make it exact)
        uint32_t b; bar_of(E->frame, &b);
        if (E->note == want[(b / 2) % 4]) in_tune++;
    }
    check(synced && nch >= 2 && on_wreck_bar == nch && in_tune == nch,
          "a WRECK at 96 BPM leads: MANTRA's 4-bar chord changes land on WRECK's machine bars (%d changes, the worst %.1f ms after, each the chord of that bar)",
          nch, (double)worst);
    set("mdrn", 0);
    sim_peer_power(k, false); sim_run(1.5f);
    set("skey", 9); set("sscl", 0); set("stmp", 100); set("sprg", 0); set("sbar", 0); set("dchg", 2); sim_run(0.5f);
}

// ---------------------------------------------------------------- clicks, levels, cost
static void clicks_levels_cost(void)
{
    section("drone: clicks, levels, cost");
    quiet(); sound(S_SUNN); sim_run(5.0f);
    int tested = 0, bad = 0; float worst = 0;
    for (int p = mod_base[M_DRONE]; p < mod_base[M_DRONE] + MOD_PARAMS_MAX; p++) {
        const param_t *d = param(p);
        if (d->kind == PK_ACT || (d->flags & PF_HIDE)) continue;
        float keep = pv[p], r = 0;
        for (int dir = 0; dir < 2; dir++) {
            param_set(p, dir ? d->min : d->max);
            sim_run(0.35f);
            tap_on(0.4f);
            param_set(p, dir ? d->max : d->min);
            sim_run(0.2f); tap_off();
            float c = a_click(0.0f);
            if (c > r) r = c;
        }
        param_set(p, keep); sim_run(0.3f);
        tested++;
        if (r > worst) worst = r;
        if (r > 3.0f) { bad++; note("%s: a jump %.1fx when it goes end to end", d->name, (double)r); }
    }
    // MIX > DRONE itself, and the song's key
    {   float r = 0;
        for (int dir = 0; dir < 2; dir++) {
            set("mdrn", dir ? 0 : 100); sim_run(0.5f);
            tap_on(0.4f); set("mdrn", dir ? 100 : 0); sim_run(0.2f); tap_off();
            float c = a_click(0.0f); if (c > r) r = c;
        }
        set("mdrn", 85); sim_run(0.5f);
        tap_on(0.4f); set("skey", 2); sim_run(0.2f); tap_off();
        float c = a_click(0.0f); if (c > r) r = c;
        set("skey", 9); sim_run(0.3f);
        tested += 2;
        if (r > worst) worst = r;
        if (r > 3.0f) { bad++; note("MIX > DRONE or KEY: a jump %.1fx", (double)r); }
    }
    check(bad == 0, "no click when any of the drone's params (or MIX > DRONE, or the key) jumps end to end while it plays (%d tries, the worst %.1fx)", tested, (double)worst);
    // everything up: MIX 100, GAIN 100, both voices, the sub, the feedback, the hum, the organ, the wah, the stick
    set("mdrn", 100); set("dgan", 100); set("dvoc", 2); set("dsub", 100); set("dfbk", 100); set("dhum", 100);
    set("dorg", 100); set("dmod", 1); set("dmdp", 100); set("dpnd", 1);
    SI.jx = 1.0f; sim_run(1.0f);
    dr_pre_peak = 0;
    tap_on(8.5f); sim_run(8.0f); tap_off();
    float pk = a_peak(0, 8.0f);
    check(pk < 0.9f && m_bad() == 0, "MIX 100, full gain, everything on and the stick: the amps peak at %.3f (%.3f before the safety net), no NaN", (double)pk, (double)dr_pre_peak);
    metric("drone_peak_all_up", pk);
    metric("drone_peak_before_clip", dr_pre_peak);
    set("dpnd", 0); SI.jx = 0.6f; SI.jy = -0.3f; sim_run(1.0f);
    float us = drone_us();
    SI.jx = 0; SI.jy = 0;
    metric("drone_us_per_block", us);
    check(us < 5.0f, "with everything on the drone takes %.2f us per 64-frame block (budget 5)", (double)us);
    // the organ alone, clean: its cost
    set("dgan", 0); set("dvoc", 0); set("dsub", 0); set("dfbk", 0); set("dhum", 0); set("dmod", 0); set("dorg", 100); sim_run(1.5f);
    float us_org = drone_us();
    note("the organ under a clean amp: %.2f us per block", (double)us_org);
    sound(S_OM); sim_run(0.5f);
}

// ---------------------------------------------------------------- what it sounds like
static void listen(void)
{
    section("drone: WAVs");
    quiet();
    static const struct { int sound; float secs; const char *name; } tour[3] = { { S_SUNN, 30.0f, "sunn" }, { S_DOOM, 30.0f, "doom-daisy" }, { S_ORGAN, 20.0f, "organ" } };
    int ok = 0;
    for (int k = 0; k < 3; k++) {
        sound(tour[k].sound); set("hbld", tour[k].sound == S_DOOM ? 100 : 0); sim_run(0.3f);
        float t0 = sim_t; tap_on(tour[k].secs + 1.0f); sim_run(tour[k].secs); tap_off();
        char b[48]; snprintf(b, sizeof b, "-drone-%s", tour[k].name);
        sim_wav(b, t0, sim_t); wav_solo(tour[k].name);
        float r = a_rms(2.0f, tour[k].secs), p = m_peak(t0, sim_t);
        if (r > 0.01f && p < 0.99f && m_bad() == 0) ok++; else note("%s: amps rms %.4f, mix peak %.3f", INST.presets[tour[k].sound].name, (double)r, (double)p);
        if (k == 0) sim_shot("drone_sunn");
    }
    // the amps alone at full fuzz: SUNN without the monks
    sound(S_SUNN); set("mvoi", 0); set("mbow", 0); sim_run(0.3f);
    float t0 = sim_t; tap_on(31.0f); sim_run(30.0f); tap_off();
    sim_wav("-drone-alone-fuzz", t0, sim_t); wav_solo("alone-fuzz");
    float r = a_rms(2.0f, 30.0f), p = m_peak(t0, sim_t);
    if (r > 0.01f && p < 0.99f && m_bad() == 0) ok++; else note("the amps alone: rms %.4f peak %.3f", (double)r, (double)p);
    check(ok == 4, "SUNN with the monks, DOOM DAISY, ORGAN and the amps alone at full fuzz: all there, under full scale (%d of 4; WAVs in out/)", ok);
    set("hbld", 0); sound(S_OM); sim_run(0.5f);
}

void test_mod_drone(void)
{
    float keep_key = get("skey"), keep_scl = get("sscl"), keep_prg = get("sprg"), keep_hold = get("schd"), keep_bar = get("sbar"), keep_tmp = get("stmp"), keep_bld = get("hbld");
    set("stmp", 100);
    silent();
    drone();
    chords();
    hands_();
    with_wreck();
    clicks_levels_cost();
    listen();
    tap_off(); free(tap); tap = NULL;
    quiet(); sound(S_OM);
    set("skey", keep_key); set("sscl", keep_scl); set("sprg", keep_prg); set("schd", keep_hold); set("sbar", keep_bar); set("stmp", keep_tmp); set("hbld", keep_bld);
    sim_run(0.5f);
}
