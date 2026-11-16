#include "tof.h"
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"

// The registers, their values and the order of the bring-up are ST's Ultra Lite Driver's (VL53L1X_SensorInit, then
// SetDistanceMode(1), SetTimingBudgetInMs(20), SetInterMeasurementInMs(25), StartRanging), spread over calls of
// tof_poll so that none of them waits: the longest one is the 93-byte configuration write, 2.1 ms at 400 kHz.

static const char *TAG = "tof";
static i2c_master_dev_handle_t dev;
static i2c_master_bus_handle_t bus_h;
static bool present, seen;                   // seen: it has answered at least once since boot
static int  st, streak, fails_total, pol;    // pol: what bit 0 of the status register reads when a measurement waits
static unsigned id_logged = ~0u;             // the model id is logged when it is not the one logged last
static int64_t t_next, t_dead, t_fail_log = -2000000;   // nothing before t_next; the step has until t_dead
#define ADDR          0x29
#define BACKOFF_AFTER 10                     // 0.1 s of failures
#define BACKOFF_US    500000
#define ABSENT_US     2000000                // never seen: look for it this often
#define STEP_US       500000                 // a bring-up step has this long, the boot wait too
#define DATA_US       1000000                // and a measurement this long to arrive
#define I2C_MS        10
#define BUDGET_MS     20
#define PERIOD_MS     25

enum { S_PROBE, S_BOOT, S_CONFIG, S_START, S_FIRST, S_VHV, S_MODE, S_TIMING, S_RANGE };
static const char *const STEP[] = { "probe", "boot", "configuration", "start", "first range", "VHV", "distance mode", "timing", "ranging" };

#define R_VHV_LOOP_BOUND 0x0008              // VHV_CONFIG__TIMEOUT_MACROP_LOOP_BOUND
#define R_VHV_INIT       0x000B              // ST's SensorInit writes it by number
#define R_GPIO_MUX       0x0030              // GPIO_HV_MUX__CTRL: bit 4 is the interrupt's polarity
#define R_GPIO_STATUS    0x0031              // GPIO__TIO_HV_STATUS: bit 0 is the interrupt
#define R_PHASECAL       0x004B              // PHASECAL_CONFIG__TIMEOUT_MACROP
#define R_TIMEOUT_A      0x005E              // RANGE_CONFIG__TIMEOUT_MACROP_A_HI
#define R_VCSEL_A        0x0060
#define R_TIMEOUT_B      0x0061
#define R_VCSEL_B        0x0063
#define R_PHASE_HIGH     0x0069              // RANGE_CONFIG__VALID_PHASE_HIGH
#define R_PERIOD         0x006C              // SYSTEM__INTERMEASUREMENT_PERIOD, 32 bits
#define R_WOI_SD0        0x0078
#define R_PHASE_SD0      0x007A              // SD_CONFIG__INITIAL_PHASE_SD0
#define R_INT_CLEAR      0x0086
#define R_MODE_START     0x0087
#define R_RESULT         0x0089              // RESULT__RANGE_STATUS, the head of the result block
#define RES_MM           (0x0096 - R_RESULT) // FINAL_CROSSTALK_CORRECTED_RANGE_MM_SD0 in it
#define RES_N            (RES_MM + 2)
#define R_OSC_CAL        0x00DE              // RESULT__OSC_CALIBRATE_VAL
#define R_BOOT           0x00E5              // FIRMWARE__SYSTEM_STATUS
#define R_MODEL_ID       0x010F

// The default configuration, registers 0x2D to 0x87, behind the index it is written to. It is
// VL51L1X_DEFAULT_CONFIGURATION from ST's Ultra Lite Driver (vl53l1x_class.cpp as packaged by stm32duino), unchanged:
//
//   COPYRIGHT(c) 2018 STMicroelectronics
//
//   Redistribution and use in source and binary forms, with or without modification,
//   are permitted provided that the following conditions are met:
//     1. Redistributions of source code must retain the above copyright notice,
//        this list of conditions and the following disclaimer.
//     2. Redistributions in binary form must reproduce the above copyright notice,
//        this list of conditions and the following disclaimer in the documentation
//        and/or other materials provided with the distribution.
//     3. Neither the name of STMicroelectronics nor the names of its contributors
//        may be used to endorse or promote products derived from this software
//        without specific prior written permission.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// ST's notes on the bytes that matter here: 0x2E and 0x2F, bit 0 clear = I2C and GPIO pulled up to 1.8 V ("else set
// bit 0 to 1 (pull up at AVDD)": ST leaves both clear); 0x30 bit 4 = interrupt polarity (clear: active high);
// 0x46 = 0x20, interrupt on a new sample; 0x6C..0x6F the inter-measurement period; 0x86 clears the interrupt;
// 0x87 = 0x40 starts ranging.
static const uint8_t CFG[2 + 91] = {
    0x00, 0x2D,                                     // the index
    0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, // 0x2D
    0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00, // 0x35
    0x00, 0xFF, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, // 0x3D
    0x00, 0x20, 0x0B, 0x00, 0x00, 0x02, 0x0A, 0x21, // 0x45
    0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC8, // 0x4D
    0x00, 0x00, 0x38, 0xFF, 0x01, 0x00, 0x08, 0x00, // 0x55
    0x00, 0x01, 0xCC, 0x0F, 0x01, 0xF1, 0x0D, 0x01, // 0x5D
    0x68, 0x00, 0x80, 0x08, 0xB8, 0x00, 0x00, 0x00, // 0x65
    0x00, 0x0F, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, // 0x6D
    0x00, 0x00, 0x01, 0x0F, 0x0D, 0x0E, 0x0E, 0x00, // 0x75
    0x00, 0x02, 0xC7, 0xFF, 0x9B, 0x00, 0x00, 0x00, // 0x7D
    0x01, 0x00, 0x00,                               // 0x85
};

