// VL53L1X time-of-flight sensor at 0x29, polled over I2C. Optional: absent means tof_present() is false.
// Short distance mode, a 20 ms timing budget, a measurement every 25 ms. The millimetres are the chip's own,
// as ST's Ultra Lite Driver returns them.
#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"

bool tof_init(i2c_master_bus_handle_t bus);  // probe (20 ms at most) and add the device; the bring-up itself runs in tof_poll
bool tof_present(void);                      // answered at init, or has come back since
bool tof_ready(void);                        // bring-up done, it is ranging
bool tof_poll(int *mm, int *status);         // call every ~10 ms. true: *mm and *status hold a new measurement (status 0 = valid, ST's range-status meaning after the ULD's mapping)
int  tof_fails(void);                        // failed transfers since boot
