#include "sim.h"
#include "looper.h"
#include "oled.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

sim_in_t SI;
float *sim_out, *sim_mic;
int    sim_n;
float  sim_t;
const char *sim_name;
static int cap;
static uint32_t frames_now;
static int ticks, held_ms;
midi_ev_t midi_log[65536];
int midi_n;
uint8_t sim_led[3];

// ---------------------------------------------------------------- the platform, on the host
void *app_alloc(size_t n) { return calloc(1, n); }
void *app_alloc_fast(size_t n) { return calloc(1, n); }
void app_free(void *p) { free(p); }
uint32_t audio_frames(void) { return frames_now; }
void app_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("        [log] "); vprintf(fmt, ap); printf("\n"); va_end(ap); }
void plat_midi3(uint8_t st, uint8_t d1, uint8_t d2) { if (midi_n < 65536) midi_log[midi_n++] = (midi_ev_t){ sim_t, st, d1, d2 }; }
void plat_led(uint8_t r, uint8_t g, uint8_t b) { sim_led[0] = r; sim_led[1] = g; sim_led[2] = b; }
int  sim_mic_db = 12;
void plat_mic_gain(int db) { sim_mic_db = db; }
bool sim_speaker = true;
void plat_speaker(bool on) { sim_speaker = on; }
bool sim_rec_button = true;
void plat_rec_button(bool on) { sim_rec_button = on; }
static uint8_t users[8][2048]; static int user_n[8];
bool plat_save_user(int k, const uint8_t *b, int n) { if (k < 0 || k > 7 || n > 2048) return false; memcpy(users[k], b, (size_t)n); user_n[k] = n; return true; }
int  plat_load_user(int k, uint8_t *b, int cap_) { if (k < 0 || k > 7 || user_n[k] > cap_) return 0; memcpy(b, users[k], (size_t)user_n[k]); return user_n[k]; }
void plat_factory_reset(void) { memset(user_n, 0, sizeof user_n); }
char sim_sysex[256][720]; int sim_sysex_n;      // the app's line: what went out, as text after F0 7D (the letter first)
bool sim_sysex_off;                             // the host stops reading
bool plat_sysex(const uint8_t *m, int n)
{
    if (sim_sysex_off) return false;
    if (n < 3 || m[0] != 0xF0 || m[n - 1] != 0xF7) return false;
    char *t = sim_sysex[sim_sysex_n % 256];
    int k = 0;
    for (int i = 2; i < n - 1 && k < 719; i++) t[k++] = (char)m[i];
    t[k] = 0;
    sim_sysex_n++;
    return true;
}
const char *plat_version(void) { return "0.1.0-sim"; }
const char *plat_serial(void) { return "SM-00000000"; }
int  plat_about(char l[][32], int max)
{
    int n = 0;
    if (n < max) snprintf(l[n++], 32, "VERSION 0.1.0 (SIM)");
    if (n < max) snprintf(l[n++], 32, "CPU 12%% (PEAK 15%%)  DROPS 0");
    if (n < max) snprintf(l[n++], 32, "RAM 210K  PSRAM 980K FREE");
    if (n < max) snprintf(l[n++], 32, "UP 0:01:23  ID 7275FBD4");
    if (n < max) snprintf(l[n++], 32, "TILT YES  STICK YES  USB ON");
    return n;
}

static uint32_t nseed = 0x12345u;
float sim_noise(void) { nseed ^= nseed << 13; nseed ^= nseed >> 17; nseed ^= nseed << 5; return (float)(int32_t)nseed / 2147483648.0f; }

// ---------------------------------------------------------------- checks and results
int checks_run, checks_failed;
static char cur_section[48] = "core";
static FILE *results;
static int nres;
void section(const char *name) { snprintf(cur_section, sizeof cur_section, "%s", name); printf("\n== %s\n", name); }
static void json_str(FILE *f, const char *s) { fputc('"', f); for (; *s; s++) { if (*s == '"' || *s == '\\') fputc('\\', f); fputc(*s, f); } fputc('"', f); }
void check(bool ok, const char *fmt, ...)
{
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", b);
    checks_run++; if (!ok) checks_failed++;
    if (results) { fprintf(results, "%s\n  {\"section\": ", nres++ ? "," : ""); json_str(results, cur_section); fprintf(results, ", \"ok\": %s, \"check\": ", ok ? "true" : "false"); json_str(results, b); fprintf(results, "}"); }
}
void note(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("        "); vprintf(fmt, ap); printf("\n"); va_end(ap); }
void metric(const char *key, double v)
{
    printf("        %s = %.4g\n", key, v);
    if (results) { fprintf(results, "%s\n  {\"section\": ", nres++ ? "," : ""); json_str(results, cur_section); fprintf(results, ", \"metric\": "); json_str(results, key); fprintf(results, ", \"value\": %.6g}", v); }
}

