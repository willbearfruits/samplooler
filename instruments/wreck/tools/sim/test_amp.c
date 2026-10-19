// AMP's checks (inst_amp.c), run by test_inst.c with the axis at 10 and the controls quiet. They listen to the amp
// alone (inst_amp.c copies its own output here while amp_tap is set) and to the whole mix for the WAVs.
//   in key: the root's pitch is the key at each tuning; the power chord's partials (root, fifth, octave)
//   chugs start on beat_kicks()' steps (within 5 ms); tremolo picking at its rate
//   the drone: struck from silence it is the same every time, it swells in over SWELL, FEEDBACK lifts its 12th to
//   the root's level; a chord change waits for the bar line
//   the dive: an octave down at full; LEVEL 100, full gain and the stick: under 0.9; no clicks at note changes or
//   when a param jumps end to end; a knock is a chord hit; stopped, it fades; the cost; WAVs of each zone and sound
#include "sim.h"
#include "wreck.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t frame; int kind, note; uint32_t kicks; int32_t step; } amp_ev_t;   // as inst_amp.c's
extern float   *amp_tap; extern int amp_tap_cap, amp_tap_n;
extern amp_ev_t amp_evlog[1024]; extern int amp_evn;
extern float    amp_pre_peak, amp_fb_hz, amp_fb_lvl;
void wr_prof(float *us, int n);
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
enum { NT_CHUG, NT_PICK, NT_STAB, NT_RIFF, NT_DRONE, NT_HIT };
#define EV_DRONE 300
#define ZB 10.0f                    // the axis in each zone
#define ZH 37.0f
#define ZS 62.0f
#define ZD 90.0f

static int ptag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = ptag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = ptag(t); return p >= 0 ? pv[p] : 0.0f; }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); }
static void sound(int k) { front_midi(0xC0, (uint8_t)k, 0); sim_run(0.05f); }