// The ULD's VL53L1X_GetRangeStatus: the chip's code (low five bits) to ST's range status. 0 is a valid range,
// 1 sigma failure, 2 signal failure, 4 out of bounds, 7 wrap-around; 255 is a code the ULD does not know.
static const uint8_t STATUS[24] = {
    255, 255, 255, 5, 2, 4, 1, 7,   3, 0, 255, 255, 9, 13, 255, 255,   255, 255, 10, 6, 255, 255, 11, 12,
};

// Register indices are 16 bits, high byte first, and so are the values. The chip steps the index after every byte.
static bool wr(const uint8_t *b, int n) { return i2c_master_transmit(dev, b, (size_t)n, I2C_MS) == ESP_OK; }
static bool wr8(unsigned reg, unsigned v) { uint8_t b[3] = { (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)v }; return wr(b, 3); }
static bool wr16(unsigned reg, unsigned v) { uint8_t b[4] = { (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(v >> 8), (uint8_t)v }; return wr(b, 4); }
static bool wr32(unsigned reg, uint32_t v)
{
    uint8_t b[6] = { (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    return wr(b, 6);
}
static bool rd(unsigned reg, uint8_t *b, int n)
{
    uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    return i2c_master_transmit_receive(dev, a, 2, b, (size_t)n, I2C_MS) == ESP_OK;
}

static bool attach(void)
{
    if (dev) return true;
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus_h, &cfg, &dev) == ESP_OK) return true;
    dev = NULL;
    return false;
}

static void go(int s, int64_t now, int64_t us) { st = s; t_dead = now + us; }
// Start over: quiet for the back-off, then a probe and the whole bring-up, because it may have lost power.
static void restart(int64_t now) { st = S_PROBE; t_next = now + BACKOFF_US; }
// A loose wire fails a hundred times a second. One line every two seconds.
static bool may_log(int64_t now) { if (now - t_fail_log < 2000000) return false; t_fail_log = now; return true; }

bool tof_init(i2c_master_bus_handle_t bus)
{
    bus_h = bus;
    st = S_PROBE;
    bool found = i2c_master_probe(bus, ADDR, 20) == ESP_OK && attach();
    int64_t now = esp_timer_get_time();
    // As in imu.c: the driver prints several error lines per failed transfer. They are counted here instead.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    if (!found) { ESP_LOGW(TAG, "no VL53L1X at 0x29: looking again every 2 s"); t_next = now + ABSENT_US; return false; }
    present = seen = true;
    go(S_BOOT, now, STEP_US);
    return true;
}

bool tof_present(void) { return present; }
bool tof_ready(void) { return st == S_RANGE; }
int  tof_fails(void) { return fails_total; }

static void failed(int64_t now)
{
    fails_total++;
    if (++streak >= BACKOFF_AFTER) {         // the streak ends at a call that worked, so after a back-off one failure is enough
        if (streak == BACKOFF_AFTER) i2c_master_bus_reset(bus_h);     // a device holding SDA: nine clocks and a stop
        restart(now);
    }
    if (may_log(now)) ESP_LOGW(TAG, "%d failed transfers so far: check SDA/SCL/power to the VL53L1X", fails_total);
}

bool tof_poll(int *mm, int *status)
{
    int64_t now = esp_timer_get_time();
    if (!bus_h || now < t_next) return false;
    if (st == S_PROBE) {                     // absent, or back from a back-off: one address byte on the bus
        if (i2c_master_probe(bus_h, ADDR, 5) != ESP_OK || !attach()) {
            present = false;
            t_next = now + (seen ? BACKOFF_US : ABSENT_US);
            return false;
        }
        present = seen = true;
        go(S_BOOT, now, STEP_US);
        return false;
    }
    if (now > t_dead) {                      // it answers but does not get on: a chip stuck in boot, or one that reset and lost its setup
        if (may_log(now)) ESP_LOGW(TAG, "VL53L1X: nothing at '%s' for too long, starting over", STEP[st]);
        restart(now);
        return false;
    }
    uint8_t b[RES_N];
    bool ok = false, got = false;
    switch (st) {
    case S_BOOT: {                           // bit 0: the chip's firmware is up
        ok = rd(R_BOOT, b, 1);
        if (!ok || !(b[0] & 1)) break;
        ok = rd(R_MODEL_ID, b, 2);
        if (!ok) break;
        unsigned id = (unsigned)(b[0] << 8 | b[1]);
        if (id != id_logged) {
            id_logged = id;
            if (id == 0xEACC) ESP_LOGI(TAG, "VL53L1X, model id 0x%04X", id);
            else ESP_LOGW(TAG, "model id 0x%04X is not a VL53L1X's 0xEACC: trying the VL53L1X bring-up anyway", id);
        }
        go(S_CONFIG, now, STEP_US);
        break;
    }
    case S_CONFIG:                           // one write of 91 registers; its last byte stops a chip that was still ranging
        ok = wr(CFG, sizeof CFG);
        if (ok) go(S_START, now, STEP_US);
        break;
    case S_START:                            // ST's CheckForDataReady reads the polarity every time; it only changes with the configuration
        ok = rd(R_GPIO_MUX, b, 1) && wr8(R_MODE_START, 0x40);
        if (ok) { pol = !(b[0] & 0x10); go(S_FIRST, now, DATA_US); }
        break;
    case S_FIRST:                            // SensorInit's first measurement, at ST's defaults (about 100 ms)
        ok = rd(R_GPIO_STATUS, b, 1);
        if (ok && (b[0] & 1) == pol) go(S_VHV, now, STEP_US);
        break;
    case S_VHV:                              // clear, stop, "two bounds VHV", "start VHV from the previous temperature"
        ok = wr8(R_INT_CLEAR, 0x01) && wr8(R_MODE_START, 0x00) && wr8(R_VHV_LOOP_BOUND, 0x09) && wr8(R_VHV_INIT, 0);
        if (ok) go(S_MODE, now, STEP_US);
        break;
    case S_MODE:                             // SetDistanceMode(1): short
        ok = wr8(R_PHASECAL, 0x14) && wr8(R_VCSEL_A, 0x07) && wr8(R_VCSEL_B, 0x05) && wr8(R_PHASE_HIGH, 0x38)
          && wr16(R_WOI_SD0, 0x0705) && wr16(R_PHASE_SD0, 0x0606);
        if (ok) go(S_TIMING, now, STEP_US);
        break;
    case S_TIMING: {                         // SetTimingBudgetInMs(20) in short mode, SetInterMeasurementInMs(25), StartRanging
        ok = wr16(R_TIMEOUT_A, 0x0051) && wr16(R_TIMEOUT_B, 0x006E) && rd(R_OSC_CAL, b, 2);
        if (!ok) break;
        // ST: (uint32_t)(ClockPLL * InterMeasMs * 1.075), ClockPLL being the low ten bits of the chip's oscillator
        // calibration. The same in integers: 1075/1000 truncates where the double does, and 1023*25*1075 fits.
        unsigned pll = (unsigned)(b[0] << 8 | b[1]) & 0x3FF;
        ok = wr32(R_PERIOD, (uint32_t)pll * PERIOD_MS * 1075 / 1000) && wr8(R_MODE_START, 0x40);
        if (!ok) break;
        ESP_LOGI(TAG, "ranging: short mode, %d ms budget, every %d ms (clock PLL %u)", BUDGET_MS, PERIOD_MS, pll);
        go(S_RANGE, now, DATA_US);
        break;
    }
    case S_RANGE:                            // one read when nothing is new; three transfers, 0.7 ms, for a measurement
        ok = rd(R_GPIO_STATUS, b, 1);
        if (!ok || (b[0] & 1) != pol) break;
        ok = rd(R_RESULT, b, RES_N) && wr8(R_INT_CLEAR, 0x01);       // a failed clear leaves it waiting: the next call reads it again
        if (!ok) break;
        if (mm) *mm = b[RES_MM] << 8 | b[RES_MM + 1];
        if (status) *status = (b[0] & 0x1F) < 24 ? STATUS[b[0] & 0x1F] : 255;
        t_dead = now + DATA_US;
        got = true;
        break;
    }
    if (!ok) { failed(now); return false; }
    streak = 0;
    return got;
}
