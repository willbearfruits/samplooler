// The device: boot, the drivers, and the 10 ms loop around front.c (the core's logic, shared with the
// host simulator). Everything an instrument sees comes through core.h.
//
// Boot: diagnostics first (why did the last boot end?), the speaker amp held off, settings from flash
// (defaults in safe mode, after three crashes in a row), the I2C bus (cleared, then the IMU and the screen),
// the controls (is a joystick fitted?), the LED, the factory-reset gesture (REC held 3 s from power-on),
// the audio engine, the instrument, USB (unless the knob is held: then the Serial/JTAG console stays).
//
// The loop: MIDI in, controls, motion (100 Hz; the 1 kHz FIFO for INST.burst), front_tick(), the screen two
// pages per tick (the OLED shares the bus with the IMU: a whole frame would hold it 26 ms), settings saved
// 3 s after a change once the output is quiet (30 s at most), status lines in the log.
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "pins.h"
#include "core.h"
#include "params.h"
#include "front.h"
#include "audio.h"
#include "imu.h"
#include "oled.h"
#include "ui.h"
#include "input.h"
#include "led.h"
#include "usb.h"
#include "diag.h"
#include "settings.h"

static const char *TAG = "shell";
static bool has_imu, has_joy;

// ---------------------------------------------------------------- the platform, for the instrument and front.c
void *app_alloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
    if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_8BIT);
    if (!p) ESP_LOGE(TAG, "out of memory for %u bytes", (unsigned)n);
    return p;
}
void *app_alloc_fast(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) { ESP_LOGW(TAG, "%u bytes for the sound went to PSRAM: internal RAM is full", (unsigned)n); p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM); }
    if (!p) ESP_LOGE(TAG, "out of memory for %u bytes", (unsigned)n);
    return p;
}
void app_free(void *p) { heap_caps_free(p); }
void app_log(const char *fmt, ...)
{
    char b[160]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ESP_LOGI(INST.name, "%s", b);
}
void plat_midi3(uint8_t st, uint8_t d1, uint8_t d2) { usb_midi_send3(st, d1, d2); }
void plat_led(uint8_t r, uint8_t g, uint8_t b) { led_set(r, g, b); }
void plat_mic_gain(int db) { audio_set_mic_gain_db(db); }
void plat_speaker(bool on) { audio_speaker(on); }
bool plat_save_user(int k, const uint8_t *b, int n) { return settings_user_save(k, b, n); }
int  plat_load_user(int k, uint8_t *b, int cap) { return settings_user_load(k, b, cap); }
void plat_factory_reset(void) { settings_erase(); }
bool plat_sysex(const uint8_t *m, int n) { return usb_midi_send_sysex(m, n); }
const char *plat_version(void) { return esp_app_get_description()->version; }
#if HAS_USB
extern char usb_serial[];                       // usb/usb_descriptors.c
const char *plat_serial(void) { return usb_connected() ? usb_serial : ""; }
#else
const char *plat_serial(void) { return ""; }
#endif
int  plat_about(char l[][32], int max)
{
    audio_stats_t a; audio_get_stats(&a);
    uint8_t mac[6] = { 0 }; esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    int64_t up = esp_timer_get_time() / 1000000;
    int n = 0;
    if (n < max) snprintf(l[n++], 32, "VERSION %s", esp_app_get_description()->version);
    if (n < max) snprintf(l[n++], 32, "CPU %d%% (PEAK %d%%)  DROPS %lu", a.cpu_avg, a.cpu_max, (unsigned long)(a.rx_ovf + a.tx_ovf));
    if (n < max) snprintf(l[n++], 32, "RAM %uK  PSRAM %uK FREE", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    if (n < max) snprintf(l[n++], 32, "UP %d:%02d:%02d  ID %02X%02X%02X%02X", (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60), mac[2], mac[3], mac[4], mac[5]);
    if (n < max) snprintf(l[n++], 32, "TILT %s  STICK %s  USB %s", has_imu ? "YES" : "NO", has_joy ? "YES" : "NO", usb_connected() ? "ON" : "OFF");
    return n;
}

// Before a reboot to the flasher (usb.c): what was waiting to be saved, and no click on the way out.
void app_prepare_reboot(void)
{
    if (front_dirty()) { settings_save(); front_saved(); }
    audio_set_mute(true);
    vTaskDelay(pdMS_TO_TICKS(40));
}

// ---------------------------------------------------------------- I2C
static void i2c_recover(void)
{
    gpio_config_t io = { .pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
                         .mode = GPIO_MODE_INPUT_OUTPUT_OD, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    gpio_set_level(PIN_I2C_SDA, 1); gpio_set_level(PIN_I2C_SCL, 1);
    esp_rom_delay_us(20);
    for (int i = 0; i < 9 && gpio_get_level(PIN_I2C_SDA) == 0; i++) {     // a device left mid-byte: clock it out
        gpio_set_level(PIN_I2C_SCL, 0); esp_rom_delay_us(10);
        gpio_set_level(PIN_I2C_SCL, 1); esp_rom_delay_us(10);
    }
    gpio_set_level(PIN_I2C_SDA, 0); esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SCL, 1); esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SDA, 1); esp_rom_delay_us(10);
    gpio_reset_pin(PIN_I2C_SDA); gpio_reset_pin(PIN_I2C_SCL);
}

