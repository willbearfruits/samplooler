// A long random performance through the Samplooler's audio path on the host (see run.sh): the recorder, the roll
// effects, the voice (SING's engines), the flock with one pretend board. Every 10 ms the harness plays main.c's part
// (the controls, the loop cue, the key and scale news), then five 64-frame blocks as audio.c runs them. Checks on
// every block: no NaN or inf anywhere on the way out, nothing far over full scale, the head inside the loop, the
// speed finite and settling on what was asked, a take that was long enough always ends in a playing loop (no stuck
// state), and the head never far from the beat while the speed sits on 1x with Snap on (no drift).
//
//   soak [hours]   simulated hours (default 2); exit 1 on any failure
#include "recorder.h"
#include "fx.h"
#include "voice.h"
#include "flock.h"
#include "link.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR 32000
// ---------------------------------------------------------------- the pretend room: one WRECK with a tempo
typedef struct { link_state_t *st; uint8_t mac[6]; double off_us, ppm, epoch; bool on; } peer_t;
static peer_t P;
static int cur = -1;
static bool board_on;
static const uint8_t board_mac[6] = { 0x90, 0xDA, 0x72, 0x76, 0x00, 0xD0 };
static uint32_t seed = 0xc0ffee;
static double rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (double)(seed % 1000000u) / 1e6; }
static double now_true(void) { return (double)recorder_frames() * 31.25 + 1e6; }
static double clk(int k, double t) { return k < 0 ? t : P.off_us + t * (1.0 + P.ppm * 1e-6); }
typedef struct { double t; int to; uint8_t from[6]; int n; uint8_t b[256]; } air_t;
static air_t air[64];
static int nair;
// (app_log is voice.c's)
bool plat_link_start(int power) { (void)power; if (cur < 0) board_on = true; else P.on = true; return true; }
void plat_link_stop(void) { if (cur < 0) board_on = false; else P.on = false; }
bool plat_link_send(const void *b, int n)
{
    int to = cur < 0 ? 0 : -1;
    if (!(to < 0 ? board_on : P.on) || nair >= 64) return true;
    air_t *a = &air[nair++];
    a->t = now_true() + 300.0 + rnd() * 2000.0; a->to = to; memcpy(a->from, cur < 0 ? board_mac : P.mac, 6);
    a->n = n; memcpy(a->b, b, (size_t)n);
    return true;
}
int64_t plat_link_us(void) { return (int64_t)clk(cur, now_true()); }
int64_t plat_link_epoch(void) { return cur < 0 ? 1000000 : (int64_t)P.epoch; }
void plat_link_mac(uint8_t mac[6]) { memcpy(mac, cur < 0 ? board_mac : P.mac, 6); }
static void use(int k) { cur = k; link_use(k < 0 ? NULL : P.st); }

// ---------------------------------------------------------------- the performance
static int    fails;
static double hours = 2.0;
static long   ticks, blocks, takes, loops, loads, scrubs, voices_changed;
static float  worst_peak, worst_drift;
static int    rec_hold_ticks, state_was = -1, closing_ticks;
static uint32_t loop_seen = 0xFFFFFFFFu;
static int    take_len_ticks;
static bool   snap_setting, expect_loop;
static float  speed_cmd = 1.0f, speed_goal = 1.0f;
static int    mode = REC_MODE_TAPE, key, scale, voice;
static int    slots_n[8];
static int16_t *slots[8];
static float  sing_ph, sing_f0 = 200.0f, nz;
static int    ticks_at_1x;

static void fail(const char *fmt, ...)
{
    if (fails < 20) { va_list a; va_start(a, fmt); printf("  FAIL at %.1f s: ", (double)ticks / 100.0); vprintf(fmt, a); printf("\n"); va_end(a); }
    fails++;
}
static inline bool finite_f(float x) { uint32_t b; memcpy(&b, &x, 4); return (b & 0x7f800000u) != 0x7f800000u; }

static void mic(float *out, int n)
{
    for (int i = 0; i < n; i++) {
        sing_ph += sing_f0 / SR; if (sing_ph >= 1) sing_ph -= 1;
        nz = nz * 0.98f + (float)(rnd() - 0.5) * 0.02f;
        float saw = 1.0f - 2.0f * sing_ph;
        out[i] = 0.25f * saw * (0.6f + 0.4f * sinf((float)ticks * 0.013f)) + nz;
    }
}

