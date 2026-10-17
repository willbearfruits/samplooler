#include "imu.h"
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "imu";
static i2c_master_dev_handle_t dev;
static i2c_master_bus_handle_t bus_h;
static bool present, burst;
static int  streak, fails_total;              // consecutive and total failed reads
static int64_t t_next, t_fail_log;            // backed off: next read not before t_next
static imu_vec_t last_a = { 0, 0, 1 }, last_g;
#define BACKOFF_AFTER 20                      // 0.2 s of failures at 100 Hz
#define BACKOFF_US    500000
#define I2C_MS        10

// registers
#define R_SMPLRT_DIV  0x19
#define R_CONFIG      0x1A
#define R_GYRO_CFG    0x1B
#define R_ACCEL_CFG   0x1C
#define R_FIFO_EN     0x23
#define R_INT_STATUS  0x3A
#define R_ACCEL_XOUT  0x3B
#define R_USER_CTRL   0x6A
#define R_PWR_MGMT_1  0x6B
#define R_FIFO_COUNT  0x72
#define R_FIFO_RW     0x74
#define R_WHO_AM_I    0x75
#define ACC_LSB_G     4096.0f                 // ±8 g
#define GYRO_LSB_DPS  16.4f                   // ±2000 °/s
#define DLPF_CALM     3                       // 44 Hz: tilt, games
#define DLPF_BURST    1                       // 184 Hz: impacts

static bool wr(uint8_t reg, uint8_t val) { uint8_t b[2] = { reg, val }; return i2c_master_transmit(dev, b, 2, I2C_MS) == ESP_OK; }
static bool rd(uint8_t reg, uint8_t *b, int n) { return i2c_master_transmit_receive(dev, &reg, 1, b, n, I2C_MS) == ESP_OK; }
static inline float s16(const uint8_t *b) { return (float)(int16_t)(b[0] << 8 | b[1]); }

bool imu_init(i2c_master_bus_handle_t bus)
{
    bus_h = bus;
    if (i2c_master_probe(bus, 0x68, 20) != ESP_OK) { ESP_LOGW(TAG, "no MPU6050 at 0x68: the motion apps will sit still"); return false; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x68, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return false;
    uint8_t who = 0;
    rd(R_WHO_AM_I, &who, 1);
    // wake on the X gyro's PLL, 1 kHz internal rate (SMPLRT_DIV 0 with the DLPF on), ±2000 °/s, ±8 g
    present = wr(R_PWR_MGMT_1, 0x01) && wr(R_SMPLRT_DIV, 0) && wr(R_CONFIG, DLPF_CALM)
           && wr(R_GYRO_CFG, 0x18) && wr(R_ACCEL_CFG, 0x10) && wr(R_FIFO_EN, 0) && wr(R_USER_CTRL, 0);
    ESP_LOGI(TAG, "MPU6050 who_am_i 0x%02X, %s", who, present ? "ready" : "init failed");
    esp_log_level_set("i2c.master", ESP_LOG_NONE);    // a loose wire would print three lines per read
    return present;
}

bool imu_present(void) { return present; }

static void failed(int64_t now)
{
    fails_total++;
    if (++streak >= BACKOFF_AFTER) {
        t_next = now + BACKOFF_US;
        if (streak == BACKOFF_AFTER) i2c_master_bus_reset(bus_h);    // a device holding SDA: nine clocks and a stop
    }
    if (now - t_fail_log > 2000000) { t_fail_log = now; ESP_LOGW(TAG, "%d failed reads so far: check SDA/SCL/power to the MPU6050", fails_total); }
}

bool imu_read(imu_vec_t *a, imu_vec_t *g)
{
    int64_t now = esp_timer_get_time();
    bool ok = false;
    if (present && now >= t_next) {
        uint8_t b[14];
        if (rd(R_ACCEL_XOUT, b, 14)) {
            streak = 0; ok = true;
            last_a = (imu_vec_t){ s16(b) / ACC_LSB_G, s16(b + 2) / ACC_LSB_G, s16(b + 4) / ACC_LSB_G };
            last_g = (imu_vec_t){ s16(b + 8) / GYRO_LSB_DPS, s16(b + 10) / GYRO_LSB_DPS, s16(b + 12) / GYRO_LSB_DPS };
        } else failed(now);
    }
    *a = last_a; *g = last_g;
    return ok;
}

static bool fifo_reset(void)
{
    return wr(R_FIFO_EN, 0) && wr(R_USER_CTRL, 0x04) && wr(R_USER_CTRL, 0x40) && wr(R_FIFO_EN, 0x08);
}

void imu_burst(bool on)
{
    if (!present || on == burst) return;
    burst = on;
    if (on) { wr(R_CONFIG, DLPF_BURST); fifo_reset(); uint8_t s; rd(R_INT_STATUS, &s, 1); }
    else { wr(R_FIFO_EN, 0); wr(R_USER_CTRL, 0x04); wr(R_CONFIG, DLPF_CALM); }
    ESP_LOGI(TAG, "burst %s", on ? "on: 1 kHz accelerometer FIFO" : "off");
}

// Six bytes per sample (accelerometer only). The FIFO holds 1024 bytes, 170 ms: read at least every
// 100 ms. An overflow leaves it misaligned (1024 is not a multiple of 6), so it is reset and the
// samples in it dropped.
int imu_burst_read(imu_vec_t *dst, int max)
{
    if (!present || !burst) return 0;
    uint8_t c[2], st = 0;
    if (!rd(R_INT_STATUS, &st, 1) || !rd(R_FIFO_COUNT, c, 2)) { failed(esp_timer_get_time()); return 0; }
    int count = c[0] << 8 | c[1];
    if ((st & 0x10) || count >= 1020) { fifo_reset(); return 0; }
    int n = count / 6;
    if (n > max) n = max;
    if (n > 40) n = 40;                       // one transfer of at most 240 bytes; the rest waits for the next call
    if (n <= 0) return 0;
    uint8_t b[240];
    if (!rd(R_FIFO_RW, b, n * 6)) { failed(esp_timer_get_time()); fifo_reset(); return 0; }
    for (int i = 0; i < n; i++)
        dst[i] = (imu_vec_t){ s16(b + 6 * i) / ACC_LSB_G, s16(b + 6 * i + 2) / ACC_LSB_G, s16(b + 6 * i + 4) / ACC_LSB_G };
    return n;
}
