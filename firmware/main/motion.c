// Motion (motion.h). ACC is the firmware before the gyro was read, to the bit: two atan2 of the latest accelerometer
// sample, a one-pole per control tick. The fusion (Mahony's filter: the gyro integrated, steered towards gravity) runs
// all the time for the orientation, and gives the control angles too under COMP (kp = 1/tau, no integral) or MAHONY.
#include "motion.h"
#include "tune.h"
#include <math.h>
#include <string.h>

#define R2D 57.2958f                       // v1's constant
#define D2R 0.0174533f

static motion_t M = { .q = { 1, 0, 0, 0 } };
static float a_dev[3] = { 0, 0, 1 };       // the latest sample, device axes
static bool  fresh;                        // a sample since the last tick
static float pitch_f, roll_f;              // ACC's smoothed angles
static float ei[3];                        // MAHONY's integral
static bool  q_set;
static float still_s;

void motion_map(const float c[3], float d[3])
{
    for (int k = 0; k < 3; k++) {
        int e = TI(TU_MO_AX_X + k);        // 0 +x, 1 -x, 2 +y, 3 -y, 4 +z, 5 -z
        if (e < 0 || e > 5) e = 2 * k;
        float v = c[e >> 1];
        d[k] = (e & 1) ? -v : v;
    }
}

void motion_init(void)
{
    memset(&M, 0, sizeof M);
    M.q[0] = 1;
    a_dev[0] = a_dev[1] = 0; a_dev[2] = 1;
    fresh = false; pitch_f = roll_f = 0; ei[0] = ei[1] = ei[2] = 0; q_set = false; still_s = 0;
}

const motion_t *motion_get(void) { return &M; }

static void q_norm(float *q)
{
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(n > 1e-6f)) { q[0] = 1; q[1] = q[2] = q[3] = 0; return; }
    float r = 1.0f / n;
    for (int k = 0; k < 4; k++) q[k] *= r;
}

// The shortest turn that brings the measured up (device axes) onto the world's z: the start, with yaw 0.
static void q_from_up(const float *a, float *q)
{
    float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!(n > 0.05f)) { q[0] = 1; q[1] = q[2] = q[3] = 0; return; }
    float ux = a[0] / n, uy = a[1] / n, uz = a[2] / n;
    if (uz < -0.9999f) { q[0] = 0; q[1] = 1; q[2] = q[3] = 0; return; }
    q[0] = 1 + uz; q[1] = uy; q[2] = -ux; q[3] = 0;
    q_norm(q);
}

static void fuse(const float *g_dps, const float *a, float dt, float kp, float ki, float gate)
{
    float *q = M.q;
    float gx = g_dps[0] * D2R, gy = g_dps[1] * D2R, gz = g_dps[2] * D2R;
    float an = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    float w = 1.0f;
    if (gate > 0) { float dev = fabsf(an - 1.0f); w = dev >= gate ? 0.0f : 1.0f - dev / gate; }
    if (an > 0.05f && w > 0) {
        float ax = a[0] / an, ay = a[1] / an, az = a[2] / an;
        float vx = 2 * (q[1] * q[3] - q[0] * q[2]);           // gravity as the estimate sees it, device axes
        float vy = 2 * (q[0] * q[1] + q[2] * q[3]);
        float vz = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3];
        float ex = ay * vz - az * vy, ey = az * vx - ax * vz, ez = ax * vy - ay * vx;
        if (ki > 0) { ei[0] += ki * ex * dt * w; ei[1] += ki * ey * dt * w; ei[2] += ki * ez * dt * w; }
        else ei[0] = ei[1] = ei[2] = 0;
        gx += kp * w * ex; gy += kp * w * ey; gz += kp * w * ez;
    }
    if (ki > 0) { gx += ei[0]; gy += ei[1]; gz += ei[2]; }
    float h = 0.5f * dt, q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];
    q[0] += h * (-q1 * gx - q2 * gy - q3 * gz);
    q[1] += h * ( q0 * gx + q2 * gz - q3 * gy);
    q[2] += h * ( q0 * gy - q1 * gz + q3 * gx);
    q[3] += h * ( q0 * gz + q1 * gy - q2 * gx);
    q_norm(q);
}

