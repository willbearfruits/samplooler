// Adapted from the ESP32-S3 SuperMini kit's input.c (MIT). The decisions moved to gesture.c (the tuning table).
#include "pins.h"
#if HAS_CONTROLS
#include "input.h"
#include "gesture.h"
#include "tune.h"
#include "rig.h"
#include <string.h>
#include <math.h>
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "input";
static pcnt_unit_handle_t pcnt;
static int glitch_ns = -1;

// The joystick: ADC1 comes up the first time joy.on is set (a board without one leaves GPIO 1 and 2 alone).
static adc_oneshot_unit_handle_t adc;
static bool adc_ready, has_joy;
static float jx0 = 2048, jy0 = 2048;
static int off_centre;
#define CH_X ((adc_channel_t)(PIN_JOY_X - 1))   // ADC1 channel n is GPIO n+1 on the S3
#define CH_Y ((adc_channel_t)(PIN_JOY_Y - 1))

static int adc_read(adc_channel_t ch)
{
    int sum = 0;
    for (int i = 0; i < 4; i++) { int raw = 0; if (adc_oneshot_read(adc, ch, &raw) != ESP_OK) return -1; sum += raw; }
    return sum / 4;
}

void input_joy_centre(void)
{
    if (!adc_ready) return;
    int minx = 9999, maxx = 0, miny = 9999, maxy = 0; long sx = 0, sy = 0;
    for (int i = 0; i < 128; i++) {
        if (i % 8 == 7) vTaskDelay(1);
        int x = adc_read(CH_X), y = adc_read(CH_Y);
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
        sx += x; sy += y;
    }
    jx0 = sx / 128.0f; jy0 = sy / 128.0f;
    has_joy = jx0 > 900 && jx0 < 3300 && jy0 > 900 && jy0 < 3300 && maxx - minx < 700 && maxy - miny < 700;
    off_centre = 0;
    ESP_LOGI(TAG, "joystick %s (centre %d,%d spread %d,%d)", has_joy ? "found" : "not found",
             (int)jx0, (int)jy0, maxx - minx, maxy - miny);
    gest_emit("joy.centre", has_joy ? 1.0f : 0.0f, 0, has_joy ? "found" : "not found");
}

static void joy_start(void)
{
    adc_oneshot_unit_init_cfg_t ac = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&ac, &adc) != ESP_OK) { ESP_LOGW(TAG, "no ADC1 for the joystick"); return; }
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(adc, CH_X, &cc);
    adc_oneshot_config_channel(adc, CH_Y, &cc);
    adc_ready = true;
    input_joy_centre();
}

void input_tune(void)
{
    int ns = TI(TU_EN_GLITCH);
    if (ns == glitch_ns || !pcnt) return;
    pcnt_unit_stop(pcnt);
    pcnt_unit_disable(pcnt);
    pcnt_glitch_filter_config_t gf = { .max_glitch_ns = (uint32_t)ns };
    esp_err_t e = pcnt_unit_set_glitch_filter(pcnt, ns > 0 ? &gf : NULL);
    pcnt_unit_enable(pcnt);
    pcnt_unit_start(pcnt);
    glitch_ns = ns;
    ESP_LOGI(TAG, "encoder glitch filter %d ns%s", ns, e == ESP_OK ? "" : " (refused)");
}

void input_init(void)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_ENC_SW) | (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
#if PIN_REC >= 0
    gpio_config_t rec = { .pin_bit_mask = 1ULL << PIN_REC, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&rec));
    ESP_LOGI(TAG, "record button on %d", PIN_REC);
#endif

    pcnt_unit_config_t uc = { .high_limit = 30000, .low_limit = -30000 };
    ESP_ERROR_CHECK(pcnt_new_unit(&uc, &pcnt));
    glitch_ns = TI(TU_EN_GLITCH);
    pcnt_glitch_filter_config_t gf = { .max_glitch_ns = (uint32_t)glitch_ns };
    if (glitch_ns > 0 && pcnt_unit_set_glitch_filter(pcnt, &gf) != ESP_OK) ESP_LOGW(TAG, "glitch filter %d ns refused", glitch_ns);
    pcnt_channel_handle_t ca, cb;
    pcnt_chan_config_t cca = { .edge_gpio_num = PIN_ENC_A, .level_gpio_num = PIN_ENC_B };
    pcnt_chan_config_t ccb = { .edge_gpio_num = PIN_ENC_B, .level_gpio_num = PIN_ENC_A };
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt, &cca, &ca));
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt, &ccb, &cb));
    pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt));
    ESP_LOGI(TAG, "encoder on %d/%d (PCNT), push on %d", PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);
}

void input_poll(input_ev_t *ev)
{
    memset(ev, 0, sizeof *ev);
    int cnt = 0;
    pcnt_unit_get_count(pcnt, &cnt);
    pcnt_unit_clear_count(pcnt);
    rig_keys_t k;
    rig_keys(&k, TI(TU_EN_STEPS), TI(TU_EN_DIR) ? 1 : -1);    // the rig's turns and presses, as if from the pins

    bool pressed = gpio_get_level(PIN_ENC_SW) == 0;
    if (k.sw >= 0) pressed = k.sw;
    enc_out_t e;
    gest_enc(cnt + k.counts, pressed, esp_timer_get_time(), &e);
    ev->enc = e.enc; ev->tap = e.tap; ev->hold = e.hold; ev->longp = e.longp;
    ev->pressed = e.pressed; ev->held_ms = e.held_ms;
#if PIN_REC >= 0
    ev->rec = gpio_get_level(PIN_REC) == 0;
#endif
    ev->rec_pc = k.rec > 0;

    if (!TI(TU_JO_ON)) { ev->has_joy = false; return; }
    if (!adc_ready) joy_start();
    bool over = k.jx >= 0 && k.jy >= 0;
    if (!adc_ready || (!has_joy && !over)) return;
    int rx = over ? k.jx : adc_read(CH_X), ry = over ? k.jy : adc_read(CH_Y);
    joy_out_t j;
    gest_joy(rx, ry, over ? 2048.0f : jx0, over ? 2048.0f : jy0, &j);
    if (!over) {
        if (fabsf(j.jx) > 0.5f || fabsf(j.jy) > 0.5f) {
            if (++off_centre > 800) { has_joy = false; ESP_LOGW(TAG, "joystick pinned off centre for 8 s, ignoring it"); gest_emit("joy.pinned", 0, 0, ""); }
        } else off_centre = 0;
    }
    ev->has_joy = true;
    ev->jx = j.jx; ev->jy = j.jy; ev->raw_x = rx; ev->raw_y = ry;
    ev->flick_l = j.flick_l; ev->flick_r = j.flick_r; ev->flick_u = j.flick_u; ev->flick_d = j.flick_d;
}
#endif
