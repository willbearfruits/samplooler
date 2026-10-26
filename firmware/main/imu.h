// MPU-6050: accelerometer and gyro through the chip's FIFO, raw, in the chip's own axes. Ranges, filter and rate come
// from the tuning table (imu.*; imu_config applies them). motion.c turns the samples into angles.
// Optional: absent means imu_present() is false.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

bool  imu_init(i2c_master_bus_handle_t bus);
bool  imu_present(void);
void  imu_config(int acc_range, int gyro_range, int dlpf, int rate);   // tune codes (tune.h): 2/4/8/16 g, 250..2000 deg/s, 260..5 Hz, 100..1000 Hz
float imu_acc_lsb(void);                  // LSB per g at the current range
float imu_gyro_lsb(void);                 // LSB per deg/s
int   imu_rate_hz(void);
int   imu_read(int16_t (*raw)[6], int max, bool *overflow);   // samples since the last call, oldest first: ax ay az gx gy gz
int   imu_fails(void);                    // failed reads since boot