static void every_tick(void)
{
    // the controls, at random
    double r = rnd();
    if (recorder_state() == REC_RECORDING && rec_hold_ticks > 0) rec_hold_ticks--;
    else if (recorder_state() != REC_RECORDING && r < 0.004) {       // a take now and then, 0.05 .. 12 s
        rec_hold_ticks = (int)(5 + rnd() * rnd() * 1200);
        take_len_ticks = rec_hold_ticks;
        expect_loop = take_len_ticks >= 11;                            // 0.1 s plus the seam
        takes++;
    }
    bool rec = rec_hold_ticks > 0;
    if (rnd() < 0.002) { speed_goal = (float)(rnd() * 16.0 - 8.0); scrubs++; }
    if (rnd() < 0.01) speed_goal = 1.0f;
    speed_cmd += 0.1f * (speed_goal - speed_cmd);
    if (rnd() < 0.001) snap_setting = !snap_setting;
    if (rnd() < 0.0005) { mode = mode == REC_MODE_TAPE ? REC_MODE_GRAIN : REC_MODE_TAPE; recorder_set_mode(mode); }
    if (rnd() < 0.001) recorder_set_grain_ms(20 + (int)(rnd() * 180));
    if (rnd() < 0.001) recorder_set_pitch(exp2f((float)(rnd() * 4.0 - 2.0)));
    if (rnd() < 0.001) recorder_set_volume((float)(rnd() * 1.5));
    if (rnd() < 0.0005) { voice = (int)(rnd() * VOICE_N); voices_changed++; vox_select(voice, key, scale); }
    if (rnd() < 0.0005) { key = (int)(rnd() * 12); scale = (int)(rnd() * VOICE_SCALES); vox_select(voice, key, scale); flock_harm_set(key, scale); }
    if (rnd() < 0.0003) sing_f0 = 100.0f + (float)rnd() * 300.0f;
    float sc = speed_cmd;
    if (snap_setting) {
        static const float grid[] = { 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f };
        float a = fabsf(sc), best = grid[0], bd = 1e9f;
        if (a >= 0.02f) { for (unsigned i = 0; i < 11; i++) { float d = fabsf(a - grid[i]) / grid[i]; if (d < bd) { bd = d; best = grid[i]; } } sc = sc < 0 ? -best : best; }
    }
    recorder_set_speed(sc);
    float roll = (float)(rnd() * 90.0 - 45.0);
    float amt = fabsf(roll) < 15 ? 0 : (fabsf(roll) - 15) / 30; if (amt > 1) amt = 1;
    if (rnd() < 0.5) { fx_set_delay(roll < 0 ? amt : 0); fx_set_stutter(roll > 0 ? amt : 0); }
    // a slot: save the take, or load one back (as main.c's jobs do, without the flash)
    if (recorder_state() != REC_RECORDING && rnd() < 0.0004) {
        int k = (int)(rnd() * 8);
        if (rnd() < 0.5) { int n = 0; const int16_t *t = recorder_take(&n); if (t && n > 0) { free(slots[k]); slots[k] = malloc((size_t)n * 2); memcpy(slots[k], t, (size_t)n * 2); slots_n[k] = n; } }
        else if (slots_n[k] > 0) {
            int max = 0; int16_t *b = recorder_load_begin(&max);
            if (b) { flock_loop_gone(); int n = slots_n[k] < max ? slots_n[k] : max; memcpy(b, slots[k], (size_t)n * 2); int le = flock_snap_len(n); recorder_load_end(n, le, le > 0); loads++; }
        }
    }
    // the pretend board, the flock, the recorder
    double t = now_true();
    for (int i = 0; i < nair;) {
        if (air[i].t <= t + 10000.0) { int k = air[i].to; use(k); link_rx(air[i].from, air[i].b, air[i].n, (int64_t)clk(k, air[i].t), -45); air[i] = air[--nair]; }
        else i++;
    }
    use(0); link_tick(0.01f); use(-1);
    flock_tick(0.01f, snap_setting);
    int k2, s2;
    if (flock_harm_news(&k2, &s2)) { key = k2; scale = s2; vox_select(voice, key, scale); }
    vox_tick(recorder_state() == REC_PLAYING ? recorder_loop_seconds() : 0.0f);
    recorder_set_record(rec);
}

