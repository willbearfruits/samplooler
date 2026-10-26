// The Samplooler's twin: the firmware's own tune.c, motion.c, gesture.c, rig.c, recorder_f32.c, fx.c, ui.c and oled.c,
// built on the PC behind the same SysEx as the board (bytes in on stdin, out on stdout). The rig's daemon starts it
// when no board is plugged in, so every part of the rig runs without one.
//
//   twin [--scene still|demo] [--fast] [--secs N] [--wav out.wav]   the board's stand-in (10 ms ticks, real time)
//   twin --calc                                                     the daemon's calculator (below)
//
// What it is not: the loop below stands in for main.c's. Menus, slots, the voice and the flock are not here; the
// motion is synthetic (or injected by the rig), mounted the way the default axis map says.
//
// --calc reads commands on stdin, one a line, and answers one line each:
//   defaults | set I V | curve N SPD_MIN SPD_MAX | replay IN OUT RATE ACC_LSB GYRO_LSB SPD_MIN SPD_MAX | quit
// replay reads raw chip samples ("ax ay az gx gy gz" a line) and writes one CSV line per control tick.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tune.h"
#include "motion.h"
#include "gesture.h"
#include "rig.h"
#include "rig_codec.h"
#include "recorder.h"
#include "fx.h"
#include "ui.h"
#include "oled.h"
#include "audio.h"

int64_t twin_now_us;

// ---------------------------------------------------------------- out: the SysEx stream on stdout
static uint8_t outbuf[1 << 17];
static int outn;
bool rig_port_send(const uint8_t *m, int n)
{
    if (outn + n > (int)sizeof outbuf) return false;
    memcpy(outbuf + outn, m, (size_t)n); outn += n;
    return true;
}
uint32_t rig_port_ms(void) { return (uint32_t)(twin_now_us / 1000); }
static void flush_out(void)
{
    int k = 0;
    while (k < outn) { ssize_t w = write(1, outbuf + k, (size_t)(outn - k)); if (w <= 0) { if (errno == EINTR) continue; exit(0); } k += (int)w; }
    outn = 0;
}
static void send_text(uint8_t cmd, const char *s)
{
    uint8_t b[600]; int k = 0;
    b[k++] = 0xF0; b[k++] = 0x7D; b[k++] = cmd;
    for (; *s && k < (int)sizeof b - 1; s++) b[k++] = (uint8_t)*s & 0x7F;
    b[k++] = 0xF7;
    rig_port_send(b, k);
}
static void tlog(const char *fmt, ...)
{
    char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    send_text('L', b);
}
void oled_on_flush(const uint8_t *fb) { rig_screen(fb); }

// ---------------------------------------------------------------- the audio the twin "hears": a beat to record
static int8_t scope[AUDIO_SCOPE_N];
static int scope_w, scope_dec;
void audio_scope(int8_t *dst) { for (int i = 0; i < AUDIO_SCOPE_N; i++) dst[i] = scope[(scope_w + i) % AUDIO_SCOPE_N]; }
static uint32_t rs = 1;
static float frand(void) { rs = rs * 1664525u + 1013904223u; return (float)(rs >> 8) / 16777216.0f; }
static float gauss(void) { float u = frand() + 1e-7f, v = frand(); return sqrtf(-2.0f * logf(u)) * cosf(6.2831853f * v); }
static float source(uint64_t n)
{
    float t = (float)(n % 32000) / 32000.0f, b = fmodf(t, 0.25f), h = fmodf(t + 0.125f, 0.25f);
    float kick = sinf(6.2831853f * (55.0f + 90.0f * expf(-b * 40.0f)) * b) * expf(-b * 14.0f);
    float hat = (frand() * 2 - 1) * expf(-h * 80.0f) * 0.25f;
    float pad = 0.08f * sinf(6.2831853f * 220.0f * (float)n / 32000.0f) * (0.6f + 0.4f * sinf(6.2831853f * 0.5f * (float)n / 32000.0f));
    return 0.6f * kick + hat + pad;
}

