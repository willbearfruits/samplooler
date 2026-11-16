// Host test for firmware/main/tof.c: the driver as the firmware builds it, against a fake VL53L1X behind the I2C
// calls, on a clock this file owns. Every 10 ms tick is one tof_poll, as in the firmware's main loop, and every
// transfer moves the clock by its time on the bus, so "how long did that call take" is a number.
//
// What the fake knows comes from ST's Ultra Lite Driver, not from a chip: nobody has run this on hardware yet.
// What it makes up: all registers read 0 after power-up, the boot time, the oscillator value, and when the
// data-ready comes (one period after a start or a clear; `free_run` tries the other reading, a chip that measures
// on its own clock whether or not the last result was cleared).
//
// Each scenario runs in its own process, because the driver's state is static and several of them need a fresh one.
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "tof.h"
#include "esp_log.h"
#include "esp_timer.h"

#define NREG      0x200
#define NEVER     INT64_MAX
#define TICK_US   10000
#define XFER_US   50                         // a transfer's cost besides its bits: a guess at the driver's own time
#define MAX_OPS   6                          // tof.c's busiest call: the six writes of the distance mode
#define MAX_US    3000                       // "about 3 ms of bus time" on a healthy bus
enum { BUS_OK, BUS_NACK, BUS_TIMEOUT };

