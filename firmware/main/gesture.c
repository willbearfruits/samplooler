// Gestures (gesture.h). With the tuning table at its defaults every function here returns what main.c and input.c
// computed before the table existed (rig/twin/test_v1.c checks it against copies of that code).
#include "gesture.h"
#include "tune.h"
#include <math.h>
#include <string.h>

static gest_event_fn emit_fn;
void gest_on_event(gest_event_fn fn) { emit_fn = fn; }
void gest_emit(const char *name, float v, int ms, const char *text) { if (emit_fn) emit_fn(name, v, ms, text ? text : ""); }

// ---------------------------------------------------------------- tilt
static bool live;                          // out of the dead zone (the hysteresis remembers)
static int  zone_was = -1;
static float detent_was, delay_was, stutter_was;

static void hand_reset(void);
void gest_reset(void) { live = false; zone_was = -1; detent_was = delay_was = stutter_was = 0; hand_reset(); }

// The nearest detent within reach holds the speed; just past it the speed catches up over half an octave, so the
// curve has no step.
static float detents(float r, float *held)
{
    static const float SET1[] = { 1 }, SET2[] = { 1, 2 }, SET3[] = { 0.25f, 0.5f, 1, 2, 4, 8 };
    const float *set; int n;
    switch (TI(TU_TI_DET)) {
    case 1: set = SET1; n = 1; break;
    case 2: set = SET2; n = 2; break;
    case 3: set = SET3; n = 6; break;
    default: return r;
    }
    if (!(r > 0)) return r;
    float best = set[0], bx = 1e9f;
    for (int i = 0; i < n; i++) { float x = log2f(r / set[i]); if (fabsf(x) < fabsf(bx)) { bx = x; best = set[i]; } }
    float w = TV(TU_TI_DET_W), s = 0.5f, a = fabsf(bx);
    if (a < w) { *held = best; return best; }
    if (a < w + s) return best * exp2f((bx < 0 ? -1.0f : 1.0f) * (a - w) * ((w + s) / s));
    return r;
}

float gest_speed(float u, float spd_min, float spd_max, float *detent)
{
    float full = TV(TU_TI_FULL), dead = TV(TU_TI_DEAD) / full;
    float mag = fabsf(u);
    if (live) { if (mag < dead) live = false; }
    else if (mag >= dead + TV(TU_TI_HYST) / full) live = true;
    if (detent) *detent = 0;
    if (!live) return spd_min;
    float t = (mag - dead) / (1.0f - dead);
    if (t > 1.0f) t = 1.0f;
    float c = TV(TU_TI_CURVE), soft = t * t * (1.6f - 0.6f * t);
    float curve = c >= 1.0f ? soft : c <= 0.0f ? t : (1.0f - c) * t + c * soft;
    float r = spd_min + (spd_max - spd_min) * curve;
    float held = 0;
    r = detents(r, &held);
    if (detent) *detent = held;
    return u >= 0.0f ? r : -r;
}

float gest_snap(float s)
{
    static const float grid[] = { 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f };
    float a = fabsf(s);
    if (a < 0.02f) return s;
    float best = grid[0], bd = 1e9f;
    for (unsigned i = 0; i < sizeof grid / sizeof grid[0]; i++) {
        float d = fabsf(a - grid[i]) / grid[i];
        if (d < bd) { bd = d; best = grid[i]; }
    }
    return s < 0 ? -best : best;
}