// ---------------------------------------------------------------- frames
static int shot_no;
static uint8_t *gif_buf; static int gif_n, gif_cap; static bool gif_on; static char gif_clip[64];

static void dump_pbm(const char *fn)
{
    FILE *f = fopen(fn, "w"); if (!f) return;
    fprintf(f, "P1\n%d %d\n", OLED_W, OLED_H);
    for (int y = 0; y < OLED_H; y++) { for (int x = 0; x < OLED_W; x++) fputc(oled_get(x, y) ? '1' : '0', f); fputc('\n', f); }
    fclose(f);
}
void sim_shot(const char *label)
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s/%02d_%s.pbm", sim_name, ++shot_no, label);
    dump_pbm(fn);
}
void sim_gif_begin(const char *clip) { snprintf(gif_clip, sizeof gif_clip, "%s", clip); gif_n = 0; gif_on = true; }
void sim_gif_end(void)
{
    gif_on = false;
    char fn[256]; snprintf(fn, sizeof fn, "out/%s-%s.frames", sim_name, gif_clip);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    fwrite(gif_buf, 1, (size_t)gif_n * (OLED_W * OLED_H / 8), f); fclose(f);
    char cmd[512]; snprintf(cmd, sizeof cmd, "python3 gif.py %s out/%s-%s.gif && rm -f %s", fn, sim_name, gif_clip, fn);
    if (system(cmd)) printf("  (gif failed)\n");
}
static void gif_frame(void)
{
    const int fb = OLED_W * OLED_H / 8;
    if (gif_n >= 1500) return;                                  // 60 s at 25 fps
    if ((gif_n + 1) * fb > gif_cap) { gif_cap = (gif_n + 64) * fb; gif_buf = realloc(gif_buf, (size_t)gif_cap); }
    uint8_t *p = gif_buf + gif_n * fb;
    memset(p, 0, (size_t)fb);
    for (int y = 0; y < OLED_H; y++) for (int x = 0; x < OLED_W; x++) if (oled_get(x, y)) p[(y * OLED_W + x) >> 3] |= (uint8_t)(1 << (x & 7));
    gif_n++;
}

// ---------------------------------------------------------------- the simulation
static void wav(const char *fn, const float *x, int n)
{
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t sr = AUDIO_SR, br = sr * 2, ds = (uint32_t)n * 2, rs = 36 + ds; uint16_t one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fl = 16; fwrite(&fl, 4, 1, f); fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < n; i++) { float v = x[i]; if (!(v == v)) v = 0; v = v > 1 ? 1 : v < -1 ? -1 : v; int16_t q = (int16_t)(v * 32767.0f); fwrite(&q, 2, 1, f); }
    fclose(f);
}
void sim_wav(const char *suffix, float t0, float t1)
{
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR);
    if (a < 0) a = 0;
    if (b > sim_n) b = sim_n;
    if (b <= a) return;
    char fn[256]; snprintf(fn, sizeof fn, "out/%s%s.wav", sim_name, suffix);
    wav(fn, sim_out + a, b - a);
}

// The sim's IMU is a right-handed one fitted with x to the right and z out of the screen, so its y points
// to the top edge: screen y is its -y (meta.axis 1, -2, 3, what tilt setup finds for it).
static vec3_t imu_axes(vec3_t s) { return (vec3_t){ s.x, -s.y, s.z }; }
static vec3_t burst_raw[64];

__attribute__((weak)) void sim_tick_hook(void) { }