// ---------------------------------------------------------------------------------------------------------------
// ST's numbers, a second copy to hold tof.c against. The table is VL51L1X_DEFAULT_CONFIGURATION (registers 0x2D to
// 0x87) from ST's Ultra Lite Driver, vl53l1x_class.cpp as packaged by stm32duino:
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
static const uint8_t ST_CFG[91] = {
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

// VL53L1X_GetRangeStatus, as ST wrote it.
static int st_status(int raw)
{
    switch (raw & 0x1F) {
    case 9: return 0;   case 6: return 1;   case 4: return 2;    case 8: return 3;    case 5: return 4;
    case 3: return 5;   case 19: return 6;  case 7: return 7;    case 12: return 9;   case 18: return 10;
    case 22: return 11; case 23: return 12; case 13: return 13;  default: return 255;
    }
}

// VL53L1X_GetTimingBudgetInMs: the budget from TIMEOUT_MACROP_A, by distance mode (0x4B: 0x14 short, 0x0A long).
static int st_budget_ms(int phasecal, unsigned a)
{
    static const struct { unsigned a; int ms; } SHORT[] = { { 0x001D, 15 }, { 0x0051, 20 }, { 0x00D6, 33 }, { 0x01AE, 50 }, { 0x02E1, 100 }, { 0x03E1, 200 }, { 0x0591, 500 } },
                                                LONG[]  = { { 0x001E, 20 }, { 0x0060, 33 }, { 0x00AD, 50 }, { 0x01CC, 100 }, { 0x02D9, 200 }, { 0x048F, 500 } };
    if (phasecal == 0x14) { for (size_t i = 0; i < sizeof SHORT / sizeof *SHORT; i++) if (SHORT[i].a == a) return SHORT[i].ms; }
    if (phasecal == 0x0A) { for (size_t i = 0; i < sizeof LONG / sizeof *LONG; i++) if (LONG[i].a == a) return LONG[i].ms; }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The fake sensor and the bus.
static struct sim_bus { int unused; } the_bus;
static struct sim_dev { int unused; } the_dev;

static struct {
    int64_t now;                             // the clock, in microseconds
    int  bus;                                // BUS_OK, or every operation fails: BUS_NACK at once, BUS_TIMEOUT after the caller's timeout
    bool powered, stuck_boot, never_ready, free_run, low_pol, ignore_im;
    unsigned flaky, rnd;                     // flaky: one transfer in this many fails, at random
    int64_t boot_us, t_power, t_meas;        // t_meas: when the measurement under way is done
    uint8_t reg[NREG];
    bool wrote[NREG];                        // registers the driver has written since power-up
    bool ranging, irq;
    unsigned dist, raw, osc, model, stream;  // what the next measurement says; the oscillator calibration; the model id
    bool in_poll;
} F;

static struct {
    long ops, xfers, writes, probes, adds, cfg_writes, bus_resets, id_reads, boot_reads, polls, violations, latched;
    int  call_ops, call_clock, max_ops;
    int64_t max_took, sum_took;
    int  logs[4], warns;
    int64_t t_warn, warn_gap;                // the last warning and the shortest time between two
} C;

static int checks, failed, out_fd = -1;
static bool verbose;
static const char *scn = "";

static void check(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void check(bool ok, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    checks++;
    if (!ok) failed++;
    printf("%s [%s] ", ok ? "ok  " : "FAIL", scn);
    vprintf(fmt, ap);
    putchar('\n');
    va_end(ap);
}

static void finish(void)
{
    fflush(stdout);
    int c[2] = { checks, failed };
    if (write(out_fd, c, sizeof c) != (ssize_t)sizeof c) _exit(3);
    exit(0);
}

static void bail(const char *why) { check(false, "%s", why); finish(); }

static void violation(const char *what)
{
    if (C.violations++ < 5) printf("     [%s] broke the rules: %s (at %.3f s)\n", scn, what, (double)F.now / 1e6);
}

static unsigned reg16(int a) { return (unsigned)(F.reg[a] << 8 | F.reg[a + 1]); }
static uint32_t reg32(int a) { return (uint32_t)reg16(a) << 16 | reg16(a + 2); }
static bool booted(void) { return F.powered && !F.stuck_boot && F.now - F.t_power >= F.boot_us; }

// One measurement takes the larger of the timing budget and the inter-measurement period, both read back from the
// registers the way ST's getters do. 0: the registers are not something ST's tables know, and it never ranges.
static int64_t period_us(void)
{
    int budget = st_budget_ms(F.reg[0x4B], reg16(0x5E));
    unsigned pll = reg16(0xDE) & 0x3FF;
    double ms = budget, im = pll && !F.ignore_im ? reg32(0x6C) / (pll * 1.075) : 0.0;
    if (!budget) return 0;
    if (im > ms) ms = im;
    return (int64_t)(ms * 1000.0 + 0.5);
}

static void schedule(int64_t from) { int64_t p = period_us(); F.t_meas = p ? from + p : NEVER; }

static void power(bool on)
{
    F.powered = on; F.ranging = F.irq = false; F.t_meas = NEVER; F.t_power = F.now;
    memset(F.reg, 0, sizeof F.reg);
    memset(F.wrote, 0, sizeof F.wrote);
    F.reg[0x10F] = (uint8_t)(F.model >> 8); F.reg[0x110] = (uint8_t)F.model;
    F.reg[0xDE] = (uint8_t)(F.osc >> 8);    F.reg[0xDF] = (uint8_t)F.osc;
}

// Bring the sensor up to now: a finished measurement fills the result block and raises the interrupt. The bytes
// around the status and the distance are junk on purpose, so a wrong offset reads a wrong number.
static void sensor_run(void)
{
    while (F.powered && F.ranging && !F.never_ready && F.now >= F.t_meas) {
        for (int a = 0x89; a <= 0x99; a++) F.reg[a] = (uint8_t)(0xA0 + a - 0x89);
        F.reg[0x89] = (uint8_t)F.raw; F.reg[0x8B] = (uint8_t)++F.stream;
        F.reg[0x96] = (uint8_t)(F.dist >> 8); F.reg[0x97] = (uint8_t)F.dist;
        F.irq = true; C.latched++;
        if (F.free_run) schedule(F.t_meas); else F.t_meas = NEVER;
    }
}

static void reg_write(unsigned a, uint8_t v)
{
    if (a >= NREG) { violation("a write past the register map"); return; }
    if (!booted()) violation("a write before the chip has booted");
    F.reg[a] = v; F.wrote[a] = true;
    if (a == 0x30 && F.low_pol) F.reg[a] |= 0x10;          // a chip that insists on an active-low interrupt
    if (a == 0x86 && (v & 1)) { F.irq = false; if (F.ranging && !F.free_run && F.t_meas == NEVER) schedule(F.now); }
    if (a == 0x87) {
        if (v == 0x40) { F.ranging = true; schedule(F.now); }
        else if (v == 0x00) { F.ranging = false; F.t_meas = NEVER; }
        else violation("a mode-start value ST's driver never writes");
    }
}

static uint8_t reg_read(unsigned a)
{
    if (a >= NREG) { violation("a read past the register map"); return 0xFF; }
    if (a == 0xE5) { C.boot_reads++; return booted(); }
    if (a == 0x10F) C.id_reads++;
    if (a == 0x31) { int active = !(F.reg[0x30] & 0x10); return (uint8_t)((F.reg[0x31] & 0xFE) | (F.irq ? active : !active)); }
    return F.reg[a];
}

// Every bus operation: count it, catch a call that never ends, move the clock by what it takes.
static esp_err_t bus_op(int timeout_ms, int bytes, int hz, bool probe)
{
    C.ops++;
    if (++C.call_ops > 200) bail("a call is spinning on the bus");
    if (F.bus == BUS_TIMEOUT) { F.now += timeout_ms * 1000; return ESP_ERR_TIMEOUT; }
    if (F.flaky && !probe && ((F.rnd = F.rnd * 1664525u + 1013904223u) >> 16) % F.flaky == 0) { F.now += 9 * 1000000 / hz + XFER_US; return ESP_ERR_INVALID_STATE; }
    if (F.bus == BUS_NACK || !F.powered) { F.now += 9 * 1000000 / hz + XFER_US; return probe ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_STATE; }
    F.now += (int64_t)bytes * 9 * 1000000 / hz + XFER_US;
    sensor_run();
    return ESP_OK;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int ms)
{
    C.probes++;
    if (bus != &the_bus || address != 0x29) violation("a probe of something else");
    if (ms != (F.in_poll ? 5 : 20)) violation("a probe timeout that is not 20 ms at init, 5 ms in tof_poll");
    return bus_op(ms, 1, 100000, true);      // the driver probes at 100 kHz whatever the device's speed
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *ret)
{
    if (bus != &the_bus || cfg->dev_addr_length != I2C_ADDR_BIT_LEN_7 || cfg->device_address != 0x29 || cfg->scl_speed_hz != 400000)
        violation("the device is not 0x29, 7 bits, 400 kHz");
    if (C.adds++) violation("the device added twice");
    *ret = &the_dev;
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d, const uint8_t *wr, size_t n, int ms)
{
    C.xfers++; C.writes++;
    if (d != &the_dev || !C.adds) violation("a write to a device that was not added");
    if (ms != 10) violation("a transfer timeout that is not 10 ms");
    if (n < 3) { violation("a write with no data"); return ESP_FAIL; }
    esp_err_t e = bus_op(ms, 1 + (int)n, 400000, false);
    if (e != ESP_OK) return e;
    unsigned a = (unsigned)(wr[0] << 8 | wr[1]);
    if (a == 0x2D && n == 2 + sizeof ST_CFG) C.cfg_writes++;
    for (size_t i = 2; i < n; i++) reg_write(a++, wr[i]);
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d, const uint8_t *wr, size_t nw, uint8_t *rd, size_t nr, int ms)
{
    C.xfers++;
    if (d != &the_dev || !C.adds) violation("a read from a device that was not added");
    if (ms != 10) violation("a transfer timeout that is not 10 ms");
    if (nw != 2 || !nr) { violation("a read that does not send a 16-bit index first"); return ESP_FAIL; }
    esp_err_t e = bus_op(ms, 1 + 2 + 1 + (int)nr, 400000, false);
    if (e != ESP_OK) return e;
    unsigned a = (unsigned)(wr[0] << 8 | wr[1]);
    for (size_t i = 0; i < nr; i++) rd[i] = reg_read(a++);
    return ESP_OK;
}

esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus)
{
    if (bus != &the_bus) violation("a reset of another bus");
    C.bus_resets++;
    return ESP_OK;
}

int64_t esp_timer_get_time(void)
{
    if (++C.call_clock > 1000) bail("a call is waiting on the clock");
    return F.now;
}

void esp_log_level_set(const char *tag, esp_log_level_t level) { (void)tag; (void)level; }

void sim_log(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    C.logs[level]++;
    if (level <= ESP_LOG_WARN) {
        if (C.warns++ && F.now - C.t_warn < C.warn_gap) C.warn_gap = F.now - C.t_warn;
        C.t_warn = F.now;
    }
    if (verbose) {
        fprintf(stderr, "     %9.3f s  %c %s: ", (double)F.now / 1e6, "?EWI"[level], tag);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
    }
    va_end(ap);
}

// ---------------------------------------------------------------------------------------------------------------
// The firmware's loop.
static int64_t next_tick;

static bool step(int *mm, int *status)
{
    if (F.now < next_tick) F.now = next_tick;
    next_tick = F.now + TICK_US;             // a call that blocked past its tick is followed at once by the next
    sensor_run();
    int64_t t0 = F.now;
    F.in_poll = true; C.call_ops = C.call_clock = 0;
    bool got = tof_poll(mm, status);
    F.in_poll = false;
    C.polls++;
    if (C.call_ops > C.max_ops) C.max_ops = C.call_ops;
    if (F.now - t0 > C.max_took) C.max_took = F.now - t0;
    C.sum_took += F.now - t0;
    return got;
}

typedef struct { int n, mm, status, wrong; int64_t t_first, t_last; } got_t;

// Poll for a while. want_mm / want_status below 0: anything goes.
static got_t run_ms(int ms, int want_mm, int want_status)
{
    got_t g = { .mm = -1, .status = -1 };
    for (int i = 0; i < ms / 10; i++) {
        int mm = -7, st = -7;
        if (!step(&mm, &st)) continue;
        if (!g.n++) g.t_first = F.now;
        g.t_last = F.now; g.mm = mm; g.status = st;
        if ((want_mm >= 0 && mm != want_mm) || (want_status >= 0 && st != want_status)) g.wrong++;
    }
    return g;
}

static double interval_ms(got_t g) { return g.n > 1 ? (double)(g.t_last - g.t_first) / 1000.0 / (g.n - 1) : 0.0; }

static int wait_ready(int max)
{
    for (int i = 1; i <= max; i++) { int mm, st; step(&mm, &st); if (tof_ready()) return i; }
    return -1;
}

static void marks(void) { C.max_ops = 0; C.max_took = C.sum_took = 0; C.warns = 0; C.warn_gap = NEVER; }

// What ST's own calls would leave in the chip: SensorInit, SetDistanceMode(1), SetTimingBudgetInMs(20),
// SetInterMeasurementInMs(25), StartRanging. Returns how many registers differ, or were written without ST writing them.
static int image_diffs(void)
{
    static uint8_t want[NREG];
    static bool touch[NREG];
    memset(want, 0, sizeof want); memset(touch, 0, sizeof touch);
#define W8(a, v)  (want[a] = (uint8_t)(v), touch[a] = true)
#define W16(a, v) (W8(a, (v) >> 8), W8((a) + 1, v))
    for (int a = 0x2D; a <= 0x87; a++) W8(a, ST_CFG[a - 0x2D]);       // SensorInit: the table,
    W8(0x87, 0x40); W8(0x86, 0x01); W8(0x87, 0x00);                   // start, (data ready), clear, stop,
    W8(0x08, 0x09); W8(0x0B, 0x00);                                   // the two VHV writes
    W8(0x4B, 0x14); W8(0x60, 0x07); W8(0x63, 0x05); W8(0x69, 0x38); W16(0x78, 0x0705); W16(0x7A, 0x0606);   // short
    W16(0x5E, 0x0051); W16(0x61, 0x006E);                             // 20 ms in short mode
    uint32_t im = (uint32_t)((int)(F.osc & 0x3FF) * 25 * 1.075);      // ST's line, as written
    W16(0x6C, im >> 16); W16(0x6E, im);
    W8(0x87, 0x40);
    if (F.low_pol) want[0x30] |= 0x10;
    int d = 0;
    for (int a = 0; a < NREG; a++) {
        if (touch[a] ? F.wrote[a] && F.reg[a] == want[a] : !F.wrote[a]) continue;
        if (d++ < 8) printf("     [%s] 0x%04X: the chip has 0x%02X%s, ST's sequence leaves %s0x%02X\n", scn, (unsigned)a, F.reg[a],
                            F.wrote[a] ? "" : " (never written)", touch[a] ? "" : "it alone, ", want[a]);
    }
    return d;
}

static void check_image(const char *when)
{
    int d = image_diffs();
    check(d == 0 && F.ranging, "%s: the chip holds what ST's sequence leaves (table, short mode, 20 ms, 25 ms at PLL %u = %u, started); %d registers differ",
          when, F.osc & 0x3FF, (unsigned)reg32(0x6C), d);
}

static bool start(void)
{
    power(true);
    F.now = 5000;
    return tof_init(&the_bus);
}

// Init with the sensor there, and the bring-up. Leaves it ranging.
static void bring_up(void)
{
    bool found = start();
    int p = wait_ready(40);
    check(found && p > 0, "found at init, ranging after %d polls", p);
}

// ---------------------------------------------------------------------------------------------------------------
// The scenarios.
static void s_before_init(void)
{
    power(true);
    got_t g = run_ms(1000, -1, -1);
    check(g.n == 0 && C.ops == 0 && !tof_present() && !tof_ready() && tof_fails() == 0, "tof_poll before tof_init does nothing");
}

static void s_bring_up(void)
{
    F.boot_us = 40000;
    bool found = start();
    check(found && tof_present() && !tof_ready(), "tof_init finds it: present, not ready yet");
    check(C.probes == 1 && C.adds == 1 && C.xfers == 0, "tof_init is one probe and the device added, no transfer (%ld, %ld, %ld)", C.probes, C.adds, C.xfers);
    int p = wait_ready(40);
    check(p > 0, "ranging after %d polls (limit 40) with a chip that boots in 40 ms and takes 100 ms over ST's first range", p);
    check_image("after the bring-up");
    check(C.cfg_writes == 1 && C.writes == 16, "the configuration went as one 91-byte write, 16 writes in all (%ld, %ld)", C.cfg_writes, C.writes);
    check(C.id_reads == 1 && C.logs[ESP_LOG_INFO] == 2, "the model id was read once; two log lines (%ld, %d)", C.id_reads, C.logs[ESP_LOG_INFO]);
    check(C.max_ops <= MAX_OPS, "bring-up: no call did more than %d transfers (most: %d)", MAX_OPS, C.max_ops);
    check(C.max_took <= MAX_US, "bring-up: no call took more than %d us on the bus (longest: %lld us)", MAX_US, (long long)C.max_took);
    check(tof_fails() == 0 && C.warns == 0, "no failure counted, no warning (%d, %d)", tof_fails(), C.warns);

    marks();
    got_t g = run_ms(3000, 300, 0);
    check(g.n > 0 && g.wrong == 0, "%d measurements in 3 s, all 300 mm with status 0 (%d were not)", g.n, g.wrong);
    check(interval_ms(g) >= 25.0 && interval_ms(g) <= 31.0, "one every %.1f ms: the 25 ms period, seen on 10 ms polls, restarted by each clear", interval_ms(g));
    check(C.max_ops <= 3 && C.max_took <= 1000, "ranging: at most 3 transfers and %lld us a call (limits 3 and 1000)", (long long)C.max_took);
    check(C.sum_took <= 3000 * 40, "ranging costs %.2f %% of the time on the bus", (double)C.sum_took / 30000.0);

    F.dist = 1234;
    run_ms(60, -1, -1);                      // the one already measured
    g = run_ms(300, 1234, 0);
    check(g.n >= 8 && g.wrong == 0, "a new distance arrives: %d measurements of 1234 mm (%d were not)", g.n, g.wrong);
    F.dist = 0;      run_ms(60, -1, -1); g = run_ms(100, 0, 0);
    bool zero = g.n > 0 && !g.wrong;
    F.dist = 0xFFFF; run_ms(60, -1, -1); g = run_ms(100, 65535, 0);
    check(zero && g.n > 0 && !g.wrong, "0 mm and 65535 mm come through as they are");

    int bad = 0, missing = 0;
    for (int i = 0; i < 35; i++) {           // every code the chip can give, and three with the upper bits set
        static const int HIGH[] = { 0x29, 0xE6, 0x93 };
        int raw = i < 32 ? i : HIGH[i - 32], seen = -1;
        F.raw = (unsigned)raw; F.dist = (unsigned)(2000 + i);
        for (int k = 0; k < 12 && seen < 0; k++) { int mm = -1, st = -1; if (step(&mm, &st) && mm == 2000 + i) seen = st; }
        if (seen < 0) missing++; else if (seen != st_status(raw)) { bad++; printf("     [%s] raw status %d gave %d, ST's table says %d\n", scn, raw, seen, st_status(raw)); }
    }
    check(!bad && !missing, "the range status maps as in ST's GetRangeStatus for all 32 codes (%d wrong, %d never arrived)", bad, missing);
    check_image("after ranging");
    check(C.cfg_writes == 1 && C.probes == 1 && tof_fails() == 0, "nothing was set up twice");
}

static void s_free_run(void)
{
    F.free_run = true;
    bring_up();
    got_t g = run_ms(3000, 300, 0);
    check(g.n > 0 && g.wrong == 0 && interval_ms(g) >= 24.5 && interval_ms(g) <= 25.5,
          "a chip that measures on its own clock: %d measurements, one every %.1f ms", g.n, interval_ms(g));
    check(C.max_ops <= MAX_OPS && C.max_took <= MAX_US, "no call over %d transfers or %d us (%d, %lld)", MAX_OPS, MAX_US, C.max_ops, (long long)C.max_took);
}

static void s_low_polarity(void)
{
    F.low_pol = true;
    bring_up();
    check_image("active-low interrupt");
    got_t g = run_ms(1000, 300, 0);
    check(g.n >= 30 && g.wrong == 0, "the polarity read at bring-up is used: %d measurements in 1 s with the interrupt active low", g.n);
}

static void s_unknown_id(void)
{
    F.model = 0xEBAA;
    bring_up();
    got_t g = run_ms(500, 300, 0);
    check(C.warns == 1 && g.n >= 15 && g.wrong == 0, "model id 0xEBAA: one warning, then the same bring-up and %d measurements", g.n);
}

static void s_glitch(void)
{
    bring_up();
    run_ms(300, 300, 0);
    F.bus = BUS_NACK;
    got_t g = run_ms(50, -1, -1);
    check(g.n == 0 && tof_fails() == 5 && tof_ready(), "five failed calls: counted (%d), still ready, no back-off", tof_fails());
    F.bus = BUS_OK;
    g = run_ms(100, 300, 0);
    check(g.n >= 1 && g.wrong == 0 && C.cfg_writes == 1 && C.probes == 1 && C.bus_resets == 0, "and it carries on where it was: %d measurements in the next 100 ms", g.n);
}

static void s_unplugged(void)
{
    bring_up();
    run_ms(300, 300, 0);
    long ops = C.ops;
    power(false);
    marks();
    got_t g = run_ms(3000, -1, -1);
    check(g.n == 0 && !tof_ready() && !tof_present(), "unplugged: no measurement, not ready, not present");
    check(tof_fails() == 10 && C.bus_resets == 1, "ten failed calls counted, then the back-off and one bus reset (%d, %ld)", tof_fails(), C.bus_resets);
    check(C.ops - ops <= 17, "3 s unplugged cost %ld bus operations (10 transfers, then a probe every 500 ms)", C.ops - ops);
    check(C.max_took <= 500 && C.warns >= 1 && C.warns <= 2 && C.warn_gap >= 2000000, "no call over 500 us (%lld); %d warnings, 2 s apart at least", (long long)C.max_took, C.warns);
    power(true);
    int p = wait_ready(100);
    check(p > 0 && tof_present(), "plugged back in: ranging after %d polls (limit 100)", p);
    check(C.cfg_writes == 2 && C.adds == 1, "the configuration was written again, the device not added again (%ld, %ld)", C.cfg_writes, C.adds);
    check_image("after the replug");
    g = run_ms(500, 300, 0);
    check(g.n >= 15 && g.wrong == 0, "%d measurements in the next 0.5 s", g.n);
}

static void s_bus_dead(void)
{
    bring_up();
    run_ms(300, 300, 0);
    long ops = C.ops;
    F.bus = BUS_TIMEOUT;                     // SDA held low: every transfer runs into its timeout
    marks();
    got_t g = run_ms(3000, -1, -1);
    check(g.n == 0 && !tof_ready(), "every transfer timing out: no measurement, not ready");
    check(tof_fails() >= 10 && tof_fails() <= 16 && C.bus_resets == 1, "%d failed transfers counted, one bus reset", tof_fails());
    check(C.ops - ops <= 17, "it backs off: %ld bus operations in 300 polls", C.ops - ops);
    check(C.max_took <= 10000, "a call blocks for one 10 ms timeout at most (%lld us)", (long long)C.max_took);
    check(C.sum_took <= 150000, "%lld ms blocked in all: ten timeouts, then 5 ms of probe every 500 ms", (long long)C.sum_took / 1000);
    check(C.warns >= 1 && C.warns <= 2 && C.warn_gap >= 2000000, "%d warnings, 2 s apart at least", C.warns);
    power(false); power(true);               // and it lost power meanwhile: the registers are back at reset
    F.bus = BUS_OK;
    int p = wait_ready(100);
    check(p > 0 && tof_present(), "the bus is back: ranging after %d polls (limit 100)", p);
    check(C.cfg_writes == 2, "with the configuration written again");
    check_image("after the bus came back");
    g = run_ms(500, 300, 0);
    check(g.n >= 15 && g.wrong == 0, "%d measurements in the next 0.5 s", g.n);
}

static void s_still_ranging(void)
{
    bring_up();
    run_ms(300, 300, 0);
    F.bus = BUS_NACK;                        // the bus fails, the chip keeps its power and goes on ranging
    run_ms(1000, -1, -1);
    F.bus = BUS_OK;
    int p = wait_ready(100);
    check(p > 0 && C.cfg_writes == 2, "a chip that kept ranging through the outage is set up again: ranging after %d polls", p);
    check_image("after the outage");
    F.dist = 777;
    run_ms(60, -1, -1);
    got_t g = run_ms(300, 777, 0);
    check(g.n >= 8 && g.wrong == 0, "%d fresh measurements", g.n);
}

static void s_brownout(void)
{
    bring_up();
    run_ms(300, 300, 0);
    power(false);
    run_ms(30, -1, -1);
    power(true);                             // back before ten calls failed, with its setup gone
    int p = 0, early = 0;
    while (p < 300 && !(C.cfg_writes == 2 && tof_ready())) { int mm, st; early += step(&mm, &st) && C.cfg_writes < 2; p++; }
    check(tof_fails() == 3 && p < 300, "a 30 ms power dip (3 failed calls): the silence is noticed, ranging again after %d polls (limit 300)", p);
    check(early == 0, "nothing was handed out as a measurement meanwhile");
    check_image("after the dip");
    got_t g = run_ms(500, 300, 0);
    check(g.n >= 15 && g.wrong == 0, "%d measurements in the next 0.5 s", g.n);
}

// A bad connection: one transfer in six fails, anywhere in the bring-up or between the result and its clear.
static void s_flaky(void)
{
    F.flaky = 6;
    bool found = start();
    int p = wait_ready(1000);
    check(found && p > 0, "one transfer in six failing: the bring-up still gets through, after %d polls (limit 1000)", p);
    long latched = C.latched;
    got_t g = run_ms(20000, 300, 0);
    check(g.n >= 200 && g.wrong == 0, "%d measurements in 20 s, all 300 mm with status 0 (%d were not)", g.n, g.wrong);
    check(g.n <= C.latched - latched + 1, "none handed out twice: %d out, %ld measured", g.n, C.latched - latched);
    check(tof_fails() > 100 && C.max_ops <= MAX_OPS && C.max_took <= MAX_US, "%d failures counted; no call over %d transfers or %d us (%d, %lld)",
          tof_fails(), MAX_OPS, MAX_US, C.max_ops, (long long)C.max_took);
    F.flaky = 0;
    p = wait_ready(200);
    run_ms(100, -1, -1);
    check(p > 0, "the connection holds again: ranging");
    check_image("after the bad connection");
}

static void s_absent(void)
{
    F.bus = BUS_TIMEOUT;                     // first with a bus that hangs, the worst an absent sensor can cost
    F.now = 5000;
    bool found = tof_init(&the_bus);
    check(!found && !tof_present() && F.now - 5000 <= 20000, "nothing there: tof_init says so within 20 ms (%lld ms)", (long long)(F.now - 5000) / 1000);
    marks();
    run_ms(4000, -1, -1);
    check(C.probes - 1 <= 2 && C.xfers == 0 && C.max_took <= 5000 && C.sum_took <= 10000,
          "absent on a hanging bus: %ld probes in 4 s, %lld ms blocked in all, 5 ms at most a call", C.probes - 1, (long long)C.sum_took / 1000);
    F.bus = BUS_OK;
    long probes = C.probes;
    marks();
    run_ms(4000, -1, -1);
    check(C.probes - probes <= 2 && C.xfers == 0 && C.max_took <= 200, "absent on a good bus: %ld probes in 4 s, %lld us the longest call", C.probes - probes, (long long)C.max_took);
    check(C.logs[ESP_LOG_WARN] + C.logs[ESP_LOG_ERROR] == 1 && tof_fails() == 0 && !tof_ready(), "one line said so at init, none since; no failure counted");
    F.osc = 0xFFAB;                          // upper bits set: only the low ten are the PLL
    power(true);
    int p = wait_ready(240);
    check(p > 0 && tof_present() && C.adds == 1, "plugged in later: ranging after %d polls (limit 240: the 2 s between probes and the bring-up)", p);
    check_image("after the late plug-in");
    got_t g = run_ms(500, 300, 0);
    check(g.n >= 15 && g.wrong == 0, "%d measurements in the next 0.5 s", g.n);
    check(C.max_ops <= MAX_OPS && C.max_took <= MAX_US, "no call over %d transfers or %d us (%d, %lld)", MAX_OPS, MAX_US, C.max_ops, (long long)C.max_took);
}

static void s_stuck_boot(void)
{
    F.stuck_boot = true;
    bool found = start();
    got_t g = run_ms(8000, -1, -1);
    long reads = C.boot_reads;
    got_t h = run_ms(2000, -1, -1);
    check(found && g.n + h.n == 0 && !tof_ready(), "a chip that never boots: found, never ready, no measurement");
    check(C.boot_reads > reads && C.probes >= 6, "it keeps trying: %ld bring-ups started in 10 s, still asking in the last two", C.probes);
    check(C.max_ops <= 1 && C.max_took <= 500, "no call hangs: one transfer and %lld us at most", (long long)C.max_took);
    check(C.writes == 0, "nothing is written to a chip that has not booted");
    check(C.warns >= 1 && C.warns <= 6 && C.warn_gap >= 2000000, "%d warnings, 2 s apart at least", C.warns);
    F.stuck_boot = false;
    int p = wait_ready(100);
    check(p > 0, "it boots after all: ranging after %d polls (limit 100)", p);
    check_image("after the late boot");
}

static void s_never_ready(void)
{
    F.never_ready = true;
    bool found = start();
    got_t g = run_ms(6000, -1, -1);
    check(found && g.n == 0 && !tof_ready(), "a chip whose first range never comes: never ready, no measurement");
    check(C.cfg_writes >= 3 && C.cfg_writes <= 5, "the whole bring-up is tried again: %ld times in 6 s", C.cfg_writes);
    check(C.max_ops <= MAX_OPS && C.max_took <= MAX_US && C.warns >= 1 && C.warn_gap >= 2000000,
          "no call over %d transfers or %d us (%d, %lld); %d warnings, 2 s apart at least", MAX_OPS, MAX_US, C.max_ops, (long long)C.max_took, C.warns);
    F.never_ready = false;
    int p = wait_ready(120);
    check(p > 0, "it ranges after all: ready after %d polls (limit 120)", p);
    check_image("after the late first range");
}

// Every PLL value the ten bits can hold, each after a power cycle: the driver's integer arithmetic against ST's
// line in double. The fake's own timing ignores the period here, or a tiny PLL would make ST's first range last minutes.
static void s_pll_sweep(void)
{
    F.ignore_im = true;
    F.osc = 0;
    start();
    int bad = 0, stuck = 0;
    for (unsigned pll = 0; pll < 1024; pll++) {
        if (pll) { power(false); run_ms(150, -1, -1); F.osc = pll | (pll & 1 ? 0xFC00 : 0); power(true); }
        if (wait_ready(150) < 0) { stuck++; continue; }
        if (image_diffs()) bad++;
    }
    check(!bad && !stuck, "the inter-measurement register equals ST's (uint32_t)(PLL * 25 * 1.075) for all 1024 PLL values (%d wrong, %d bring-ups stuck)", bad, stuck);
    check(C.adds == 1 && C.cfg_writes == 1024, "1024 bring-ups, one device");
}

static void scenario(const char *name, void (*fn)(void))
{
    int fd[2];
    fflush(stdout);
    if (pipe(fd)) { perror("pipe"); exit(2); }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (!pid) {
        close(fd[0]);
        out_fd = fd[1]; scn = name; checks = failed = 0;
        F.boot_us = 2000; F.dist = 300; F.raw = 9; F.osc = 0x03B6; F.model = 0xEACC; F.t_meas = NEVER;
        C.warn_gap = NEVER;
        fn();
        check(C.violations == 0, "the driver kept to the bus rules throughout (%ld broken)", C.violations);
        finish();
    }
    close(fd[1]);
    int c[2] = { 0, 0 }, ws = 0;
    bool told = read(fd[0], c, sizeof c) == (ssize_t)sizeof c;
    close(fd[0]);
    waitpid(pid, &ws, 0);
    checks += c[0]; failed += c[1];
    if (!told || !WIFEXITED(ws) || WEXITSTATUS(ws)) { checks++; failed++; printf("FAIL [%s] the scenario died\n", name); }
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    scenario("before init", s_before_init);
    scenario("bring-up", s_bring_up);
    scenario("free-running", s_free_run);
    scenario("active low", s_low_polarity);
    scenario("other model", s_unknown_id);
    scenario("glitch", s_glitch);
    scenario("unplugged", s_unplugged);
    scenario("bus dead", s_bus_dead);
    scenario("outage", s_still_ranging);
    scenario("power dip", s_brownout);
    scenario("bad connection", s_flaky);
    scenario("absent", s_absent);
    scenario("stuck in boot", s_stuck_boot);
    scenario("no first range", s_never_ready);
    scenario("pll sweep", s_pll_sweep);
    printf("%d checks, %d failed\n", checks, failed);
    return failed != 0;
}