void motion_sample(const float a_chip[3], const float g_chip[3], float dt)
{
    if (!(dt > 0) || dt > 0.1f) dt = 0.002f;
    float a[3], g[3];
    for (int k = 0; k < 3; k++) {
        a[k] = a_chip[k] - TV(TU_IMU_AOFF_X + k);
        g[k] = g_chip[k] - TV(TU_IMU_GBIAS_X + k) - M.bias[k];
    }
    // lying still: the gyro near zero and the accelerometer near 1 g, for imu.rest_time
    float gm = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    float am = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (gm < TV(TU_IMU_REST_DPS) && fabsf(am - 1.0f) < TV(TU_IMU_REST_G)) still_s += dt; else still_s = 0;
    M.rest = still_s * 1000.0f >= TV(TU_IMU_REST_MS);
    if (M.rest && TI(TU_IMU_AUTOBIAS)) {
        float k = dt / TV(TU_IMU_BIAS_TAU);
        for (int i = 0; i < 3; i++) { M.bias[i] += g[i] * k; g[i] -= g[i] * k; }
    }
    motion_map(a, a_dev);
    motion_map(g, M.gyro);
    memcpy(M.acc, a_dev, sizeof a_dev);
    fresh = true;

    if (!q_set) { q_from_up(a_dev, M.q); q_set = true; }
    int f = TI(TU_MO_FUSION);
    float kp = f == 2 ? TV(TU_MO_KP) : 1.0f / TV(TU_MO_COMP_TAU), ki = f == 2 ? TV(TU_MO_KI) : 0.0f;
    fuse(M.gyro, a_dev, dt, kp, ki, TV(TU_MO_ACC_GATE));
}

void motion_tick(bool hold)
{
    const float *q = M.q;
    float vx = 2 * (q[1] * q[3] - q[0] * q[2]), vy = 2 * (q[0] * q[1] + q[2] * q[3]);
    float vz = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3];
    M.yaw = atan2f(2 * (q[0] * q[3] + q[1] * q[2]), 1 - 2 * (q[2] * q[2] + q[3] * q[3])) * R2D;
    if (hold) { fresh = false; return; }               // frozen: the control angles stay (v1 did not read the sensor)
    if (TI(TU_MO_FUSION) == 0) {
        // v1, exactly: the old imu_poll() worked in the chip's axes (roll from y, pitch from -x) and main.c
        // flipped the pitch (TILT_SIGN -1). The default axis map (x = -y, y = +x, z = +z) gives the same numbers.
        if (fresh) {
            float ax = a_dev[0], ay = a_dev[1], az = a_dev[2];
            float pitch = atan2f(ay, sqrtf(ax * ax + az * az)) * R2D;
            float roll  = atan2f(-ax, sqrtf(ay * ay + az * az)) * R2D;
            float k = TV(TU_MO_ACC_SMOOTH);
            roll_f  += k * (roll - roll_f);
            pitch_f += k * (pitch - pitch_f);
        }
        M.pitch = pitch_f; M.roll = roll_f;
    } else {
        M.pitch = atan2f(vy, sqrtf(vx * vx + vz * vz)) * R2D;
        M.roll  = atan2f(-vx, sqrtf(vy * vy + vz * vz)) * R2D;
        pitch_f = M.pitch; roll_f = M.roll;           // a switch back to ACC starts from here, not from zero
    }
    fresh = false;
}

void motion_yaw_reset(void)
{
    float h = -M.yaw * D2R * 0.5f, c = cosf(h), s = sinf(h);
    float *q = M.q, q0 = q[0], q1 = q[1], q2 = q[2], q3 = q[3];   // (c, 0, 0, s) * q: a turn about the world's z
    q[0] = c * q0 - s * q3; q[1] = c * q1 - s * q2; q[2] = c * q2 + s * q1; q[3] = c * q3 + s * q0;
    q_norm(q);
    M.yaw = 0;
}

void motion_bias_keep(void)
{
    for (int k = 0; k < 3; k++) { tune_set(TU_IMU_GBIAS_X + k, TV(TU_IMU_GBIAS_X + k) + M.bias[k]); M.bias[k] = 0; }
}
