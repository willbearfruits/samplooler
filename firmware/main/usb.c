#include "usb.h"
#if HAS_USB
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_mac.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_serial_jtag_reg.h"
#include "hal/usb_serial_jtag_ll.h"
#include "esp_private/periph_ctrl.h"
#include "driver/gpio.h"
#include "tusb.h"
#include "usb_device_uac.h"
#include "usb/usb_itf.h"
#include "recorder.h"
#include "diag.h"

static const char *TAG = "usb";
extern char usb_serial[];                    // usb_descriptors.c; filled from the chip's MAC before enumeration
void app_prepare_reboot(void);               // main.c: flush settings, mute

// One lock for every writer of the MIDI IN endpoint: the UI task's CCs and notes, and log lines from any
// task (TinyUSB's FIFO has one producer by design). A mutex, not a spinlock: the write can claim the
// endpoint, which takes TinyUSB's own mutex. A writer that cannot get it in time drops its message.
static SemaphoreHandle_t midi_lock;
static uint32_t midi_write(const uint8_t *m, uint32_t n, TickType_t wait)
{
    if (!midi_lock || xSemaphoreTake(midi_lock, wait) != pdTRUE) return 0;
    uint32_t w = tud_midi_stream_write(0, m, n);
    xSemaphoreGive(midi_lock);
    return w;
}

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
    midi_write(m, 3, 1);
}
void usb_midi_send_note(uint8_t note, uint8_t vel, bool on)
{
    if (!tud_midi_mounted()) return;
    uint8_t m[3] = { on ? 0x90 : 0x80, note & 0x7F, vel & 0x7F };
    midi_write(m, 3, 1);
}

// Host-driven flashing with no buttons: detach from the bus so the host forgets this device, hand the USB pads
// back to the built-in Serial/JTAG controller, ask the ROM for download mode on the next reset, and restart.
// The ROM then enumerates as 303a:1001 and esptool works as usual. The bare "force download boot + restart"
// left the host confused often enough (same sequence as Arduino-ESP32's usb_persist_restart).
static void reboot_to_download(void)
{
    app_prepare_reboot();
    diag_crumb("bl detach");
    tud_disconnect();                                   // OTG pull-up off
    vTaskDelay(pdMS_TO_TICKS(20));
    // SE0 for 100 ms: D-/D+ (GPIO19/20) driven low as plain GPIOs, so the host registers a clean detach
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL | RTC_CNTL_USB_PAD_ENABLE);
    PERIPH_RCC_ATOMIC() {                               // IDF gated the USJ clock at boot (USJ unused)
        usb_serial_jtag_ll_enable_bus_clock(true);
        usb_serial_jtag_ll_reset_register();
    }
    usb_serial_jtag_ll_phy_enable_pad(false);
    usb_serial_jtag_ll_phy_enable_external(false);      // internal PHY
    gpio_set_direction(GPIO_NUM_19, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(GPIO_NUM_20, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(GPIO_NUM_19, 0);
    gpio_set_level(GPIO_NUM_20, 0);
    diag_crumb("bl se0");
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_reset_pin(GPIO_NUM_19);
    gpio_reset_pin(GPIO_NUM_20);
    // hand the pads to USB-Serial/JTAG and wait (<= 1 s) for the host to reset the bus: it has seen us
    REG_WRITE(USB_SERIAL_JTAG_INT_CLR_REG, USB_SERIAL_JTAG_USB_BUS_RESET_INT_CLR);
    usb_serial_jtag_ll_phy_enable_pad(true);
    diag_crumb("bl usj");
    for (int i = 0; i < 100; i++) {
        if (REG_READ(USB_SERIAL_JTAG_INT_RAW_REG) & USB_SERIAL_JTAG_USB_BUS_RESET_INT_RAW) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    diag_crumb("bl restart");
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);   // consumed by the ROM on the next boot
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
                else if (sxn >= 2 && sx[0] == 0x7D && sx[1] == 'S') diag_log_status();
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
    if (midi_write(pkt, k, 0) < (uint32_t)k) { /* busy or full: the line is lost, never block */ }
    return n;
}

bool usb_connected(void) { return tud_mounted(); }

bool usb_start(void)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);           // one serial per chip, so two devices on one host stay apart
    snprintf(usb_serial, 16, "SL-%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    diag_crumb("usb start");
    uac_device_config_t cfg = {
        .skip_tinyusb_init = false,            // the component brings up the PHY, tusb and its task
        .output_cb = spk_cb, .input_cb = mic_cb,
        .spk_itf_num = ITF_AUDIO_SPK, .mic_itf_num = ITF_AUDIO_MIC,
    };
    if (uac_device_init(&cfg) != ESP_OK) { ESP_LOGE(TAG, "USB init failed"); diag_crumb(NULL); return false; }
    diag_crumb(NULL);
    midi_lock = xSemaphoreCreateMutex();
    esp_log_set_vprintf(midi_log_vprintf);     // the Serial/JTAG console is gone now; logs ride on SysEx
    ESP_LOGI(TAG, "USB composite up: MIDI + audio %d Hz, logs over SysEx, serial %s", REC_SAMPLE_RATE, usb_serial);
    return true;
}
#endif