void sim_tick(void)
{
    sim_tick_hook();
    for (int i = 0; i < SI.nburst && i < 64; i++) burst_raw[i] = imu_axes(SI.burst[i]);
    float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
    for (int b = 0; b < TICK_FRAMES / AUDIO_BLOCK; b++) {
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            float t = (float)(frames_now + (uint32_t)i) / (float)AUDIO_SR;
            in[i] = SI.mic ? SI.mic(t, SI.mic_u) : 0.0f;
        }
        front_audio(in, out, AUDIO_BLOCK);
        if (sim_n + AUDIO_BLOCK > cap) { cap = cap * 2 + AUDIO_SR; sim_out = realloc(sim_out, (size_t)cap * sizeof(float)); sim_mic = realloc(sim_mic, (size_t)cap * sizeof(float)); }
        memcpy(sim_out + sim_n, out, sizeof out); memcpy(sim_mic + sim_n, in, sizeof in);
        sim_n += AUDIO_BLOCK;
        frames_now += AUDIO_BLOCK;
    }
    sim_t = (float)frames_now / (float)AUDIO_SR;
    held_ms = SI.pressed ? held_ms + 10 : 0;
    front_in_t fi = {
        .enc = SI.enc, .tap = SI.tap, .pressed = SI.pressed, .held_ms = held_ms, .rec = SI.rec,
        .imu_ok = SI.imu, .acc_raw = imu_axes(SI.acc), .gyro_raw = imu_axes(SI.gyro), .burst_raw = burst_raw, .nburst = SI.nburst,
        .joy_ok = SI.joy, .jx = SI.jx, .jy = SI.jy, .dt = 0.01f, .frames = frames_now,
    };
    front_tick(&fi);
    SI.enc = 0; SI.tap = false; SI.nburst = 0;
    if (++ticks % 4 == 0) { front_draw(); if (gif_on) gif_frame(); }
}

void sim_run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
void sim_turn(int d) { SI.enc = d; sim_tick(); }
// A click in play opens the menu only once the double-click window has passed (front.c's DOUBLE_MS): wait it out,
// so a test that clicks and then turns finds the menu open, as a player would after the same click.
void sim_tap(void)
{
    bool opens = front_screen() == 1 && front_menu_level() == 0;
    SI.pressed = true; sim_run(0.05f); SI.pressed = false; SI.tap = true; sim_tick();
    if (opens) sim_run(0.35f);
}
void sim_double_tap(void)
{
    SI.pressed = true; sim_run(0.05f); SI.pressed = false; SI.tap = true; sim_tick();
    sim_run(0.1f);
    SI.pressed = true; sim_run(0.05f); SI.pressed = false; SI.tap = true; sim_tick();
}
void sim_hold_knob(void) { SI.pressed = true; sim_run(0.7f); SI.pressed = false; sim_tick(); }
void sim_rec(float s) { SI.rec = true; sim_run(s); SI.rec = false; sim_tick(); }
void sim_tilt(float lx, float ly)
{
    // lean_x = asin(-acc.x), lean_y = asin(-acc.y)
    float ax = -sinf(lx * 0.0174533f), ay = -sinf(ly * 0.0174533f);
    float az2 = 1.0f - ax * ax - ay * ay;
    SI.acc = (vec3_t){ ax, ay, az2 > 0 ? sqrtf(az2) : 0.0f };
}

void sim_boot(bool show_splash)
{
    char nm[32]; int i = 0;
    for (; INST.name[i] && i < 31; i++) nm[i] = (char)tolower((unsigned char)INST.name[i]);
    nm[i] = 0;
    sim_name = strdup(nm);
    mkdir("out", 0755);
    char d[128]; snprintf(d, sizeof d, "out/%s", sim_name); mkdir(d, 0755);
    char cmd[200]; snprintf(cmd, sizeof cmd, "rm -f out/%s/*.pbm out/%s*.gif out/%s*.wav", sim_name, sim_name, sim_name); if (system(cmd)) { }
    results = fopen("out/results.json", "w");
    if (results) fprintf(results, "{\"instrument\": \"%s\", \"results\": [", INST.name);
    SI.imu = true; SI.joy = true; SI.acc = (vec3_t){ 0, 0, 1 };
    params_init();
    meta.axis[0] = 1; meta.axis[1] = -2; meta.axis[2] = 3; meta.calibrated = 1;
    front_init(true, false, false);
    if (show_splash) {
        sim_gif_begin("splash");
        for (int k = 0; k < 600 && front_screen() == 0; k++) sim_tick();
        sim_gif_end();
    } else { SI.tap = true; sim_tick(); }
}

