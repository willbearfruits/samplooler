#include "oled.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "font5x7.h"

static const char *TAG = "oled";
#define ADDR        0x3C
#define PAGES       (OLED_H / 8)
#if OLED_W == 72
#define COL_OFFSET  28          // the 72-wide panel sits at RAM columns 28..99
#define MUX         0x27        // 40 lines
#define COMPINS     0x12
#else
#define COL_OFFSET  0
#define MUX         (OLED_H - 1)
#define COMPINS     (OLED_H == 64 ? 0x12 : 0x02)
#endif

static i2c_master_dev_handle_t dev;
static bool present;
static int  fails;                          // consecutive failed flushes
static int64_t t_retry;                     // when to probe again after giving up
#define FAIL_LIMIT 3
#define RETRY_US   2000000
static uint8_t fb[PAGES][OLED_W];
static uint8_t txbuf[1 + PAGES * OLED_W];

static bool cmd(const uint8_t *c, int n)
{
    uint8_t b[16]; b[0] = 0x00;                 // control byte: commands follow
    if (n > 15) n = 15;
    memcpy(b + 1, c, n);
    return i2c_master_transmit(dev, b, n + 1, 50) == ESP_OK;
}

static bool bring_up(void)
{
    // 72x40 sequence as u8g2's ssd1306_72x40 (40-line multiplex, no display offset);
    // 128x64 is the standard SSD1306 bring-up. Both use the internal charge pump.
    static const uint8_t init[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock divide / oscillator
        0xA8, MUX,        // multiplex
        0xD3, 0x00,       // display offset 0
#if OLED_W == 72
        0xAD, 0x30,       // internal IREF (0.42" panel)
#endif
        0x8D, 0x14,       // charge pump on
        0x40,             // start line 0
        0xA6,             // normal (not inverted)
        0xA4,             // follow RAM
        0x20, 0x00,       // horizontal addressing
        0xA1,             // segment remap
        0xC8,             // COM scan reversed
        0xDA, COMPINS,    // COM pins
        0x81, 0xAF,       // contrast
        0xD9, 0x22,       // precharge
        0xDB, 0x20,       // VCOMH
        0x2E,             // no scroll
        0xAF,             // display on
    };
    for (int i = 0; i < (int)sizeof init; ) {
        int n = (int)sizeof init - i; if (n > 15) n = 15;
        if (!cmd(init + i, n)) { ESP_LOGW(TAG, "init failed"); return false; }
        i += n;
    }
    present = true; fails = 0;
    oled_clear(); oled_flush();
    ESP_LOGI(TAG, "%dx%d ready", OLED_W, OLED_H);
    return true;
}

bool oled_init(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, ADDR, 20) != ESP_OK) { ESP_LOGW(TAG, "no SSD1306 at 0x3C"); return false; }
    i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADDR, .scl_speed_hz = 400000 };
    if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) return false;
    return bring_up();
}

// A panel that stops answering (loose wire, or a brown-out on its side) is dropped after FAIL_LIMIT flushes,
// so the UI loop is not held up by I2C timeouts, and probed again every two seconds.
bool oled_present(void)
{
    if (present || !dev) return present;
    int64_t now = esp_timer_get_time();
    if (now < t_retry) return false;
    t_retry = now + RETRY_US;
    if (bring_up()) ESP_LOGI(TAG, "panel is back");
    return present;
}
void oled_clear(void)   { memset(fb, 0, sizeof fb); }

void oled_pixel(int x, int y, bool on)
{
    if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
    if (on) fb[y >> 3][x] |= 1 << (y & 7); else fb[y >> 3][x] &= ~(1 << (y & 7));
}

bool oled_get(int x, int y)
{
    if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return false;
    return (fb[y >> 3][x] >> (y & 7)) & 1;
}

void oled_rect(int x, int y, int w, int h, bool on)
{
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) oled_pixel(i, j, on);
}

void oled_frame(int x, int y, int w, int h)
{
    oled_rect(x, y, w, 1, true); oled_rect(x, y + h - 1, w, 1, true);
    oled_rect(x, y, 1, h, true); oled_rect(x + w - 1, y, 1, h, true);
}

void oled_text(int x, int y, const char *s)
{
    for (; *s; s++, x += 6) {
        unsigned c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7F) c = '?';
        const uint8_t *g = font5x7[c - 0x20];
        for (int i = 0; i < 5; i++)
            for (int j = 0; j < 7; j++)
                if (g[i] & (1 << j)) oled_pixel(x + i, y + j, true);
    }
}

void oled_text2(int x, int y, const char *s)
{
    for (; *s; s++, x += 12) {
        unsigned c = (unsigned char)*s;
        if (c < 0x20 || c > 0x7F) c = '?';
        const uint8_t *g = font5x7[c - 0x20];
        for (int i = 0; i < 5; i++)
            for (int j = 0; j < 7; j++)
                if (g[i] & (1 << j)) oled_rect(x + 2 * i, y + 2 * j, 2, 2, true);
    }
}

void oled_line(int x0, int y0, int x1, int y1, bool on)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;   // dy negative
    int err = dx + dy;
    while (1) {
        oled_pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void oled_circle(int cx, int cy, int r, bool on)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        oled_pixel(cx + x, cy + y, on); oled_pixel(cx - x, cy + y, on); oled_pixel(cx + x, cy - y, on); oled_pixel(cx - x, cy - y, on);
        oled_pixel(cx + y, cy + x, on); oled_pixel(cx - y, cy + x, on); oled_pixel(cx + y, cy - x, on); oled_pixel(cx - y, cy - x, on);
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
    }
}

void oled_disc(int cx, int cy, int r, bool on)
{
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) oled_pixel(cx + i, cy + j, on);
}

void oled_invert(int x, int y, int w, int h)
{
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) oled_pixel(i, j, !oled_get(i, j));
}

void oled_flush(void)
{
    if (!present) return;
    const uint8_t win[] = { 0x21, COL_OFFSET, COL_OFFSET + OLED_W - 1, 0x22, 0x00, PAGES - 1 };
    bool ok = cmd(win, sizeof win);
    if (ok) {
        txbuf[0] = 0x40;                        // control byte: data follows
        memcpy(txbuf + 1, fb, sizeof fb);
        ok = i2c_master_transmit(dev, txbuf, sizeof txbuf, 100) == ESP_OK;   // 361 bytes (72x40) or 1025 (128x64): 9 or 26 ms at 400 kHz
    }
    if (ok) { fails = 0; return; }
    if (++fails >= FAIL_LIMIT) {
        present = false; t_retry = esp_timer_get_time() + RETRY_US;
        ESP_LOGW(TAG, "panel stopped answering: check its SDA/SCL/power; retrying every 2 s");
    }
}
