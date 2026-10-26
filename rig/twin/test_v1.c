// v1 to the bit: the firmware's new motion.c + gesture.c, at the tuning table's defaults, against copies of the code
// they replaced (imu.c's imu_poll, main.c's control_to_speed and roll mapping, input.c's encoder and joystick, as of
// 86e19db). Random inputs, every output compared with ==.
//   cc -O2 -ffast-math -I../../firmware/main test_v1.c ../../firmware/main/{tune,motion,gesture}.c -lm && ./a.out
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "tune.h"
#include "motion.h"
#include "gesture.h"

// ---- the old code, copied ----
static float o_roll_f, o_pitch_f;
static void old_imu_poll(const uint8_t b[6], float *roll_deg, float *pitch_deg)
{
    float ax = (int16_t)(b[0] << 8 | b[1]), ay = (int16_t)(b[2] << 8 | b[3]), az = (int16_t)(b[4] << 8 | b[5]);
    float roll  = atan2f(ay, sqrtf(ax * ax + az * az)) * 57.2958f;
    float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.2958f;
    o_roll_f  += 0.25f * (roll - o_roll_f);
    o_pitch_f += 0.25f * (pitch - o_pitch_f);
    *roll_deg = o_roll_f; *pitch_deg = o_pitch_f;
}
#define TILT_SIGN     -1.0f
#define TILT_DEAD_DEG  5.0f
#define TILT_FULL_DEG  45.0f
#define ROLL_DEAD_DEG  15.0f
#define ROLL_FULL_DEG  45.0f
#define ROLL_SIGN      1.0f
static float spd_min = 0.05f, spd_max = 2.0f;
static float old_control_to_speed(float u)
{
    float mag = fabsf(u), dead = TILT_DEAD_DEG / TILT_FULL_DEG;
    if (mag < dead) return spd_min;
    float t = (mag - dead) / (1.0f - dead);
    if (t > 1.0f) t = 1.0f;
    float curve = t * t * (1.6f - 0.6f * t);
    float r = spd_min + (spd_max - spd_min) * curve;
    return u >= 0.0f ? r : -r;
}
static void old_roll(float roll, float *delay, float *stutter)
{
    float r = roll * ROLL_SIGN;
    float a = fabsf(r) < ROLL_DEAD_DEG ? 0.0f : (fabsf(r) - ROLL_DEAD_DEG) / (ROLL_FULL_DEG - ROLL_DEAD_DEG);
    if (a > 1.0f) a = 1.0f;
    *delay = r < 0 ? a : 0.0f;
    *stutter = r > 0 ? a : 0.0f;
}
#define ENC_STEPS 4
#define ENC_DIR  -1
static int o_enc_acc; static bool o_was, o_long; static int64_t o_t_press;
static void old_enc(int cnt, bool pressed, int64_t now, enc_out_t *ev)
{
    memset(ev, 0, sizeof *ev);
    o_enc_acc += cnt * ENC_DIR;
    ev->enc = o_enc_acc / ENC_STEPS;
    o_enc_acc -= ev->enc * ENC_STEPS;
    if (pressed && !o_was) { o_t_press = now; o_long = false; }
    int held = pressed ? (int)((now - o_t_press) / 1000) : 0;
    if (pressed && !o_long && held >= 3000) { ev->longp = true; o_long = true; }
    if (!pressed && o_was && !o_long) {
        int ms = (int)((now - o_t_press) / 1000);
        if (ms >= 40 && ms < 500) ev->tap = true;
        else if (ms >= 500) ev->hold = true;
    }
    ev->pressed = pressed; ev->held_ms = held;
    o_was = pressed;
}
static float o_jx_f, o_jy_f; static bool o_armed[4] = { true, true, true, true };
static float old_axis(int raw, float centre)
{
    float v = raw > centre ? (raw - centre) / (4095.0f - centre) : (raw - centre) / centre;
    float a = fabsf(v);
    if (a < 0.12f) return 0;
    v = (a - 0.12f) / 0.88f * (v < 0 ? -1 : 1);
    return v < -1 ? -1 : (v > 1 ? 1 : v);
}
static void old_flick(float v, bool *neg, bool *pos, int idx)
{
    if (v > 0.7f && o_armed[idx]) { *pos = true; o_armed[idx] = false; }
    else if (v < -0.7f && o_armed[idx]) { *neg = true; o_armed[idx] = false; }
    else if (fabsf(v) < 0.3f) o_armed[idx] = true;
}
static void old_joy(int rx, int ry, float cx, float cy, joy_out_t *o)
{
    memset(o, 0, sizeof *o);
    float ax = old_axis(rx, cx), ay = old_axis(ry, cy);
    float x = ay, y = -ax;
    o_jx_f += 0.3f * (x - o_jx_f); o_jy_f += 0.3f * (y - o_jy_f);
    o->jx = o_jx_f; o->jy = o_jy_f;
    old_flick(o_jx_f, &o->flick_l, &o->flick_r, 0);
    old_flick(o_jy_f, &o->flick_d, &o->flick_u, 1);
}