// ---------------------------------------------------------------- the twin's settings (a few of the board's)
enum { T_MODE, T_PITCH, T_GRAIN, T_SNAP, T_TILT, T_SMAX, T_SMIN, T_VOL, T_N };
static const char *SNAME[T_N] = { "Mode", "Pitch", "Grain", "Snap", "Tilt", "Speed max", "Speed min", "Volume" };
static const char *SGROUP[T_N] = { "PLAY", "PLAY", "PLAY", "PLAY", "TILT", "TILT", "TILT", "SOUND" };
static const char *SHELP[T_N] = { "TAPE: the pitch follows the speed. GRAIN: they are apart.", "Semitones, in GRAIN only.",
    "The grain's length, in GRAIN only. Short is smooth, long is choppy.", "Speeds rounded to octaves and fifths.",
    "Off: it plays forward at 1x, whatever the angle.", "The speed at 45 degrees.", "The crawl when flat. 0 stops the tape.",
    "Above 100% it can clip." };
static struct { int mode, pitch_st, grain_ms, snap, tilt, vol_pct; float spd_max, spd_min; } S = { 0, 0, 50, 0, 1, 100, 2.0f, 0.05f };
static void s_step(int p, int d)
{
    switch (p) {
    case T_MODE: S.mode = !S.mode; break;
    case T_PITCH: S.pitch_st += d; if (S.pitch_st < -24) S.pitch_st = -24; if (S.pitch_st > 24) S.pitch_st = 24; break;
    case T_GRAIN: S.grain_ms += 10 * d; if (S.grain_ms < 20) S.grain_ms = 20; if (S.grain_ms > 200) S.grain_ms = 200; break;
    case T_SNAP: S.snap = !S.snap; break;
    case T_TILT: S.tilt = !S.tilt; break;
    case T_SMAX: S.spd_max += 0.25f * (float)d; if (S.spd_max < 0.25f) S.spd_max = 0.25f; if (S.spd_max > 8) S.spd_max = 8; break;
    case T_SMIN: S.spd_min += 0.05f * (float)d; if (S.spd_min < 0) S.spd_min = 0; if (S.spd_min > 1) S.spd_min = 1; break;
    case T_VOL: S.vol_pct += 5 * d; if (S.vol_pct < 0) S.vol_pct = 0; if (S.vol_pct > 150) S.vol_pct = 150; break;
    }
    recorder_set_mode(S.mode); recorder_set_grain_ms(S.grain_ms); recorder_set_volume((float)S.vol_pct / 100.0f);
}
static void s_text(int p, char *b, int n)
{
    switch (p) {
    case T_MODE: snprintf(b, n, "%s", S.mode ? "GRAIN" : "TAPE"); break;
    case T_PITCH: snprintf(b, n, S.mode ? "%+d st" : "= speed", S.pitch_st); break;
    case T_GRAIN: snprintf(b, n, "%d ms", S.grain_ms); break;
    case T_SNAP: snprintf(b, n, "%s", S.snap ? "ON" : "OFF"); break;
    case T_TILT: snprintf(b, n, "%s", S.tilt ? "ON" : "OFF"); break;
    case T_SMAX: snprintf(b, n, "%.2fx", (double)S.spd_max); break;
    case T_SMIN: snprintf(b, n, "%.2fx", (double)S.spd_min); break;
    case T_VOL: snprintf(b, n, "%d%%", S.vol_pct); break;
    default: snprintf(b, n, "?");
    }
}
static void s_value(int p) { char t[32], b[64]; s_text(p, t, sizeof t); snprintf(b, sizeof b, "tag=p%02d\ntext=%s", p, t); send_text('V', b); }