void gest_tilt(float pitch, float roll, bool tilt_on, bool has_imu, bool joy_speed, float joy_y,
               float spd_min, float spd_max, tilt_out_t *o)
{
    memset(o, 0, sizeof *o);
    float sign = TI(TU_TI_SIGN) ? 1.0f : -1.0f;
    if (tilt_on && has_imu) {
        o->u = (TI(TU_TI_AXIS) ? roll : pitch) * sign / TV(TU_TI_FULL);
        o->speed = gest_speed(o->u, spd_min, spd_max, &o->detent);
        o->zone = !live ? ZONE_DEAD : o->u >= 0.0f ? ZONE_FWD : ZONE_BACK;
    } else if (joy_speed) {
        o->u = joy_y;
        o->speed = gest_speed(joy_y, spd_min, spd_max, &o->detent);
        o->zone = !live ? ZONE_DEAD : o->u >= 0.0f ? ZONE_FWD : ZONE_BACK;
    } else { o->speed = 1.0f; o->zone = ZONE_DEAD; }

    // roll: left of the dead zone feeds the delay, right of it grabs a stutter
    float r = has_imu && TI(TU_RO_ON) ? roll * (TI(TU_RO_SIGN) ? 1.0f : -1.0f) : 0.0f;
    float dead = TV(TU_RO_DEAD), full = TV(TU_RO_FULL);
    float a = fabsf(r) < dead ? 0.0f : (fabsf(r) - dead) / (full - dead);
    if (a > 1.0f) a = 1.0f;
    o->delay = r < 0 ? a : 0.0f;
    o->stutter = r > 0 ? a : 0.0f;

    static const char *ZN[] = { "dead", "fwd", "back" };
    if (o->zone != zone_was) { if (zone_was >= 0) gest_emit("tilt.zone", (float)o->zone, 0, ZN[o->zone]); zone_was = o->zone; }
    if (o->detent != detent_was) { if (o->detent > 0) gest_emit("tilt.detent", o->detent, 0, ""); detent_was = o->detent; }
    if ((o->delay > 0) != (delay_was > 0)) gest_emit(o->delay > 0 ? "roll.delay" : "roll.off", o->delay, 0, "delay");
    if ((o->stutter > 0) != (stutter_was > 0)) gest_emit(o->stutter > 0 ? "roll.stutter" : "roll.off", o->stutter, 0, "stutter");
    delay_was = o->delay; stutter_was = o->stutter;
}

// ---------------------------------------------------------------- the encoder
void gest_enc(int counts, bool pressed, int64_t now, enc_out_t *o)
{
    static int acc;
    static bool was, long_fired;
    static int64_t t_press;
    static float rate;                     // detents a second, smoothed (enc.accel)
    memset(o, 0, sizeof *o);
    int steps = TI(TU_EN_STEPS);
    if (steps < 1) steps = 1;
    acc += counts * (TI(TU_EN_DIR) ? 1 : -1);
    o->enc = acc / steps;
    acc -= o->enc * steps;
    float ac = TV(TU_EN_ACCEL);
    rate += 0.2f * ((float)(o->enc < 0 ? -o->enc : o->enc) * 100.0f - rate);
    if (ac > 0 && o->enc) {
        float f = (rate - 8.0f) / 30.0f;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        o->enc = (int)lroundf((float)o->enc * (1.0f + ac * f));
    }
    if (o->enc) gest_emit("enc.turn", (float)o->enc, 0, "");

    if (pressed && !was) { t_press = now; long_fired = false; gest_emit("enc.down", 0, 0, ""); }
    int held = pressed ? (int)((now - t_press) / 1000) : 0;
    if (pressed && !long_fired && held >= TI(TU_EN_LONG)) { o->longp = true; long_fired = true; gest_emit("enc.long", 0, held, ""); }
    if (!pressed && was) {
        int ms = (int)((now - t_press) / 1000);
        gest_emit("enc.up", 0, ms, "");
        if (!long_fired) {
            if (ms >= TI(TU_EN_TAP_MIN) && ms < TI(TU_EN_HOLD)) { o->tap = true; gest_emit("enc.tap", 0, ms, ""); }
            else if (ms >= TI(TU_EN_HOLD)) { o->hold = true; gest_emit("enc.hold", 0, ms, ""); }
            else gest_emit("enc.bounce", 0, ms, "");
        }
    }
    o->pressed = pressed; o->held_ms = held;
    was = pressed;
}

// ---------------------------------------------------------------- the joystick
static float axis(int raw, float centre)
{
    float v = raw > centre ? (raw - centre) / (4095.0f - centre) : (raw - centre) / centre;
    float a = fabsf(v), dead = TV(TU_JO_DEAD);
    if (a < dead) return 0;
    v = (a - dead) / (1.0f - dead) * (v < 0 ? -1 : 1);
    return v < -1 ? -1 : (v > 1 ? 1 : v);
}

