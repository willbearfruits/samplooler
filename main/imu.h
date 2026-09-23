// MPU6050 tilt. Optional: absent means imu_present() is false and the angles stay 0.
#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"
bool imu_init(i2c_master_bus_handle_t bus);
bool imu_present(void);
void imu_poll(float *roll_deg, float *pitch_deg);   // filtered; call at about 50 Hz