// ---------------------------------------------------------------- the motion: a pose over time, made into sensor samples
static const char *scene = "still";
static bool scene_rec;                      // the demo holds REC
static void pose(double t, float *pitch, float *roll, float *yaw, float *shake)
{
    *pitch = *roll = *yaw = *shake = 0;
    if (!strcmp(scene, "demo")) {
        double c = fmod(t, 32.0);
        scene_rec = c >= 2.0 && c < 4.5;
        float e;
        #define RAMP(a, b) ((float)((c - (a)) / ((b) - (a))))
        if (c < 5) { }
        else if (c < 8)  { e = RAMP(5, 8);   *pitch = 32.0f * sinf(3.14159f * e); }            // nose down: forward, faster
        else if (c < 11) { e = RAMP(8, 11);  *pitch = -28.0f * sinf(3.14159f * e); }           // nose up: backwards
        else if (c < 14) { e = RAMP(11, 14); *roll = -38.0f * sinf(3.14159f * e); }            // roll left: the delay
        else if (c < 17) { e = RAMP(14, 17); *roll = 34.0f * sinf(3.14159f * e); }             // roll right: the stutter
        else if (c < 20) { e = RAMP(17, 20); *pitch = 14.0f * sinf(6.2831853f * 3.0f * (float)(c - 17)) * sinf(3.14159f * e); }   // flicks
        else if (c < 26) { e = RAMP(20, 26); *pitch = -50.0f + 100.0f * e; *yaw = 40.0f * sinf(3.14159f * e); }   // a slow sweep
        else if (c < 28) { e = RAMP(26, 28); *shake = 0.8f * sinf(3.14159f * e); }             // a shake
        *yaw += 10.0f * sinf((float)t * 0.2f);
        #undef RAMP
    }
}
// the device's orientation as a quaternion: tilt from the up vector, then the yaw about the world's z
static void pose_q(float pitch, float roll, float yaw, float q[4])
{
    const float d = 0.0174533f;
    float th = pitch * d, ph = roll * d;
    float ux = -sinf(ph) * cosf(th), uy = sinf(th), uz = cosf(th) * cosf(ph);
    float n = sqrtf(ux * ux + uy * uy + uz * uz); ux /= n; uy /= n; uz /= n;
    float a[4] = { 1 + uz, uy, -ux, 0 };
    float an = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    for (int k = 0; k < 4; k++) a[k] /= an;
    float c = cosf(yaw * d * 0.5f), s = sinf(yaw * d * 0.5f);   // (c,0,0,s) * a
    q[0] = c * a[0] - s * a[3]; q[1] = c * a[1] - s * a[2]; q[2] = c * a[2] + s * a[1]; q[3] = c * a[3] + s * a[0];
}
static void qmul(const float *a, const float *b, float *o)
{
    o[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    o[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    o[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    o[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}
// one sensor sample at time t: raw LSB in the chip's axes (mounted as the default axis map says), with noise and bias
static const float GBIAS[3] = { 0.9f, -0.6f, 0.4f }, AOFF[3] = { 0.010f, -0.006f, 0.020f };
static void sample_at(double t, float dt, float acc_lsb, float gyro_lsb, int16_t out[6])
{
    float p0, r0, y0, sh, p1, r1, y1, sh1, q0[4], q1[4];
    pose(t, &p0, &r0, &y0, &sh);
    pose(t + (double)dt, &p1, &r1, &y1, &sh1);
    pose_q(p0, r0, y0, q0); pose_q(p1, r1, y1, q1);
    float qc[4] = { q0[0], -q0[1], -q0[2], -q0[3] }, dq[4];
    qmul(qc, q1, dq);
    if (dq[0] < 0) for (int k = 0; k < 4; k++) dq[k] = -dq[k];
    float w[3] = { 2 * dq[1] / dt * 57.2958f, 2 * dq[2] / dt * 57.2958f, 2 * dq[3] / dt * 57.2958f };   // body rates, deg/s
    float up[3] = { 2 * (q0[1] * q0[3] - q0[0] * q0[2]), 2 * (q0[0] * q0[1] + q0[2] * q0[3]),
                    q0[0] * q0[0] - q0[1] * q0[1] - q0[2] * q0[2] + q0[3] * q0[3] };
    if (sh > 0) for (int k = 0; k < 3; k++) up[k] += sh * sinf((float)t * 6.2831853f * (7.0f + 2.0f * (float)k)) * 1.5f;
    // device to chip: x = device y, y = -device x, z = device z
    float ac[3] = { up[1], -up[0], up[2] }, gc[3] = { w[1], -w[0], w[2] };
    for (int k = 0; k < 3; k++) {
        float a = (ac[k] + AOFF[k] + 0.004f * gauss()) * acc_lsb, g = (gc[k] + GBIAS[k] + 0.06f * gauss()) * gyro_lsb;
        out[k] = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, roundf(a)));
        out[3 + k] = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, roundf(g)));
    }
}
static float acc_lsb_now(void) { static const float L[] = { 16384, 8192, 4096, 2048 }; return L[TI(TU_IMU_ARANGE) & 3]; }
static float gyro_lsb_now(void) { static const float L[] = { 131, 65.5f, 32.8f, 16.4f }; return L[TI(TU_IMU_GRANGE) & 3]; }
static int rate_now(void) { static const int R[] = { 100, 250, 500, 1000 }; return R[TI(TU_IMU_RATE) & 3]; }

