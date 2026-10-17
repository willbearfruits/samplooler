// MPU-6050: accelerometer (±8 g) and gyro (±2000 °/s), read in the board's own axes. The shell maps them
// to the screen's axes (store.h). Optional: absent means imu_present() is false.
// In burst mode the chip's FIFO also collects accelerometer samples at 1 kHz, for apps that need the
// shape of an impact (Knock); everything else reads the latest sample once per control tick.
#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"

typedef struct { float x, y, z; } imu_vec_t;

bool imu_init(i2c_master_bus_handle_t bus);
bool imu_present(void);
bool imu_read(imu_vec_t *acc_g, imu_vec_t *gyro_dps);   // latest sample; false (values kept) on a failed read
void imu_burst(bool on);                                 // 1 kHz accelerometer FIFO on or off
int  imu_burst_read(imu_vec_t *dst, int max);            // FIFO samples since the last call, oldest first, in g