// ---- the check ----
static int fails, checks;
// Discrete results (taps, flicks, counts) must be equal. Continuous ones within float rounding: with -ffast-math a
// division by a constant (the old #defines) becomes a multiply by its reciprocal, a division by a tuning value does
// not, and the two differ in the last bit. The angles themselves come out equal to the bit.
static double worst;
#define SAME(a, b, what, i) do { checks++; double _a = (a), _b = (b), _d = fabs(_a - _b), _t = 2e-6 * fabs(_a) + 1e-6; \
    if (_d > worst) worst = _d; \
    if (!(_d <= _t)) { if (fails++ < 12) printf("DIFF %s at %d: old %.9g new %.9g\n", what, i, _a, _b); } } while (0)
#define EXACT(a, b, what, i) do { checks++; if (!((a) == (b))) { if (fails++ < 12) printf("DIFF %s at %d: old %.9g new %.9g\n", what, i, (double)(a), (double)(b)); } } while (0)

static int16_t rnd16(int span) { return (int16_t)((rand() % (2 * span + 1)) - span); }

int main(void)
{
    tune_defaults();
    motion_init();
    gest_reset();
    srand(1234);
    // A wandering board: random walks, with jumps, every sign, every size (±2 g at 16384 LSB/g).
    int16_t c[3] = { 0, 0, 16384 };
    for (int i = 0; i < 200000; i++) {
        for (int k = 0; k < 3; k++) {
            int span = (i % 1000 < 30) ? 30000 : 900;              // now and then a knock
            int v = c[k] + rnd16(span);
            v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
            c[k] = (int16_t)v;
        }
        if (i % 50000 == 7) c[0] = c[1] = c[2] = 0;                   // the sensor reads all zeros
        uint8_t b[6] = { (uint8_t)(c[0] >> 8), (uint8_t)c[0], (uint8_t)(c[1] >> 8), (uint8_t)c[1], (uint8_t)(c[2] >> 8), (uint8_t)c[2] };
        float o_roll, o_pitch;
        old_imu_poll(b, &o_roll, &o_pitch);
        float a[3] = { c[0] / 16384.0f, c[1] / 16384.0f, c[2] / 16384.0f }, g[3] = { 0, 0, 0 };
        motion_sample(a, g, 0.002f);
        motion_tick(false);
        const motion_t *m = motion_get();
        EXACT(o_roll, m->roll, "roll", i);
        EXACT(o_pitch * TILT_SIGN, m->pitch, "pitch (sign)", i);

        if (i % 3 == 0) { spd_min = (rand() % 100) / 100.0f; spd_max = 0.25f + (rand() % 32) * 0.25f; }
        float o_speed = old_control_to_speed(o_pitch * TILT_SIGN / TILT_FULL_DEG);
        float od, os; old_roll(o_roll, &od, &os);
        tilt_out_t t; gest_tilt(m->pitch, m->roll, true, true, false, 0, spd_min, spd_max, &t);
        SAME(o_speed, t.speed, "speed", i);
        SAME(od, t.delay, "delay", i);
        SAME(os, t.stutter, "stutter", i);
        // straight angles too, not only smoothed ones
        float ang = (rand() % 20001 - 10000) / 100.0f;
        SAME(old_control_to_speed(ang * TILT_SIGN / TILT_FULL_DEG), gest_speed(-ang * 1.0f / 45.0f, spd_min, spd_max, NULL), "curve", i);
        old_roll(ang, &od, &os); gest_tilt(0, ang, false, true, false, 0, spd_min, spd_max, &t);
        SAME(od, t.delay, "delay straight", i); SAME(os, t.stutter, "stutter straight", i);
        EXACT(1.0f, t.speed, "tilt off: 1x", i);
    }
    // the encoder: random counts, presses of every length
    int64_t now = 0; bool sw = false; int64_t next = 0;
    for (int i = 0; i < 300000; i++) {
        now += 10000;
        if (now >= next) { sw = !sw; next = now + (rand() % 400) * 10000 * (rand() % 3 ? 1 : 12); }
        int cnt = rand() % 9 - 4;
        if (rand() % 4) cnt = 0;
        enc_out_t a, b;
        old_enc(cnt, sw, now, &a);
        gest_enc(cnt, sw, now, &b);
        EXACT(a.enc, b.enc, "enc", i); EXACT(a.tap, b.tap, "tap", i); EXACT(a.hold, b.hold, "hold", i);
        EXACT(a.longp, b.longp, "long", i); EXACT(a.held_ms, b.held_ms, "held", i); EXACT(a.pressed, b.pressed, "pressed", i);
    }
    // the joystick
    int rx = 2048, ry = 2048;
    for (int i = 0; i < 200000; i++) {
        rx += rand() % 401 - 200; ry += rand() % 401 - 200;
        rx = rx < 0 ? 0 : rx > 4095 ? 4095 : rx; ry = ry < 0 ? 0 : ry > 4095 ? 4095 : ry;
        float cx = 1900 + rand() % 300, cy = 1900 + rand() % 300;
        joy_out_t a, b;
        old_joy(rx, ry, cx, cy, &a);
        gest_joy(rx, ry, cx, cy, &b);
        SAME(a.jx, b.jx, "jx", i); SAME(a.jy, b.jy, "jy", i);
        EXACT(a.flick_l, b.flick_l, "flick l", i); EXACT(a.flick_r, b.flick_r, "flick r", i);
        EXACT(a.flick_u, b.flick_u, "flick u", i); EXACT(a.flick_d, b.flick_d, "flick d", i);
    }
    printf("v1 check: %d comparisons, %d differences (largest continuous gap %.3g)\n", checks, fails, worst);
    return fails ? 1 : 0;
}