static i2c_master_bus_handle_t i2c_start(void)
{
    i2c_recover();
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT, .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    return bus;
}

static void boot_screen(const char *l1, const char *l2)
{
    if (!oled_present()) return;
    oled_clear();
    oled_ftext_c(F_MED, 64, 14, l1, 2);
    if (l2) oled_ftext_c(F_SMALL, 64, 36, l2, 1);
    oled_flush();
}

// Factory reset: REC held from power-on, kept down for 3 s, then let go. Letting go earlier does nothing; a REC
// that never lets go (a stuck button, a short on its pin) cancels it, so it cannot wipe the settings at every start.
static bool rec_stuck_at_boot;                    // REC was down at power-on and has not been let go since
static volatile bool rec_button = true;           // SYSTEM > REC BUTTON: the REC pin counts
void plat_rec_button(bool on) { rec_button = on; }
static void factory_reset_gesture(void)
{
    if (pv[CP(CP_REC_BTN)] < 1.0f) return;       // the button is off: it resets nothing either (RESET ALL in the menu does)
    if (gpio_get_level(PIN_REC) != 0) return;
    int held = 0;
    for (; held < 3000 && gpio_get_level(PIN_REC) == 0; held += 100) {
        char b[24]; snprintf(b, sizeof b, "KEEP HOLDING REC  %d", 3 - held / 1000);
        boot_screen("RESET?", b);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (held < 3000) return;
    int wait = 0;
    for (; wait < 3000 && gpio_get_level(PIN_REC) == 0; wait += 100) {
        boot_screen("RESET?", "LET GO OF REC TO RESET");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (wait >= 3000) {                          // 6 s down: not a hand. A stuck REC: no reset, and REC is ignored
        ESP_LOGW(TAG, "REC (GPIO %d) stays down from power-on: no reset; REC is ignored until it lets go, BOOT records", PIN_REC);
        boot_screen("REC IS STUCK", "BOOT RECORDS FOR NOW");
        vTaskDelay(pdMS_TO_TICKS(1500));
        rec_stuck_at_boot = true;
        return;
    }
    ESP_LOGW(TAG, "factory reset: REC held 3 s at power-on");
    int8_t ax[3]; memcpy(ax, meta.axis, 3); uint8_t cal = meta.calibrated;
    settings_erase();
    params_defaults(false);
    memset(&meta, 0, sizeof meta); memcpy(meta.axis, ax, 3); meta.calibrated = cal;
    settings_save();
    boot_screen("RESET", "EVERYTHING BACK TO NEW");
    vTaskDelay(pdMS_TO_TICKS(1500));
}

// ---------------------------------------------------------------- the loop
void app_main(void)
{
    diag_init();
    gpio_config_t amp = { .pin_bit_mask = 1ULL << PIN_AMP_SD, .mode = GPIO_MODE_OUTPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE };
    gpio_config(&amp);
    gpio_set_level(PIN_AMP_SD, 0);                       // the amp wakes once the I2S clock is good (audio.c)
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "%s: %s", INST.name, INST.tagline);
    gpio_config_t btn = { .pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&btn);

    params_init();
    settings_init();
    bool safe = diag_safe_mode();
    if (safe) ESP_LOGW(TAG, "safe mode: %d crashes in a row, starting from the defaults", diag_crash_streak());
    bool from_flash = settings_load(!safe) > 0;          // the tilt and the sound list are kept even in safe mode
    i2c_master_bus_handle_t bus = i2c_start();
    has_imu = imu_init(bus);
    if (has_imu && INST.burst) imu_burst(true);
    oled_init(bus);
    input_init();
    led_init();
    factory_reset_gesture();
    audio_i2s_init();
    audio_set_process(front_audio);
    front_init(has_imu, safe, from_flash);
    audio_run();
    if (gpio_get_level(PIN_ENC_SW) == 0) ESP_LOGW(TAG, "knob held at boot: USB MIDI/audio off, console kept");
    else { vTaskDelay(pdMS_TO_TICKS(200)); usb_start(); usb_midi_on_sysex(front_sysex); }
    const char *note = diag_note();
    if (note) front_note(note);

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    input_ev_t in = { 0 };
    static imu_vec_t raw_burst[64];
    static vec3_t burst[64];
    int tick = 0, rec_n = 0, dirty_ms = 0;
    bool rec = false;
    TickType_t wake = xTaskGetTickCount();
    int64_t t_last = 0;
    audio_stats_t last_stats = { 0 };

    while (1) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
        tick++;
        esp_task_wdt_reset();
        if (tick == 1000) diag_mark_alive();
        int64_t t_now = esp_timer_get_time();
        float dt = t_last ? (float)(t_now - t_last) * 1e-6f : 0.01f;
        if (dt > 0.05f) dt = 0.05f;
        t_last = t_now;

        usb_midi_poll(front_midi);
        input_poll(&in);
        has_joy = in.has_joy;
        // REC down since power-on counts for nothing until it has been up 0.1 s (a stuck button or a short on its pin);
        // BOOT is REC meanwhile
        static int rec_up_ms;
        if (rec_stuck_at_boot) {
            rec_up_ms = in.rec ? 0 : rec_up_ms + 10;
            if (rec_up_ms >= 100) { rec_stuck_at_boot = false; ESP_LOGI(TAG, "REC let go: it records again"); }
        }
        bool down = gpio_get_level(PIN_BOOT) == 0 || (in.rec && rec_button && !rec_stuck_at_boot);
        rec_n = down ? (rec_n < 2 ? rec_n + 1 : 2) : (rec_n > 0 ? rec_n - 1 : 0);     // 20 ms debounce
        rec = rec_n == 2 ? true : rec_n == 0 ? false : rec;

        imu_vec_t ra = { 0, 0, 1 }, rg = { 0, 0, 0 };
        if (has_imu) imu_read(&ra, &rg);
        int nb = 0;
        if (INST.burst && has_imu) {
            nb = imu_burst_read(raw_burst, 64);
            for (int i = 0; i < nb; i++) burst[i] = (vec3_t){ raw_burst[i].x, raw_burst[i].y, raw_burst[i].z };
        }
        front_in_t fi = {
            .enc = in.enc, .tap = in.tap, .pressed = in.pressed, .held_ms = in.held_ms, .rec = rec,
            .imu_ok = has_imu, .acc_raw = { ra.x, ra.y, ra.z }, .gyro_raw = { rg.x, rg.y, rg.z },
            .burst_raw = burst, .nburst = nb, .joy_ok = in.has_joy, .jx = in.jx, .jy = in.jy,
            .dt = dt, .frames = audio_frames(),
        };
        front_tick(&fi);

        // the screen: a new frame whenever the last one is out, two pages per tick (25 fps)
        if (tick % 100 == 0) oled_present();
        if (!oled_flush_busy()) { front_draw(); oled_flush_start(); }
        oled_flush_pages(2);

        // settings: 3 s after a change, in a quiet moment (30 s at most), never during the splash or tilt setup
        if (front_dirty()) {
            dirty_ms += 10;
            if (dirty_ms >= 3000 && !front_busy() && (front_quiet() || dirty_ms >= 30000)) { settings_save(); front_saved(); dirty_ms = 0; }
        } else dirty_ms = 0;

        if (tick % 1000 == 0) {
            audio_stats_t a; audio_get_stats(&a);
            ESP_LOGI(TAG, "cpu %d%% (max %d%%)  heap %u  psram %u", a.cpu_avg, a.cpu_max,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            if (a.rx_ovf != last_stats.rx_ovf || a.tx_ovf != last_stats.tx_ovf || a.read_err != last_stats.read_err)
                ESP_LOGW(TAG, "audio: %lu rx overruns, %lu tx underruns, %lu read errors since boot (%lu flash stalls)",
                         (unsigned long)a.rx_ovf, (unsigned long)a.tx_ovf, (unsigned long)a.read_err, (unsigned long)a.stalls_flash);
            last_stats = a;
        }
        if (tick % 6000 == 0) diag_log_status();
    }
}
