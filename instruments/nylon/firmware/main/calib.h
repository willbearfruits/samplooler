// Tilt setup's arithmetic: which IMU axis lies behind each screen axis. Host-testable (tools/sim checks
// all 24 ways the sensor can be mounted).
#pragma once
#include <stdint.h>

// From two accelerometer readings in the IMU's axes (lying flat, screen up; then the right edge tipped
// down), the IMU axis behind each screen axis: 1..3 = +X..+Z, negative for the reverse. Returns NULL and
// fills axis[], or returns what to tell the player ("TIP IT FURTHER", "TRY AGAIN").
const char *calib_solve(const float flat[3], const float tipped[3], int8_t axis[3]);
