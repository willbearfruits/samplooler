// Motion: IMU samples to the board's orientation, by the tuning table (tune.h: imu.*, motion.*). Pure C, also built
// on the PC (rig/twin, the v1 check). Axes (rig/PROTOCOL.md), right-handed: device x to the screen's right edge, y to
// its top edge (the nose), z out of the screen; flat, screen up, the accelerometer reads (0, 0, +1 g).
#pragma once
#include <stdbool.h>

typedef struct {
    float acc[3], gyro[3];   // the latest sample in device axes: g, deg/s (offsets and both biases removed)
    float pitch, roll;       // deg, what the controls use: pitch + with the nose (the screen's top edge) down,
                             // roll + with the right edge down. ACC: from gravity alone, smoothed (v1); else the fusion's
    float yaw;               // deg, the fusion's (no compass: it drifts)
    float q[4];              // w x y z, device axes into the world (z up): the gyro + gravity fusion, always running
    float bias[3];           // the learned gyro bias, chip axes, deg/s
    bool  rest;              // lying still (the bias is learned meanwhile)
} motion_t;

void motion_init(void);
void motion_sample(const float a_chip[3], const float g_chip[3], float dt);  // each sensor sample: g and deg/s, chip axes
void motion_tick(bool hold);                  // each control tick, after its samples: the angles (hold: they stay)
const motion_t *motion_get(void);
void motion_yaw_reset(void);
void motion_bias_keep(void);                  // the learned bias into imu.gyro_bias_* (the learned part starts again from 0)
void motion_map(const float chip[3], float dev[3]);   // chip axes to device axes (motion.ax_*)