void sim_finish(void)
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s.wav", sim_name); wav(fn, sim_out, sim_n);
    snprintf(fn, sizeof fn, "python3 render.py out/%s.png out/%s >/dev/null", sim_name, sim_name);
    if (shot_no && system(fn)) printf("  (contact sheet failed)\n");
    if (results) { fprintf(results, "\n], \"checks\": %d, \"failed\": %d}\n", checks_run, checks_failed); fclose(results); }
}

// ---------------------------------------------------------------- measurements
static int idx(float t) { int i = (int)(t * AUDIO_SR); return i < 0 ? 0 : i > sim_n ? sim_n : i; }
float m_rms(float t0, float t1)
{
    int a = idx(t0), b = idx(t1);
    double e = 0; for (int i = a; i < b; i++) e += (double)sim_out[i] * sim_out[i];
    return b > a ? (float)sqrt(e / (b - a)) : 0.0f;
}
float m_peak(float t0, float t1)
{
    int a = idx(t0), b = idx(t1);
    float p = 0; for (int i = a; i < b; i++) { float v = fabsf(sim_out[i]); if (v > p) p = v; }
    return p;
}
int m_bad(void) { int n = 0; for (int i = 0; i < sim_n; i++) { uint32_t u; memcpy(&u, &sim_out[i], 4); if ((u & 0x7f800000u) == 0x7f800000u) n++; } return n; }

float m_pitch(const float *x, int n, float fmin, float fmax)
{
    int lmin = (int)(AUDIO_SR / fmax), lmax = (int)(AUDIO_SR / fmin);
    if (lmax >= n / 2) lmax = n / 2 - 1;
    static double r[8192];
    double e0 = 0; for (int i = 0; i < n - lmax; i++) e0 += (double)x[i] * x[i];
    if (e0 < 1e-9) return 0;
    double best = 0;
    for (int l = lmin; l <= lmax && l < 8192; l++) {
        double c = 0, e1 = 0;
        for (int i = 0; i < n - lmax; i++) { c += (double)x[i] * x[i + l]; e1 += (double)x[i + l] * x[i + l]; }
        r[l] = c / sqrt(e0 * e1 + 1e-12);
        if (r[l] > best) best = r[l];
    }
    if (best < 0.3) return 0;
    for (int l = lmin + 1; l < lmax && l < 8191; l++)
        if (r[l] >= 0.85 * best && r[l] >= r[l - 1] && r[l] >= r[l + 1]) {
            double a = r[l - 1], b = r[l], c = r[l + 1], d = a - 2 * b + c;
            double sh = fabs(d) > 1e-12 ? 0.5 * (a - c) / d : 0;
            return (float)(AUDIO_SR / (l + sh));
        }
    return 0;
}
float m_pitch_at(float t0, float t1, float fmin, float fmax) { int a = idx(t0), b = idx(t1); return b > a ? m_pitch(sim_out + a, b - a, fmin, fmax) : 0.0f; }
float m_goertzel(float t0, float t1, float f)
{
    int a = idx(t0), b = idx(t1);
    double w = 2 * M_PI * f / AUDIO_SR, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = a; i < b; i++) { double s0 = sim_out[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return (float)(p / ((double)(b - a) * (b - a) + 1));
}
// A click is a jump in the first difference right where a control changed, much bigger than the
// signal's own jumps just before and a little after.
float m_click(float tc, float *spike, float *ctx)
{
    float sp = 0, cx = 0;
    int a = idx(tc), b = idx(tc + 0.006f);
    for (int i = a > 0 ? a : 1; i < b; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > sp) sp = d; }
    int c0 = idx(tc - 0.10f), c1 = idx(tc - 0.004f), c2 = idx(tc + 0.04f), c3 = idx(tc + 0.14f);
    for (int i = c0 > 0 ? c0 : 1; i < c1; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > cx) cx = d; }
    for (int i = c2; i < c3; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > cx) cx = d; }
    if (spike) *spike = sp;
    if (ctx) *ctx = cx;
    return sp / (cx + 0.004f);
}
float hz_to_midi(float f) { return f > 0 ? 69.0f + 12.0f * log2f(f / 440.0f) : -1.0f; }