// ---------------------------------------------------------------- SysEx in
static bool midi_rec, freeze_req;
static void handle_sysex(const uint8_t *m, int n)
{
    if (n < 2 || m[0] != 0x7D) return;
    uint8_t cmd = m[1];
    const uint8_t *body = m + 2; int bn = n - 2;
    if (rig_sysex(cmd, body, bn)) return;
    char t[64]; int tn = bn < 63 ? bn : 63;
    for (int i = 0; i < tn; i++) t[i] = (char)body[i];
    t[tn] = 0;
    if (cmd == 'I') {
        char b[160];
        snprintf(b, sizeof b, "name=Samplooler twin\nversion=twin\nserial=TWIN\nproto=1\nparams=%d\nrig=1", T_N);
        send_text('I', b);
    } else if (cmd == 'D') {
        for (int p = 0; p < T_N; p++) {
            char tx[32], b[300]; s_text(p, tx, sizeof tx);
            snprintf(b, sizeof b, "i=%d\ntag=p%02d\nname=%s\ngroup=%s\nkind=step\ntext=%s\nhelp=%s", p, p, SNAME[p], SGROUP[p], tx, SHELP[p]);
            send_text('D', b);
        }
        char b[24]; snprintf(b, sizeof b, "end=%d", T_N); send_text('D', b);
    } else if (cmd == 'P' && t[0] == 'p') {
        int p = atoi(t + 1); char *eq = strchr(t, '=');
        if (p >= 0 && p < T_N && eq) { int d = atoi(eq + 1); if (d) s_step(p, d > 0 ? 1 : -1); s_value(p); }
    } else if (cmd == 'S') {
        tlog("diag: twin, up %.1f s, rig drops %u", (double)twin_now_us / 1e6, (unsigned)rig_drops());
    } else if (cmd == 'R') tlog("twin: no download mode here");
}
static void handle_midi3(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) == 0x90 && d2 > 0) { if (d1 == 60) midi_rec = true; else if (d1 == 64) freeze_req = true; }
    else if ((st & 0xF0) == 0x80 || ((st & 0xF0) == 0x90 && d2 == 0)) { if (d1 == 60) midi_rec = false; }
}
static void read_input(void)
{
    static uint8_t sx[256]; static int sxn; static bool in_sx;
    static uint8_t st, d[2]; static int nd;
    uint8_t buf[4096];
    for (;;) {
        ssize_t r = read(0, buf, sizeof buf);
        if (r == 0) exit(0);                                  // the daemon has gone
        if (r < 0) return;
        for (ssize_t i = 0; i < r; i++) {
            uint8_t b = buf[i];
            if (b == 0xF0) { in_sx = true; sxn = 0; continue; }
            if (b == 0xF7) { if (in_sx) handle_sysex(sx, sxn); in_sx = false; continue; }
            if (in_sx) { if (b & 0x80) in_sx = false; else if (sxn < (int)sizeof sx) sx[sxn++] = b; continue; }
            if (b & 0x80) { st = b; nd = 0; continue; }
            if (!st) continue;
            d[nd++] = b;
            if (nd == 2) { nd = 0; handle_midi3(st, d[0], d[1]); }
        }
    }
}

