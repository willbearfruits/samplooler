#include "usb.h"
#if HAS_USB
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "soc/rtc_cntl_reg.h"
#include "tusb.h"
#include "usb_device_uac.h"
#include "usb/usb_itf.h"
#include "recorder.h"

static const char *TAG = "usb";

// ---- audio rings: single producer, single consumer, mono int16 ----
#define RING_N 4096                          // frames, 128 ms at 32 kHz
typedef struct { int16_t buf[RING_N]; atomic_uint wr, rd; } ring_t;
static ring_t to_host, from_host;
static volatile bool in_active;

static inline unsigned ring_count(const ring_t *r) { return atomic_load(&r->wr) - atomic_load(&r->rd); }
static inline void ring_push(ring_t *r, int16_t v) { unsigned w = atomic_load(&r->wr); r->buf[w % RING_N] = v; atomic_store(&r->wr, w + 1); }
static inline int16_t ring_pop(ring_t *r) { unsigned d = atomic_load(&r->rd); int16_t v = r->buf[d % RING_N]; atomic_store(&r->rd, d + 1); return v; }

void IRAM_ATTR usb_audio_out_push(const int16_t *mono, int n)
{
    if (ring_count(&to_host) + n > RING_N) return;          // host not draining: drop the block
    for (int i = 0; i < n; i++) ring_push(&to_host, mono[i]);
}

int IRAM_ATTR usb_audio_in_pull(int16_t *mono, int n)
{
    unsigned have = ring_count(&from_host);
    int got = have < (unsigned)n ? (int)have : n;
    for (int i = 0; i < got; i++) mono[i] = ring_pop(&from_host);
    for (int i = got; i < n; i++) mono[i] = 0;
    return got;
}
bool usb_audio_in_active(void) { return in_active; }

// Host -> device (its "speaker"): stereo 16-bit; average to mono into the ring.
static esp_err_t spk_cb(uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    const int16_t *s = (const int16_t *)buf;
    size_t frames = len / 4;
    in_active = true;
    if (ring_count(&from_host) + frames > RING_N) return ESP_OK;     // audio task slower than host: drop
    for (size_t i = 0; i < frames; i++) ring_push(&from_host, (int16_t)(((int32_t)s[2 * i] + s[2 * i + 1]) >> 1));
    return ESP_OK;
}

// Device -> host (its "microphone"): asked for len bytes of stereo 16-bit every 10 ms.
static esp_err_t mic_cb(uint8_t *buf, size_t len, size_t *bytes_read, void *ctx)
{
    (void)ctx;
    int16_t *d = (int16_t *)buf;
    size_t frames = len / 4;
    unsigned have = ring_count(&to_host);
    // Drift guard: if the ring runs high, drop one frame; if low, repeat one.
    if (have > RING_N * 3 / 4 && have > frames + 1) { ring_pop(&to_host); have--; }
    for (size_t i = 0; i < frames; i++) {
        int16_t v = have ? (have--, ring_pop(&to_host)) : 0;
        d[2 * i] = v; d[2 * i + 1] = v;
    }
    *bytes_read = frames * 4;
    return ESP_OK;
}

// ---- MIDI ----
void usb_midi_send_cc(uint8_t cc, uint8_t val)
{
    if (!tud_midi_mounted()) return;
    uint8_t m[3] = { 0xB0, cc & 0x7F, val & 0x7F };
    tud_midi_stream_write(0, m, 3);
}
void usb_midi_send_note(uint8_t note, uint8_t vel, bool on)
{
    if (!tud_midi_mounted()) return;
    uint8_t m[3] = { on ? 0x90 : 0x80, note & 0x7F, vel & 0x7F };
    tud_midi_stream_write(0, m, 3);
}

static void reboot_to_download(void)
{
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

// Byte-stream parser: channel messages to the handler, our SysEx commands here.
void usb_midi_poll(usb_midi_handler_t h)
{
    static uint8_t st, d[2], nd;
    static bool in_sysex; static uint8_t sx[16]; static int sxn;
    uint8_t buf[64];
    uint32_t n;
    while ((n = tud_midi_stream_read(buf, sizeof buf)) > 0) {
        for (uint32_t i = 0; i < n; i++) {
            uint8_t b = buf[i];
            if (b == 0xF0) { in_sysex = true; sxn = 0; continue; }
            if (b == 0xF7) {
                in_sysex = false;
                if (sxn >= 2 && sx[0] == 0x7D && sx[1] == 'R') { ESP_LOGW(TAG, "reboot to download mode"); vTaskDelay(pdMS_TO_TICKS(50)); reboot_to_download(); }
                continue;
            }
            if (in_sysex) { if (sxn < (int)sizeof sx) sx[sxn++] = b; continue; }
            if (b & 0x80) { st = b; nd = 0; continue; }
            if (st == 0) continue;
            uint8_t type = st & 0xF0;
            if (type == 0xC0 || type == 0xD0) continue;            // one data byte, unused
            d[nd++] = b;
            if (nd == 2) { nd = 0; if (type == 0x90 || type == 0x80 || type == 0xB0) h(type, d[0], d[1]); }
        }
    }
}

// ---- logs as SysEx: F0 7D 4C <7-bit text> F7 ----
static int midi_log_vprintf(const char *fmt, va_list ap)
{
    char line[200];
    int n = vsnprintf(line, sizeof line, fmt, ap);
    if (n <= 0) return n;
    if (n > (int)sizeof line - 1) n = sizeof line - 1;
    if (!tud_midi_mounted()) return n;
    uint8_t pkt[204]; int k = 0;
    pkt[k++] = 0xF0; pkt[k++] = 0x7D; pkt[k++] = 'L';
    for (int i = 0; i < n; i++) { char c = line[i]; if (c == '\n' || c == '\r') continue; pkt[k++] = (uint8_t)c & 0x7F; }
    pkt[k++] = 0xF7;
    if (tud_midi_stream_write(0, pkt, k) < (uint32_t)k) { /* buffer full: the line is lost, never block */ }
    return n;
}

bool usb_connected(void) { return tud_mounted(); }

bool usb_start(void)
{
    uac_device_config_t cfg = {
        .skip_tinyusb_init = false,            // the component brings up the PHY, tusb and its task
        .output_cb = spk_cb, .input_cb = mic_cb,
        .spk_itf_num = ITF_AUDIO_SPK, .mic_itf_num = ITF_AUDIO_MIC,
    };
    if (uac_device_init(&cfg) != ESP_OK) { ESP_LOGE(TAG, "USB init failed"); return false; }
    esp_log_set_vprintf(midi_log_vprintf);     // the Serial/JTAG console is gone now; logs ride on SysEx
    ESP_LOGI(TAG, "USB composite up: MIDI + audio %d Hz, logs over SysEx", REC_SAMPLE_RATE);
    return true;
}
#endif
