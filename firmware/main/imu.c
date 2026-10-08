#include "imu.h"
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "imu";
static int fails, fails_total;
static int64_t t_fail_log;
static i2c_master_dev_handle_t dev;
static bool present;
static float roll_f, pitch_f;
static int   streak;                        // consecutive failed reads
static int64_t t_next;                      // backed off: next read not before this
#define BACKOFF_AFTER 10                    // 0.2 s of failures at 50 Hz
#define BACKOFF_US    500000
#define I2C_MS        10                    // 7 bytes at 400 kHz take well under a millisecond

static bool wr(uint8_t reg, uint8_t val) { uint8_t b[2] = { reg, val }; return i2c_master_transmit(dev, b, 2, I2C_MS) == ESP_OK; }

bool imu_init(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, 0x68, 20) != ESP_OK) { ESP_LOGW(TAG, "no MPU6050 at 0x68: playing forward at 1x"); return false; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x68, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return false;
    uint8_t who = 0, reg = 0x75;
    i2c_master_transmit_receive(dev, &reg, 1, &who, 1, I2C_MS);
    present = wr(0x6B, 0x01) && wr(0x1C, 0x00) && wr(0x1B, 0x00) && wr(0x1A, 0x03);   // wake, 2 g, 250 dps, 44 Hz DLPF
    ESP_LOGI(TAG, "MPU6050 who_am_i 0x%02X, %s", who, present ? "ready" : "init failed");
    // A loose wire makes the driver print three error lines per failed read, 50 times a second.
    // Keep the log usable: count the failures here and report them once every two seconds.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    return present;
}

bool imu_present(void) { return present; }

void imu_poll(float *roll_deg, float *pitch_deg)
{
    // A sensor that stops answering is read twice a second instead of fifty times, so its I2C timeouts
    // do not slow the control loop; the last good angles are held meanwhile.
    int64_t now = esp_timer_get_time();
    if (present && now >= t_next) {
        uint8_t reg = 0x3B, b[6];
        if (i2c_master_transmit_receive(dev, &reg, 1, b, 6, I2C_MS) == ESP_OK) {
            streak = 0;
            float ax = (int16_t)(b[0] << 8 | b[1]), ay = (int16_t)(b[2] << 8 | b[3]), az = (int16_t)(b[4] << 8 | b[5]);
            // roll as the sideways lean of the Y axis, so nose-down alone leaves it near zero
            float roll  = atan2f(ay, sqrtf(ax * ax + az * az)) * 57.2958f;
            float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.2958f;
            roll_f  += 0.25f * (roll - roll_f);
            pitch_f += 0.25f * (pitch - pitch_f);
        } else {
            fails++; fails_total++;
            if (++streak >= BACKOFF_AFTER) t_next = now + BACKOFF_US;
        }
    }
    if (present) {
        if (fails && now - t_fail_log > 2000000) {
            ESP_LOGW(TAG, "%d of the last %d reads failed (%d total): check SDA/SCL/power to the MPU6050", fails, 100, fails_total);
            fails = 0; t_fail_log = now;
        } else if (!fails && now - t_fail_log > 2000000) t_fail_log = now;
    }
    *roll_deg = roll_f; *pitch_deg = pitch_f;
}