static bool armed[4] = { true, true, true, true };
static void flick(float v, bool *neg, bool *pos, int idx, const char *nn, const char *pn)
{
    if (v > TV(TU_JO_FLICK) && armed[idx]) { *pos = true; armed[idx] = false; gest_emit("joy.flick", v, 0, pn); }
    else if (v < -TV(TU_JO_FLICK) && armed[idx]) { *neg = true; armed[idx] = false; gest_emit("joy.flick", v, 0, nn); }
    else if (fabsf(v) < TV(TU_JO_REARM)) armed[idx] = true;
}

void gest_joy(int raw_x, int raw_y, float cx, float cy, joy_out_t *o)
{
    static float jx_f, jy_f;
    memset(o, 0, sizeof *o);
    float ax = axis(raw_x, cx), ay = axis(raw_y, cy), x, y;
    switch (TI(TU_JO_ROT)) {               // 90 (v1): the module sits a quarter turn clockwise
    case 0:  x = ax;  y = ay;  break;
    case 2:  x = -ax; y = -ay; break;
    case 3:  x = -ay; y = ax;  break;
    default: x = ay;  y = -ax; break;
    }
    if (TI(TU_JO_INV_X)) x = -x;
    if (TI(TU_JO_INV_Y)) y = -y;
    float k = TV(TU_JO_SMOOTH);
    jx_f += k * (x - jx_f); jy_f += k * (y - jy_f);
    o->jx = jx_f; o->jy = jy_f;
    flick(jx_f, &o->flick_l, &o->flick_r, 0, "left", "right");
    flick(jy_f, &o->flick_d, &o->flick_u, 1, "down", "up");
}

// ---------------------------------------------------------------- the left knob
void gest_knob(int counts, knob_out_t *o)
{
    static int acc;
    int steps = TI(TU_KN_STEPS);
    if (steps < 1) steps = 1;
    o->counts = counts * (TI(TU_KN_DIR) ? 1 : -1);
    acc += o->counts;
    o->det = acc / steps;
    acc -= o->det * steps;
    if (o->det) gest_emit("knob.turn", (float)o->det, 0, "");
}

// ---------------------------------------------------------------- the hand
// A reading counts when the sensor calls it good and it is under hand.far. Two in a row bring the hand in, three
// misses in a row (a glitch is one) send it away; the middle one of the last three readings is used, so one wild
// number does nothing. The value glides between readings, which come every 25 ms, so what it drives never steps.
static int   h_mm[3], h_n, h_hits, h_miss;
static bool  h_seen;
static float h_v, h_target;
static int   h_last_mm;
static void hand_reset(void) { h_n = h_hits = h_miss = 0; h_seen = false; h_v = h_target = 0; h_last_mm = 0; }

static int median3(int a, int b, int c) { return a > b ? (b > c ? b : a > c ? c : a) : (a > c ? a : b > c ? c : b); }

void gest_hand(bool fresh, int mm, int status, int dt_ms, hand_out_t *o)
{
    int near = TI(TU_HA_NEAR), far = TI(TU_HA_FAR);
    if (far < near + 10) far = near + 10;
    if (fresh) {
        bool good = status == 0 && mm >= 0 && mm < far;
        if (good) {
            h_mm[h_n % 3] = mm; h_n++;
            h_miss = 0;
            if (h_hits < 2) h_hits++;
            if (h_hits >= 2 && !h_seen) { h_seen = true; gest_emit("hand.on", (float)mm, 0, ""); }
            int m = h_n >= 3 ? median3(h_mm[0], h_mm[1], h_mm[2]) : mm;
            h_last_mm = m;
            float t = (float)(far - m) / (float)(far - near);
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float c = TV(TU_HA_CURVE);
            h_target = c == 1.0f ? t : powf(t, c);
        } else if (++h_miss >= 3) {
            if (h_seen) gest_emit("hand.off", 0, 0, "");
            h_seen = false; h_hits = 0; h_n = 0; h_target = 0;
        }
    }
    float tau = h_seen ? TV(TU_HA_GLIDE) : TV(TU_HA_RELEASE);
    float k = tau > (float)dt_ms ? (float)dt_ms / tau : 1.0f;
    h_v += k * ((h_seen ? h_target : 0.0f) - h_v);
    if (!h_seen && h_v < 0.002f) h_v = 0;
    o->v = h_v; o->seen = h_seen; o->mm = h_seen ? h_last_mm : 0;
}
