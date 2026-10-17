# esp_driver_i2c (patched copy)

ESP-IDF v5.5.1's `components/esp_driver_i2c`, copied so that it overrides the IDF one (a project component
with the same name wins). One change, in `s_i2c_send_commands()` in `i2c_master.c`: after a NACK the driver
waited for the bus to go idle with no limit. A bus that never went idle (a device holding SDA low, a loose
wire) kept the calling task spinning until the task watchdog reset the board. That happened on a
Samplooler board on 2026-10-14 (backtrace: `imu_read` -> `i2c_master_transmit_receive` -> this loop).
The wait now ends after 2 ms and marks the transaction timed out; the driver's own code then resets the
controller and clears the bus at the start of the next transaction.

When moving to a newer ESP-IDF, check whether the loop is still unbounded there; if not, delete this folder.