// ---------------------------------------------------------------- the amp alone
static float *tap; static uint32_t tap_f0;
static void tap_on(float seconds)
{
    int n = (int)(seconds * AUDIO_SR) + 8192;
    tap = realloc(tap, (size_t)n * sizeof(float));
    amp_tap = tap; amp_tap_cap = n; amp_tap_n = 0; tap_f0 = audio_frames(); amp_evn = 0;
}
static void tap_off(void) { amp_tap = NULL; }
static int ti(float t) { int i = (int)(t * AUDIO_SR); return i < 0 ? 0 : i > amp_tap_n ? amp_tap_n : i; }   // seconds into the tap
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
// the power within +-0.6 % of f (the feedback wobbles +-0.3 % in pitch, as real feedback does, so its line is spread:
// Goertzel bins 0.1 % apart, summed; a window of seconds puts the bins near its own width)
static float a_band(float t0, float t1, float f)
{
    float sum = 0;
    for (int k = -6; k <= 6; k++) sum += a_goertzel(t0, t1, f * (1.0f + 0.001f * (float)k));
    return sum;
}
// the lowest strong partial (a power chord's autocorrelation finds the octave under its root: root and fifth imply it)
static float a_lowest(float t0, float t1, float fmin, float fmax)
{
    int a = ti(t0), b = ti(t1), n = b - a;
    if (n < 1024) return 0;
    float *x = malloc((size_t)n * sizeof(float));
    for (int i = 0; i < n; i++) x[i] = tap[a + i] * (float)(0.5 - 0.5 * cos(2 * M_PI * i / (double)n));
    int steps = (int)(16.0f * 12.0f * log2f(fmax / fmin)) + 1;
    float *pw = malloc((size_t)steps * sizeof(float)), pmax = 0;
    for (int k = 0; k < steps; k++) {
        double f = fmin * pow(2.0, k / 192.0), w = 2 * M_PI * f / AUDIO_SR, c = 2 * cos(w), s1 = 0, s2 = 0;
        for (int i = 0; i < n; i++) { double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
        pw[k] = (float)(s1 * s1 + s2 * s2 - c * s1 * s2);
        if (pw[k] > pmax) pmax = pw[k];
    }
    float f = 0;
    for (int k = 1; k < steps - 1; k++)
        if (pw[k] >= 0.06f * pmax && pw[k] >= pw[k - 1] && pw[k] >= pw[k + 1]) {
            float y0 = log10f(pw[k - 1] + 1e-30f), y1 = log10f(pw[k] + 1e-30f), y2 = log10f(pw[k + 1] + 1e-30f), dd = y0 - 2 * y1 + y2;
            float sh = fabsf(dd) > 1e-9f ? 0.5f * (y0 - y2) / dd : 0.0f;
            f = fmin * powf(2.0f, ((float)k + sh) / 192.0f);
            break;
        }
    free(x); free(pw);
    return f;
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
static void wav_solo(const char *name)                              // out/wreck-amp-<name>.wav: the amp alone
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s-amp-solo-%s.wav", sim_name, name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t sr = AUDIO_SR, br = sr * 2, ds = (uint32_t)amp_tap_n * 2, rs = 36 + ds; uint16_t one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fl = 16; fwrite(&fl, 4, 1, f); fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < amp_tap_n; i++) { float v = clampf(tap[i], -1.0f, 1.0f); int16_t q = (int16_t)(v * 32767.0f); fwrite(&q, 2, 1, f); }
    fclose(f);
}
static void wav_mix(const char *name, float t0) { char b[96]; snprintf(b, sizeof b, "-amp-%s", name); sim_wav(b, t0, sim_t); }

// the amp on its own terms: a held, clean chord, nothing else
static void plain(void)
{
    set("ahrp", 0); set("aorg", 0); set("afbk", 0); set("ahum", 0); set("amod", 0); set("asub", 0); set("avoc", 0);
    set("ascl", 0); set("alvl", 70);
}
static float midi_hz_d(int n) { return 440.0f * powf(2.0f, (float)(n - 69) / 12.0f); }
static int expect_root(int key, int floor_) { return floor_ + ((key - floor_) % 12 + 12) % 12; }
static const int floors[7] = { 40, 38, 37, 36, 35, 33, 31 };

// ---------------------------------------------------------------- in key, the power chord
static void in_key(void)
{
    section("amp: in key");
    quiet(); plain();
    // a held chord at HARDCORE (DRONE ON: the amp's bar is the song's), from a bar line on: chords wait for one
    set("sprg", 0); set("schd", 0); set("adrn", 1); set("agan", 0); set("xaxs", ZH); sim_run(5.0f);
    static const int keys[3] = { 9, 4, 0 }, tunings[3] = { 0, 3, 5 };
    int ok = 0, n = 0;
    for (int k = 0; k < 3; k++) for (int t = 0; t < 3; t++) {
        set("skey", (float)keys[k]); set("atun", (float)tunings[t]);
        sim_run(1.5f);
        tap_on(1.2f); sim_run(1.0f); tap_off();
        int want = expect_root(keys[k], floors[tunings[t]]);
        float hz = a_lowest(0.1f, 1.0f, 25.0f, 400.0f), f = midi_hz_d(want);
        n++;
        if (fabsf(hz / f - 1.0f) < 0.01f) ok++;
        else note("key %d tuning %d: %.1f Hz, want %.1f (MIDI %d)", keys[k], tunings[t], (double)hz, (double)f, want);
    }
    check(ok == n, "the root is the key, at or above the lowest string of each tuning (%d of %d within 1 %%)", ok, n);
    // the partials: root, fifth, octave; not the thirds or the fourth (key A, DROP C: A2, 110 Hz)
    set("skey", 9); set("atun", 3);
    for (int pass = 0; pass < 2; pass++) {
        set("agan", pass ? 100 : 0);
        sim_run(1.5f);
        tap_on(1.2f); sim_run(1.0f); tap_off();
        float f = 110.0f;
        float p1 = a_goertzel(0.1f, 1.0f, f), p5 = a_goertzel(0.1f, 1.0f, 1.5f * f), p8 = a_goertzel(0.1f, 1.0f, 2.0f * f);
        float m3 = a_goertzel(0.1f, 1.0f, 1.1892f * f), M3 = a_goertzel(0.1f, 1.0f, 1.2599f * f), p4 = a_goertzel(0.1f, 1.0f, 1.3348f * f);
        float worst_in = fminf(p1, fminf(p5, p8)), best_out = fmaxf(m3, fmaxf(M3, p4));
        float db = 10.0f * log10f(worst_in / (best_out + 1e-20f));
        check(db > (pass ? 10.0f : 20.0f), "%s: root, fifth and octave stand %.0f dB over the thirds and the fourth",
              pass ? "full gain" : "clean", (double)db);
    }
    set("agan", 70);
}

// ---------------------------------------------------------------- chugs and tremolo picking
static void chugs(void)
{
    section("amp: chugs and tremolo picking");
    quiet(); plain(); set("avoc", 2);
    set("adrn", 2); set("achg", 100); set("atrm", 0); set("agan", 80); set("xaxs", ZH);
    sim_run(2.0f);
    tap_on(8.5f); sim_run(8.0f); tap_off();
    grid_t g; wr_grid_get(&g);
    int nch = 0, on_kick = 0, timed = 0, heard = 0;
    for (int e = 0; e < amp_evn && e < 1024; e++) {
        const amp_ev_t *E = &amp_evlog[e];
        if (E->kind != NT_CHUG) continue;
        nch++;
        if ((E->kicks >> (E->step & 31)) & 1u) on_kick++;
        int32_t d = wr_pos(&g, E->frame) - wr_step_start(&g, E->step % (int32_t)(SONG_WRAP_BEATS * wr_steps_per_beat(g.feel_q)));
        if (abs(d) <= AUDIO_SR / 200) timed++;
        // heard: the amp's own output rises within 5 ms of the step's start
        float t0 = t_of(E->frame) - (float)d / AUDIO_SR;
        float before = a_rms(t0 - 0.012f, t0 - 0.002f), after = a_peak(t0, t0 + 0.005f);
        if (after > 4.0f * before + 0.01f) heard++;
    }
    check(nch >= 4 && on_kick == nch, "HARDCORE: every chug is on one of beat_kicks()' steps (%d of %d)", on_kick, nch);
    check(nch >= 4 && timed == nch, "each starts on its step to the frame (%d of %d within 5 ms)", timed, nch);
    check(nch >= 4 && heard >= nch * 9 / 10, "and is heard within 5 ms of it (%d of %d)", heard, nch);
    wav_solo("chugs");
    // tremolo picking: 1/32 (two picks a step) at x1: a pick every fpb / 8 frames
    set("atrm", 100); set("aprt", 1);
    sim_run(2.6f);                                                 // into a new bar
    tap_on(4.0f); sim_run(3.5f); tap_off();
    float want = (float)g.fpb / 8.0f;
    int np = 0, even = 0; uint32_t last = 0;
    for (int e = 0; e < amp_evn && e < 1024; e++) {
        if (amp_evlog[e].kind != NT_PICK) continue;
        if (np && fabsf((float)(amp_evlog[e].frame - last) - want) <= 1.5f) even++;
        last = amp_evlog[e].frame; np++;
    }
    // the sound: its envelope repeats at the picking rate
    int dec = 32, ne = amp_tap_n / dec;                            // the envelope at 1 kHz
    float *env = calloc((size_t)ne, sizeof(float)); float z = 0;
    for (int i = 0; i < ne * dec; i++) { z += 0.01f * (fabsf(tap[i]) - z); if (i % dec == 0) env[i / dec] = z; }
    // less its drift (a moving average over 300 ms: the strings beat over seconds, and the picks are the fast part)
    float *hp = calloc((size_t)ne, sizeof(float));
    for (int i = 0; i < ne; i++) { double m = 0; int a = i - 150 < 0 ? 0 : i - 150, b = i + 150 > ne ? ne : i + 150; for (int j = a; j < b; j++) m += env[j]; hp[i] = env[i] - (float)(m / (b - a)); }
    double ac[200]; double best = -1; int best_l = 0;
    for (int l = 20; l < 200; l++) {
        double c = 0; for (int i = 200; i + l < ne; i++) c += (double)hp[i] * hp[i + l];
        ac[l] = c; if (c > best) best = c;
    }
    free(hp);
    // the envelope repeats at the pick: the strongest lag near one pick, well over the lag of half a pick
    int lw = (int)(want / 32.0f + 0.5f), lo = lw * 85 / 100, hi = lw * 115 / 100; double near = -1e30;
    for (int l = lo; l <= hi && l < 200; l++) if (ac[l] > near) { near = ac[l]; best_l = l; }
    bool trough = ac[lw / 2] < 0.5 * near;
    free(env);
    float heard_ms = (float)best_l, want_ms = want / (AUDIO_SR / 1000.0f);
    check(np > 20 && even >= np - 2, "tremolo picking: %d picks, %d of them %.0f frames apart (1/32 at x1)", np, even, (double)want);
    check(near >= 0.5 * best && trough && fabsf(heard_ms - want_ms) <= 0.06f * want_ms,
          "and it is heard at that rate: the envelope repeats every %.0f ms (a pick every %.1f ms)", (double)heard_ms, (double)want_ms);
    wav_solo("tremolo");
    set("atrm", 30);
}

// ---------------------------------------------------------------- the drone
// One run, the amp alone tapped for 12 s from the turn. Two ways in: the knob (silent at BREAKCORE, the axis to
// DRONE: on the way the riff may start a note, and the chord slides in from it, as it does in play) or from silence
// (DRONE ON strikes the chord at BREAKCORE, then the axis goes to DRONE): the strings start at rest then, so the run
// is the same whatever came before.
typedef struct {
    float t0;                  // the strike, seconds into the tap
    float w[8];                // rms per half second from it
    float plateau;             // rms over 4 .. 9.5 s
    float sec[6];              // rms per second from 4 s
    float hz;                  // the feedback partial at 10 s
    float fbv1, fbv2;          // the screen's feedback at 2.5 s and 9 s
    float part[8];             // the partials' shares late: 110, 165, 220, 330, 440, 550, 660, 880 Hz
} drone_t;
static void drone_run(bool knob, int voice, float swell, float fbk, float hum, float settle, drone_t *m)
{
    quiet(); plain(); set("avoc", (float)voice); set("asub", 50);
    set("adrn", 2); set("agan", 90); set("afbk", fbk); set("ahum", hum); set("aswl", swell); set("abrk", 0);
    set("xaxs", ZB); sim_run(settle);                              // silent at BREAKCORE
    tap_on(13.0f);
    if (!knob) { set("adrn", 1); sim_run(0.1f); }                  // struck from silence, here
    set("xaxs", ZD);
    float fbv[48]; m->hz = 0;
    for (int k = 0; k < 48; k++) { sim_run(0.25f); amp_view_t v; amp_view(&v); fbv[k] = v.feedback; if (k == 39) m->hz = amp_fb_hz; }
    tap_off();
    m->t0 = -1;
    for (int e = 0; e < amp_evn && e < 1024; e++) if (amp_evlog[e].kind == EV_DRONE) { m->t0 = t_of(amp_evlog[e].frame); break; }
    float t0 = m->t0 < 0 ? 0.0f : m->t0;
    for (int k = 0; k < 8; k++) m->w[k] = a_rms(t0 + 0.5f * (float)k, t0 + 0.5f * (float)(k + 1));
    m->plateau = a_rms(t0 + 4.0f, t0 + 9.5f);
    for (int k = 0; k < 6; k++) m->sec[k] = a_rms(t0 + 4.0f + (float)k, t0 + 5.0f + (float)k);
    float r = a_rms(t0 + 6.5f, t0 + 9.5f);
    { static const float pf[8] = { 110, 165, 220, 330, 440, 550, 660, 880 }; for (int q = 0; q < 8; q++) m->part[q] = a_band(t0 + 6.5f, t0 + 9.5f, pf[q]) / (r * r + 1e-12f); }
    int k1 = (int)((t0 + 2.5f) / 0.25f), k2 = (int)((t0 + 9.0f) / 0.25f);
    if (k2 > 47) k2 = 47;
    m->fbv1 = fbv[k1]; m->fbv2 = fbv[k2];
}
static void drone(void)
{
    section("amp: the drone");
    set("skey", 9); set("atun", 3);                                // A2, 110 Hz: the feedback's 12th is 330 Hz
    drone_t k, a, b, b2, c, d;
    drone_run(true, 2, 3.0f, 80, 40, 2.5f, &k);                    // the knob, as WRECK's sounds have it
    wav_solo("drone"); sim_shot("amp_drone");
    drone_run(false, 2, 3.0f, 100, 40, 2.5f, &a);                  // from silence: the feedback, full
    drone_run(false, 2, 3.0f, 0, 40, 2.5f, &b);                    // the same without it: the feedback's control
    drone_run(false, 2, 3.0f, 0, 40, 2.63f, &b2);                  // ... again, from another moment
    drone_run(false, 1, 3.0f, 0, 0, 2.5f, &c);                     // the bass alone (one saw and a sine: no doubled
    drone_run(false, 1, 12.0f, 0, 0, 2.5f, &d);                    // strings beating): the swell, and the slowest one
    {   const drone_t *runs[6] = { &k, &a, &b, &b2, &c, &d };
        static const char *const nm[6] = { "the knob", "FEEDBACK 100", "FEEDBACK 0", "FEEDBACK 0 again", "bass, SWELL 3", "bass, SWELL 12" };
        for (int r = 0; r < 6; r++) {
            char line[200]; int w = snprintf(line, sizeof line, "%-15s struck %.2f s, rms per half second:", nm[r], (double)runs[r]->t0);
            for (int j = 0; j < 8; j++) w += snprintf(line + w, sizeof line - (size_t)w, " %.3f", (double)runs[r]->w[j]);
            note("%s; settled %.3f", line, (double)runs[r]->plateau);
            if (r != 1 && r != 2) continue;
            w = snprintf(line, sizeof line, "%-15s partials late, dB of the sound: 110 %.1f 165 %.1f 220 %.1f 330 %.1f 440 %.1f 550 %.1f 660 %.1f 880 %.1f", nm[r],
                         (double)(10.0f * log10f(runs[r]->part[0] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[1] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[2] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[3] + 1e-12f)),
                         (double)(10.0f * log10f(runs[r]->part[4] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[5] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[6] + 1e-12f)), (double)(10.0f * log10f(runs[r]->part[7] + 1e-12f)));
            note("%s", line);
        }
    }
    check(k.t0 >= 0.0f && k.t0 < 1.5f, "the knob into DRONE: the chord is struck (%.2f s after the turn)", (double)k.t0);
    // and holds: every second from 4 s on within a quarter of the whole stretch
    float lo = 1e9f, hi = 0;
    for (int j = 0; j < 6; j++) { if (k.sec[j] < lo) lo = k.sec[j]; if (k.sec[j] > hi) hi = k.sec[j]; }
    check(lo > 0.75f * k.plateau && hi < 1.25f * k.plateau, "it holds: 4 to 10 s in, each second's rms is %.3f .. %.3f (%.3f over the stretch)", (double)lo, (double)hi, (double)k.plateau);
    // the same drone every time: the strings start at rest when the chord is struck from silence
    float same = 0;
    for (int j = 0; j < 8; j++) { float dd = fabsf(b2.w[j] / (b.w[j] + 1e-9f) - 1.0f); if (dd > same) same = dd; }
    check(b.t0 >= 0.0f && b2.t0 >= 0.0f && same < 0.03f, "struck from silence at another moment it is the same drone (rms within %.1f %% in every half second)", (double)(same * 100.0f));
    // the swell (on the bass: its level is the swell's): a one-pole over a third of SWELL. At 3 s: half a second in
    // under 0.4 of where it settles, at 1 s about half, at 2 s about 0.8, by 4 s there
    float p = c.plateau;
    bool rising = c.w[0] < 0.4f * p && c.w[1] > 0.35f * p && c.w[1] < 0.7f * p && c.w[2] > c.w[1] && c.w[3] > 0.7f * p && c.w[3] < 0.95f * p && c.w[7] > 0.9f * p;
    check(rising, "SWELL 3 s: it swells in (rms %.3f at 0.5 s, %.3f at 1 s, %.3f at 1.5 s, %.3f at 2 s, %.3f at 4 s; settled %.3f)",
          (double)c.w[0], (double)c.w[1], (double)c.w[2], (double)c.w[3], (double)c.w[7], (double)p);
    float g3 = c.w[7] / (c.w[3] + 1e-9f), g12 = d.w[7] / (d.w[3] + 1e-9f);
    check(g3 < 1.3f && g12 > 1.4f && d.w[3] < 0.55f * d.plateau, "SWELL 12 s is slower: at 2 s it is %.2f of where it gets by 10 s, and from 2 s to 4 s it still grows %.2fx (SWELL 3 s: %.2fx)",
          (double)(d.w[3] / d.plateau), (double)g12, (double)g3);
    // the feedback: a partial of the root (the 12th) that grows until it sings over the chord. The fuzz gives the
    // chord that partial of its own (the root's third harmonic, strong in a clipped wave: 8 dB under the root here), so
    // the measure is the 12th against the root, with FEEDBACK 100 and with 0, in two runs the same in every other way
    float twelfth_on = 10.0f * log10f(a.part[3] / (a.part[0] + 1e-12f)), twelfth_off = 10.0f * log10f(b.part[3] / (b.part[0] + 1e-12f));
    check(fabsf(a.hz / 330.0f - 1.0f) < 0.01f && a.fbv2 > 0.4f && a.fbv1 < a.fbv2 && twelfth_on > -4.0f && twelfth_on - twelfth_off > 4.0f,
          "the feedback grows and sings: FEEDBACK 100 lifts the 12th (%.0f Hz) to %.1f dB of the root 6.5 to 9.5 s in (without: %.1f dB); the screen's feedback %.2f -> %.2f",
          (double)a.hz, (double)twelfth_on, (double)twelfth_off, (double)a.fbv1, (double)a.fbv2);
    set("adrn", 2);
}

// ---------------------------------------------------------------- chords move on the bar line
static void chord_change(void)
{
    section("amp: a chord change waits for the bar line");
    quiet(); plain();
    set("sprg", 0); set("schd", 0); set("skey", 9); set("atun", 3); set("adrn", 1); set("agan", 40);
    static const float axes[2] = { ZH, ZD };
    for (int c = 0; c < 2; c++) {
        set("schd", 0); set("xaxs", axes[c]); sim_run(2.5f);
        grid_t g; wr_grid_get(&g);
        int spb = wr_steps_per_beat(g.feel_q), bar_steps = spb * song.bar_beats < 16 ? 16 : spb * song.bar_beats;
        // change it a third of the way into a bar
        int32_t pos = wr_pos(&g, audio_frames()), st = wr_step_of(&g, pos);
        int32_t next_bar = (st / bar_steps + 1) * bar_steps;
        int32_t at = wr_step_start(&g, next_bar - bar_steps * 2 / 3);
        while (wr_pos(&g, audio_frames()) < at) sim_tick();
        int before = -1; { amp_view_t v; amp_view(&v); before = v.note; }
        set("schd", 3);                                            // IV: D
        uint32_t f_set = audio_frames();
        uint32_t bar_frame = g.origin + (uint32_t)wr_step_start(&g, next_bar);
        float t_change = -1;
        for (int k = 0; k < 1100; k++) {
            sim_tick();
            amp_view_t v; amp_view(&v);
            if (v.note >= 0 && v.note != before) { t_change = (float)(int32_t)(audio_frames() - bar_frame) / AUDIO_SR; break; }
        }
        float waited = (float)(bar_frame - f_set) / AUDIO_SR;
        check(t_change >= -0.0001f && t_change < 0.02f && before >= 0,
              "%s: set %.2f s before the bar line, the root moves on it (%+.1f ms; %d -> IV)", host_zone_name(c ? Z_DRONE : Z_HARDCORE),
              (double)waited, (double)(t_change * 1000.0f), before);
    }
    set("schd", 0); sim_run(0.5f);
}

// ---------------------------------------------------------------- the dive, the level
static void dive_level(void)
{
    section("amp: the dive and the level");
    // one chord held (progression HOLD, chord I), so a bar line between the two measurements can't move the root
    float keep_prg = get("sprg"), keep_hold = get("schd");
    quiet(); plain(); set("sprg", 0); set("schd", 0); set("adrn", 1); set("agan", 0); set("skey", 9); set("atun", 3); set("xaxs", ZD);
    sim_run(2.0f);
    // measured on the chord's fifth: the root is saws a few cents apart, and their beating can sink it below the
    // finder's floor for the length of a window (the fifth is there either way)
    tap_on(1.2f); sim_run(1.0f); tap_off();
    float f0 = a_lowest(0.1f, 1.0f, 20.0f, 400.0f);
    float q0 = f0 > 0 ? a_lowest(0.1f, 1.0f, f0 * 1.4f, f0 * 1.6f) : 0;
    SI.jy = -1.0f; sim_run(0.5f);
    float dv = perf.dive;
    tap_on(1.2f); sim_run(1.0f); tap_off();
    float q1 = f0 > 0 ? a_lowest(0.1f, 1.0f, f0 * 0.7f, f0 * 0.8f) : 0;
    SI.jy = 0.0f; set("sprg", keep_prg); set("schd", keep_hold); sim_run(0.5f);
    check(q0 > 0 && fabsf(q1 / q0 - 0.5f) < 0.006f, "the stick down, full (dive %.2f): an octave lower (the root %.1f Hz; its fifth %.1f Hz -> %.1f Hz)",
          (double)dv, (double)f0, (double)q0, (double)q1);
    // everything up: LEVEL 100, GAIN 100, the stick's drive, both voices, the sub, feedback, hum, the harpsichord, the wah
    set("alvl", 100); set("agan", 100); set("avoc", 2); set("asub", 100); set("afbk", 100); set("ahum", 100);
    set("ahrp", 100); set("aorg", 100); set("amod", 1); set("amdp", 100); set("adrn", 2); set("achg", 100); set("atrm", 50);
    SI.jx = 1.0f;
    static const float axes[4] = { ZB, ZH, ZS, ZD };
    float worst = 0, pre = 0;
    for (int z = 0; z < 4; z++) {
        set("abrk", 2); set("xaxs", axes[z]); sim_run(1.0f);
        amp_pre_peak = 0;
        tap_on(6.5f); sim_run(6.0f); tap_off();
        float p = a_peak(0, 6.0f);
        if (p > worst) worst = p;
        if (amp_pre_peak > pre) pre = amp_pre_peak;
    }
    SI.jx = 0.0f;
    check(worst < 0.9f, "LEVEL 100, full gain and the stick's drive, everything on: peak %.3f in every zone (%.3f before the safety clip)",
          (double)worst, (double)pre);
    metric("amp_peak_all_up", worst);
    metric("amp_peak_before_clip", pre);
    plain(); set("agan", 70); set("amdp", 60); set("abrk", 0);
}

// ---------------------------------------------------------------- clicks
static void clicks(void)
{
    section("amp: clicks");
    quiet(); plain(); set("avoc", 2); set("asub", 50); set("adrn", 2);
    // note changes: riffs with slides and bends, chugs, tremolo picking; clean (the most revealing) and fuzzed
    static const char *what[4] = { "riffs", "chugs", "tremolo", "drone" };
    static const float axes[4] = { ZS, ZH, ZH, ZD };
    int bad = 0, total = 0; float worst = 0;
    for (int gain = 0; gain < 2; gain++) for (int w = 0; w < 4; w++) {
        set("agan", gain ? 80 : 0);
        set("atrm", w == 2 ? 100 : 0); set("achg", 100); set("arif", (float)(gain ? 2 : 1));
        set("xaxs", axes[w]); sim_run(2.0f);
        tap_on(6.0f); sim_run(5.5f); tap_off();
        for (int e = 0; e < amp_evn && e < 1024; e++) {
            float t = t_of(amp_evlog[e].frame);
            if (t < 0.05f || t > 5.4f) continue;
            float r = a_click(t);
            total++;
            if (r > worst) worst = r;
            if (r > 3.0f) { bad++; if (bad < 6) note("%s %s: a jump %.1fx at %.3f s", gain ? "fuzzed" : "clean", what[w], (double)r, (double)t); }
        }
    }
    check(total > 50 && bad == 0, "no click where a note starts or changes (%d notes, the worst %.1fx; 3 is a click)", total, (double)worst);
    // every AMP param from one end to the other while it plays (riffs at SLUDGE, the drone at DRONE)
    int tested = 0; bad = 0; worst = 0;
    for (int zz = 0; zz < 2; zz++) {
        sound(zz ? 8 : 6);
        set("xaxs", zz ? ZD : ZS); sim_run(4.0f);
        for (int p = mod_base[M_AMP]; p < mod_base[M_AMP] + MOD_PARAMS_MAX; p++) {
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
            if (r > 3.0f) { bad++; note("%s at %s: a jump %.1fx when it goes end to end", d->name, zz ? "DRONE" : "SLUDGE", (double)r); }
        }
    }
    check(bad == 0, "no click when any of the amp's params jumps end to end while it plays (%d tries, the worst %.1fx)", tested, (double)worst);
    sound(0);
}

// ---------------------------------------------------------------- a knock, stop and start
static void hands(void)
{
    section("amp: a knock, stop and start");
    quiet(); sound(0); set("xaxs", ZS); sim_run(3.0f);
    tap_on(1.5f);
    amp_hit(0.9f); sim_run(1.0f); tap_off();
    int hits = 0; for (int e = 0; e < amp_evn && e < 1024; e++) if (amp_evlog[e].kind == NT_HIT) hits++;
    set("xaxs", ZB); sim_run(2.0f);
    tap_on(1.0f); amp_hit(0.9f); sim_run(0.5f); tap_off();
    int hits_b = 0; for (int e = 0; e < amp_evn && e < 1024; e++) if (amp_evlog[e].kind == NT_HIT) hits_b++;
    check(hits == 1 && hits_b == 0, "a knock at SLUDGE is a chord hit (%d); at BREAKCORE it leaves the amp alone (%d)", hits, hits_b);
    // stop: what rings fades and nothing new starts; start: it comes back
    set("xaxs", ZD); sim_run(5.0f);
    tap_on(16.0f);
    sim_run(1.0f);
    float on = a_rms(0.2f, 1.0f);
    sim_rec(0.1f);
    int waited = 0;
    while (perf.running && waited < 600) { sim_tick(); waited++; }          // the tape brakes to a halt first (FX)
    float t_stop = (float)(int32_t)(audio_frames() - tap_f0) / AUDIO_SR;
    sim_run(3.0f);
    bool stopped = !perf.running;
    float off = a_rms(t_stop + 2.0f, t_stop + 3.0f);
    sim_rec(0.1f); sim_run(3.0f);
    tap_off();
    float back = a_rms(t_stop + 5.0f, t_stop + 6.0f);
    check(stopped && off < 0.01f * on && back > 0.2f * on, "REC tapped: once stopped the drone fades (rms %.4f of %.3f 2 s on), tapped again it comes back (%.3f)",
          (double)off, (double)on, (double)back);
    set("xaxs", ZB); sim_run(0.5f);
}

// ---------------------------------------------------------------- the cost
static void cost(void)
{
    section("amp: cost");
    quiet(); sound(5);
    set("avoc", 2); set("asub", 100); set("afbk", 100); set("ahum", 100); set("ahrp", 100); set("aorg", 100);
    set("amod", 1); set("amdp", 100); set("agan", 100); set("atrm", 100); set("abrk", 2);
    static const float axes[4] = { ZB, ZH, ZS, ZD };
    float worst = 0; int wz = 0;
    for (int z = 0; z < 4; z++) {
        set("xaxs", axes[z]); sim_run(2.5f);
        SI.jx = 0.6f; SI.jy = -0.3f;
        float best = 1e9f;
        for (int run = 0; run < 3; run++) {
            float junk[8]; wr_prof(junk, 8);
            sim_run(1.0f);
            float parts[8]; wr_prof(parts, 8);
            float us = parts[M_AMP] * 0.96f;
            if (us < best) best = us;
        }
        SI.jx = 0; SI.jy = 0;
        note("%s: AMP %.2f us per block (everything on)", host_zone_name(z), (double)best);
        if (best > worst) { worst = best; wz = z; }
    }
    metric("amp_us_per_block", worst);
    check(worst < 5.0f, "the busiest zone (%s) takes %.2f us per 64-frame block (budget 5)", host_zone_name(wz), (double)worst);
    sound(0);
}

// ---------------------------------------------------------------- what it sounds like
static void listen(void)
{
    section("amp: WAVs");
    quiet();
    static const float axes[4] = { ZB, ZH, ZS, ZD };
    static const char *zn[4] = { "breakcore", "hardcore", "sludge", "drone" };
    sound(0);
    set("xaxs", ZB); sim_run(2.0f);
    for (int z = 0; z < 4; z++) {
        set("xaxs", axes[z]); sim_run(1.0f);
        float t0 = sim_t; tap_on(10.0f); sim_run(9.0f); tap_off();
        char b[48]; snprintf(b, sizeof b, "zone-%s", zn[z]);
        wav_mix(b, t0); wav_solo(b);
    }
    // a tour: GRIND at HARDCORE, BAROQUE at BREAKCORE, WIZARD and EARTH at SLUDGE
    static const struct { int sound; float axis; const char *name; } tour[4] = {
        { 4, ZH, "grind-hardcore" }, { 5, ZB, "baroque-breakcore" }, { 6, ZS, "wizard-sludge" }, { 7, ZS, "earth-sludge" } };
    for (int k = 0; k < 4; k++) {
        sound(tour[k].sound); set("xaxs", tour[k].axis); sim_run(2.0f);
        float t0 = sim_t; tap_on(10.0f); sim_run(9.0f); tap_off();
        wav_mix(tour[k].name, t0); wav_solo(tour[k].name);
    }
    // each sound's DRONE end
    int ok = 0;
    for (int k = 0; k < WR_NSOUNDS; k++) {
        sound(k); set("xaxs", ZS); sim_run(1.0f); set("xaxs", ZD); sim_run(1.5f);
        float t0 = sim_t; tap_on(12.0f); sim_run(11.0f); tap_off();
        char b[48]; snprintf(b, sizeof b, "sound-%d-%s", k, INST.presets[k].name);
        for (char *c = b; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        wav_mix(b, t0); wav_solo(b);
        float r = a_rms(1.0f, 11.0f), p = a_peak(0.0f, 11.0f);
        if (r > 0.01f && p < 0.9f) ok++; else note("%s at DRONE: amp rms %.4f peak %.3f", INST.presets[k].name, (double)r, (double)p);
    }
    check(ok == WR_NSOUNDS, "every sound's DRONE end has the amp in it, under 0.9 (%d of %d; WAVs in out/)", ok, WR_NSOUNDS);
    sound(0); set("xaxs", ZB); sim_run(0.5f);
}

void test_mod_amp(void)
{
    float keep_key = get("skey"), keep_scl = get("sscl"), keep_prg = get("sprg"), keep_hold = get("schd"), keep_tmp = get("stmp");
    set("stmp", 100);
    in_key();
    chugs();
    drone();
    chord_change();
    dive_level();
    clicks();
    hands();
    listen();
    cost();
    tap_off(); free(tap); tap = NULL;
    quiet(); sound(0);
    set("skey", keep_key); set("sscl", keep_scl); set("sprg", keep_prg); set("schd", keep_hold); set("stmp", keep_tmp);
    set("xaxs", ZB); sim_run(0.5f);
}