static void audio_block(void)
{
    static float in[64], out[64], env;
    mic(in, 64);
    vox_run(in, 64, env);
    recorder_process(in, out, 64);
    fx_process(out, 64);
    float pk = 0;
    for (int i = 0; i < 64; i++) {
        float o = out[i];
        if (!finite_f(o)) { fail("NaN or inf on the way out (voice %s, mode %d, speed %.2f)", vox_name(voice), mode, (double)recorder_speed_now()); break; }
        float a = fabsf(o); if (a > pk) pk = a;
    }
    env = pk > env ? pk : env * 0.95f;
    if (pk > worst_peak) worst_peak = pk;
    if (pk > 4.0f) fail("output %.2f, far over full scale", (double)pk);
    blocks++;
}

static void after_tick(void)
{
    int s = recorder_state();
    float pos = recorder_position(), spn = recorder_speed_now();
    if (!finite_f(pos) || pos < 0.0f || pos >= 1.0f) fail("head position %.3f", (double)pos);
    if (!finite_f(spn) || fabsf(spn) > 8.01f) fail("speed now %.3f", (double)spn);
    if (s != state_was) {
        if (s != REC_PLAYING) flock_loop_gone();
        if (state_was == REC_RECORDING && s == REC_IDLE && expect_loop) fail("a %d ms take ended in silence", take_len_ticks * 10);
        state_was = s;
    }
    if (s == REC_RECORDING && rec_hold_ticks == 0) { if (++closing_ticks > 400) fail("REC let go 4 s ago and the take has not closed"); }
    else closing_ticks = 0;
    if (s == REC_PLAYING && recorder_loop_start() != loop_seen) { loop_seen = recorder_loop_start(); flock_loop_made(recorder_loop_frames(), loop_seen); loops++; }
    // drift: at 1x with Snap on and the board around, the head stays on the beat once it has had a second to settle
    bool at_1x = s == REC_PLAYING && snap_setting && fabsf(spn - 1.0f) < 0.001f && recorder_loop_frames() % (int)link_fpb() == 0;
    ticks_at_1x = at_1x ? ticks_at_1x + 1 : 0;
    if (ticks_at_1x > 200) {
        float e = fabsf(recorder_lock_error());
        if (e > worst_drift) worst_drift = e;
        if (e > 64.0f) fail("the head is %.0f frames off the beat at 1x with Snap on", (double)e);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) hours = atof(argv[1]);
    if (!recorder_init()) return 1;
    fx_init();
    vox_init();
    recorder_set_volume(1.0f);
    flock_init(0, 0);
    flock_enable(true, 1, 1);
    P.st = link_new_state(); for (int i = 0; i < 6; i++) P.mac[i] = (uint8_t)(0x40 + i); P.off_us = 5e6; P.ppm = 25; P.epoch = 5.5e6;
    use(0); link_init("WRECK", 2); link_boot_time(19200, 0, 4); link_boot_harm(0, 0, 1, 0, 0); link_enable(true, 1, 1);
    link_set_time(16000, 0, 4, 0); use(-1);
    long total = (long)(hours * 3600.0 * 100.0);
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    for (ticks = 0; ticks < total; ticks++) {
        every_tick();
        for (int k = 0; k < 5; k++) audio_block();
        after_tick();
        if (ticks % 360000 == 0 && ticks) printf("  %.1f h: %ld takes, %ld loops, %ld loads, %ld scrubs, %ld voice changes, peak %.2f, worst drift %.1f frames, %d failures\n",
                                                  (double)ticks / 360000.0, takes, loops, loads, scrubs, voices_changed, (double)worst_peak, (double)worst_drift, fails);
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double wall = (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) * 1e-9;
    printf("%.2f simulated hours in %.0f s: %ld blocks, %ld takes, %ld loops, %ld loads, %ld scrubs, %ld voice changes\n", hours, wall, blocks, takes, loops, loads, scrubs, voices_changed);
    printf("worst output peak %.2f (the device soft-clips at 1.5), worst drift at 1x %.1f frames, link peers %d\n", (double)worst_peak, (double)worst_drift, flock_peers());
    printf("%d failures\n", fails);
    return fails ? 1 : 0;
}