// ---------------------------------------------------------------- the calculator (--calc)
static int calc(void)
{
    tune_defaults();
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        char cmd[32] = { 0 };
        sscanf(line, "%31s", cmd);
        if (!strcmp(cmd, "quit")) break;
        else if (!strcmp(cmd, "defaults")) { tune_defaults(); printf("ok\n"); }
        else if (!strcmp(cmd, "set")) { int i; float v; if (sscanf(line, "%*s %d %f", &i, &v) == 2) printf("ok %g\n", (double)tune_set(i, v)); else printf("err set I V\n"); }
        else if (!strcmp(cmd, "curve")) {
            int n = 121; float mn = 0.05f, mx = 2.0f;
            sscanf(line, "%*s %d %f %f", &n, &mn, &mx);
            if (n < 2) n = 2;
            if (n > 2001) n = 2001;
            printf("{\"tilt\":{\"angle\":[");
            for (int k = 0; k < n; k++) printf("%s%.3f", k ? "," : "", -90.0 + 180.0 * k / (n - 1));
            printf("],\"speed\":[");
            float sign = TI(TU_TI_SIGN) ? 1.0f : -1.0f;
            for (int k = 0; k < n; k++) {
                gest_reset();
                float a = -90.0f + 180.0f * (float)k / (float)(n - 1);
                printf("%s%.5f", k ? "," : "", (double)gest_speed(a * sign / TV(TU_TI_FULL), mn, mx, NULL));
            }
            printf("],\"detent\":[");
            for (int k = 0; k < n; k++) {
                gest_reset();
                float a = -90.0f + 180.0f * (float)k / (float)(n - 1), dt = 0;
                gest_speed(a * sign / TV(TU_TI_FULL), mn, mx, &dt);
                printf("%s%.3f", k ? "," : "", (double)dt);
            }
            printf("],\"dead\":%g,\"full\":%g},\"roll\":{\"angle\":[", (double)TV(TU_TI_DEAD), (double)TV(TU_TI_FULL));
            for (int k = 0; k < n; k++) printf("%s%.3f", k ? "," : "", -90.0 + 180.0 * k / (n - 1));
            float dl[2001], su[2001];
            for (int k = 0; k < n; k++) {
                tilt_out_t o; gest_reset();
                gest_tilt(0, -90.0f + 180.0f * (float)k / (float)(n - 1), false, true, false, 0, mn, mx, &o);
                dl[k] = o.delay; su[k] = o.stutter;
            }
            printf("],\"delay\":[");
            for (int k = 0; k < n; k++) printf("%s%.4f", k ? "," : "", (double)dl[k]);
            printf("],\"stutter\":[");
            for (int k = 0; k < n; k++) printf("%s%.4f", k ? "," : "", (double)su[k]);
            printf("],\"dead\":%g,\"full\":%g}}\n", (double)TV(TU_RO_DEAD), (double)TV(TU_RO_FULL));
            gest_reset();
        }
        else if (!strcmp(cmd, "replay")) {
            char in[512], out[512]; int rate; float al, gl, mn, mx;
            if (sscanf(line, "%*s %511s %511s %d %f %f %f %f", in, out, &rate, &al, &gl, &mn, &mx) != 7) { printf("err replay IN OUT RATE ACC_LSB GYRO_LSB SPD_MIN SPD_MAX\n"); fflush(stdout); continue; }
            FILE *fi = fopen(in, "r"), *fo = fopen(out, "w");
            if (!fi || !fo) { printf("err cannot open\n"); if (fi) fclose(fi); if (fo) fclose(fo); fflush(stdout); continue; }
            motion_init(); gest_reset();
            fprintf(fo, "t,pitch,roll,yaw,speed,u,zone,detent,delay,stutter,rest,bias_x,bias_y,bias_z,qw,qx,qy,qz\n");
            double t = 0, next_tick = 0, ctl = TI(TU_MO_RATE) ? 0.01 : 0.02;
            int ticks = 0; char l[256];
            float dt = 1.0f / (float)rate;
            while (fgets(l, sizeof l, fi)) {
                int v[6];
                if (sscanf(l, "%d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) continue;
                float a[3] = { v[0] / al, v[1] / al, v[2] / al }, g[3] = { v[3] / gl, v[4] / gl, v[5] / gl };
                motion_sample(a, g, dt);
                t += (double)dt;
                if (t + 1e-9 >= next_tick) {
                    next_tick += ctl;
                    motion_tick(false);
                    const motion_t *m = motion_get();
                    tilt_out_t o;
                    gest_tilt(m->pitch, m->roll, true, true, false, 0, mn, mx, &o);
                    fprintf(fo, "%.4f,%.4f,%.4f,%.4f,%.5f,%.5f,%d,%.3f,%.4f,%.4f,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f\n",
                            t, (double)m->pitch, (double)m->roll, (double)m->yaw, (double)o.speed, (double)o.u, o.zone, (double)o.detent,
                            (double)o.delay, (double)o.stutter, m->rest ? 1 : 0, (double)m->bias[0], (double)m->bias[1], (double)m->bias[2],
                            (double)m->q[0], (double)m->q[1], (double)m->q[2], (double)m->q[3]);
                    ticks++;
                }
            }
            fclose(fi); fclose(fo);
            printf("ok %d\n", ticks);
        }
        else printf("err unknown\n");
        fflush(stdout);
    }
    return 0;
}

// ---------------------------------------------------------------- the twin
static void ev_to_rig(const char *name, float v, int ms, const char *text) { rig_event(name, v, ms, text); }

int main(int argc, char **argv)
{
    bool fast = false; double secs = 0; const char *wav = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--calc")) return calc();
        else if (!strcmp(argv[i], "--fast")) fast = true;
        else if (!strcmp(argv[i], "--scene") && i + 1 < argc) scene = argv[++i];
        else if (!strcmp(argv[i], "--secs") && i + 1 < argc) secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
    }
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    tune_defaults();
    gest_on_event(ev_to_rig);
    rig_init();
    motion_init();
    fx_init();
    oled_init(NULL);
    if (!recorder_init()) { fprintf(stderr, "twin: recorder_init failed\n"); return 1; }
    s_step(-1, 0);

    FILE *fw = NULL;
    if (wav) { fw = fopen(wav, "wb"); if (fw) { uint8_t h[44] = { 0 }; fwrite(h, 1, 44, fw); } }
    uint64_t nframe = 0, wav_n = 0;
    int tick = 0, held_n = 0, click_ms = 0, vol_ms = 0, toast = 0, param = 0, enc_total = 0;
    bool held = false, menu = false, menu_edit = false, click_menu = false, frozen = false, injected = false;
    float speed_cmd = 1.0f, meter_in = 0, meter_out = 0;
    tilt_out_t to = { .speed = 1.0f };
    double samp_acc = 0;
    struct timespec next; clock_gettime(CLOCK_MONOTONIC, &next);
    tlog("twin up: scene %s, %s", scene, fast ? "fast" : "real time");

    for (;;) {
        if (!fast) {
            next.tv_nsec += 10000000; if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        }
        twin_now_us += 10000; tick++;
        double t = (double)twin_now_us / 1e6;
        if (secs > 0 && t > secs) break;
        read_input();
        rig_tick(10);

        // the motion: injected by the rig, or the scene's
        static int16_t raw[256][6];
        int n = rig_inject(raw, 256);
        injected = n >= 0;
        int rate = rate_now();
        float al = acc_lsb_now(), gl = gyro_lsb_now();
        if (!injected) {
            samp_acc += rate * 0.01;
            n = 0;
            while (samp_acc >= 1.0 && n < 256) { sample_at(t - 0.01 + (double)n / rate, 1.0f / (float)rate, al, gl, raw[n]); n++; samp_acc -= 1.0; }
        }
        for (int i = 0; i < n; i++) {
            float a[3] = { raw[i][0] / al, raw[i][1] / al, raw[i][2] / al }, g[3] = { raw[i][3] / gl, raw[i][4] / gl, raw[i][5] / gl };
            motion_sample(a, g, 1.0f / (float)rate);
        }
        rig_motion((const int16_t (*)[6])raw, n, (uint32_t)nframe, TI(TU_IMU_ARANGE) | TI(TU_IMU_GRANGE) << 2, (injected ? 1 : 0) | 4);

        // the controls: the rig's keys (the twin has no pins), the scene's REC, MIDI note 60
        rig_keys_t k; rig_keys(&k, TI(TU_EN_STEPS), TI(TU_EN_DIR) ? 1 : -1);
        enc_out_t e; gest_enc(k.counts, k.sw > 0, twin_now_us, &e);
        enc_total += e.enc;
        bool down = (k.rec >= 0 ? k.rec > 0 : scene_rec) || midi_rec;
        int deb = TI(TU_RE_DEB);
        held_n = down ? (held_n < deb ? held_n + 1 : deb) : (held_n > 0 ? held_n - 1 : 0);
        bool now = held_n >= deb ? true : held_n == 0 ? false : held;
        if (now != held) { held = now; recorder_set_record(held); gest_emit(held ? "rec.down" : "rec.up", 0, 0, ""); }
        if (e.tap) {
            if (!menu) {
                if (click_ms > 0) { click_ms = 0; vol_ms = TI(TU_EN_VOL_MS); toast = vol_ms; param = T_VOL; gest_emit("enc.double", 0, 0, "volume"); }
                else { click_ms = TI(TU_EN_DOUBLE) > 10 ? TI(TU_EN_DOUBLE) : 10; click_menu = vol_ms <= 0; if (vol_ms > 0) { vol_ms = 0; toast = 0; } }
            } else menu_edit = !menu_edit;
        }
        if (e.hold && menu) { menu = false; menu_edit = false; gest_emit("menu.close", 0, 0, ""); }
        else if (e.hold) freeze_req = true;
        if (click_ms > 0 && (click_ms -= 10) <= 0) { click_ms = 0; if (click_menu && !menu) { menu = true; menu_edit = false; gest_emit("menu.open", 0, 0, ""); } }
        if (vol_ms > 0) vol_ms -= 10;
        if (e.enc) {
            if (menu && !menu_edit) param = ((param + e.enc) % T_N + T_N) % T_N;
            else { s_step(vol_ms > 0 && !menu ? T_VOL : param, e.enc); if (!menu) toast = 1500; s_value(param); }
        }
        if (freeze_req) { freeze_req = false; frozen = !frozen; gest_emit(frozen ? "freeze.on" : "freeze.off", 0, 0, ""); }

        bool ctl_tick = TI(TU_MO_RATE) == 1 || tick % 2 == 0;
        if (ctl_tick) {
            motion_tick(frozen);
            const motion_t *mo = motion_get();
            gest_tilt(mo->pitch, mo->roll, S.tilt, true, false, 0, S.spd_min, S.spd_max, &to);
            speed_cmd = to.speed;
            fx_set_delay(to.delay); fx_set_stutter(to.stutter);
            if (S.snap) speed_cmd = gest_snap(speed_cmd);
            recorder_set_speed(speed_cmd);
            recorder_set_pitch(exp2f((float)S.pitch_st / 12.0f));
        }

        // the audio: 10 ms in 64-frame blocks, the beat into the recorder and the effects
        float ipk = 0, opk = 0;
        for (int b = 0; b < 5; b++) {
            float in[64], out[64];
            for (int f = 0; f < 64; f++) {
                in[f] = source(nframe + (uint64_t)f) * 0.7f;
                if (fabsf(in[f]) > ipk) ipk = fabsf(in[f]);
                if (++scope_dec >= 8) { scope_dec = 0; scope[scope_w] = (int8_t)fmaxf(-127.0f, fminf(127.0f, in[f] * 127.0f)); scope_w = (scope_w + 1) % AUDIO_SCOPE_N; }
            }
            recorder_process(in, out, 64);
            fx_process(out, 64);
            for (int f = 0; f < 64; f++) {
                if (fabsf(out[f]) > opk) opk = fabsf(out[f]);
                if (fw) { int16_t s = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, out[f] * 29000.0f)); fwrite(&s, 2, 1, fw); wav_n++; }
            }
            nframe += 64;
        }
        if (fw) fflush(fw);                                   // the daemon reads the growing file (audio.measure)
        meter_in = ipk > meter_in ? ipk : meter_in * 0.9f;
        meter_out = opk > meter_out ? opk : meter_out * 0.9f;
        if (toast > 0) toast -= 10;

        int s = recorder_state();
        static int last_state = -1;
        if (s != last_state) { static const char *SN[] = { "idle", "rec", "play" }; gest_emit("take.state", (float)s, 0, SN[s]); last_state = s; }
        if (ctl_tick && rig_state_on()) {
            const motion_t *mo = motion_get();
            float v[RS_N] = { 0 };
            int fl = (frozen ? RF_FROZEN : 0) | (menu ? RF_MENU : 0) | (menu_edit ? RF_MENU_EDIT : 0) | (injected ? RF_INJECTED : 0)
                   | (held ? RF_REC_DOWN : 0) | RF_IMU | (mo->rest ? RF_REST : 0) | (e.pressed ? RF_ENC_DOWN : 0)
                   | (s == REC_RECORDING ? RF_RECORDING : 0) | (s == REC_PLAYING ? RF_PLAYING : 0);
            v[RS_state] = (float)s; v[RS_mode] = (float)S.mode; v[RS_flags] = (float)fl;
            v[RS_qw] = mo->q[0]; v[RS_qx] = mo->q[1]; v[RS_qy] = mo->q[2]; v[RS_qz] = mo->q[3];
            v[RS_pitch] = mo->pitch; v[RS_roll] = mo->roll; v[RS_yaw] = mo->yaw;
            v[RS_tilt_u] = to.u; v[RS_zone] = (float)to.zone; v[RS_detent] = to.detent;
            v[RS_speed_cmd] = speed_cmd; v[RS_speed_now] = recorder_speed_now(); v[RS_pos] = recorder_position();
            v[RS_loop_s] = recorder_loop_seconds(); v[RS_delay] = to.delay; v[RS_stutter] = to.stutter;
            v[RS_enc] = (float)(((enc_total + 8192) & 16383) - 8192);
            v[RS_in_peak] = meter_in; v[RS_out_peak] = meter_out; v[RS_cpu] = 0; v[RS_heap_kb] = 0;
            v[RS_bias_x] = mo->bias[0]; v[RS_bias_y] = mo->bias[1]; v[RS_bias_z] = mo->bias[2];
            v[RS_rate_hz] = (float)rate; v[RS_menu_param] = (float)param;
            rig_state(v, (uint32_t)nframe);
        }
        if (tick % 5 == 0) {
            char pv[32]; int shown = vol_ms > 0 && !menu ? T_VOL : param; s_text(shown, pv, sizeof pv);
            const motion_t *mo = motion_get();
            ui_t u = {
                .state = s, .mode = S.mode, .speed_now = recorder_speed_now(), .speed_cmd = speed_cmd, .spd_max = S.spd_max,
                .pitch_ratio = 1.0f, .tilt_pitch = TI(TU_TI_AXIS) ? mo->roll : -mo->pitch, .tilt_roll = TI(TU_TI_AXIS) ? -mo->pitch : mo->roll,
                .has_imu = true, .tilt_on = S.tilt, .frozen = frozen, .pos = recorder_position(), .rec_fill = recorder_record_fill(),
                .loop_s = recorder_loop_seconds(), .max_s = recorder_max_seconds(), .meter_in = meter_in, .meter_out = meter_out,
                .cpu = 0, .menu = menu, .menu_edit = menu_edit, .param = shown, .param_n = T_N,
                .param_name = SNAME[shown], .param_val = pv, .param_frac = 0.5f, .toast_ms = toast,
            };
            u.fx_delay = fx_delay_active(); u.fx_stutter = fx_stutter_active();
            ui_draw(&u);
        }
        if (tick % 100 == 0) {
            const motion_t *mo = motion_get();
            tlog("main: %s speed %+5.2f pitch %+5.1f roll %+5.1f%s", s == REC_RECORDING ? "REC " : s == REC_PLAYING ? "PLAY" : "IDLE",
                 (double)recorder_speed_now(), (double)mo->pitch, (double)mo->roll, injected ? " (injected)" : "");
        }
        flush_out();
    }
    if (fw) {
        uint32_t data = (uint32_t)(wav_n * 2), riff = data + 36, sr = 32000, br = 64000;
        uint8_t h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0,0,0,0, 0,0,0,0, 2,0, 16,0, 'd','a','t','a', 0,0,0,0 };
        memcpy(h + 4, &riff, 4); memcpy(h + 24, &sr, 4); memcpy(h + 28, &br, 4); memcpy(h + 40, &data, 4);
        fseek(fw, 0, SEEK_SET); fwrite(h, 1, 44, fw); fclose(fw);
    }
    flush_out();
    return 0;
}
