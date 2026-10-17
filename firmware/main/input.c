// Adapted from the ESP32-S3 SuperMini kit's input.c (MIT).
#include "pins.h"
#if HAS_CONTROLS
#include "input.h"
#include <string.h>
#include <math.h>
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#if SOC_PCNT_SUPPORTED
#include "driver/pulse_cnt.h"
#else
#include "freertos/portmacro.h"
#endif
#if HAS_JOY
#include "esp_adc/adc_oneshot.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "input";
#if SOC_PCNT_SUPPORTED
static pcnt_unit_handle_t pcnt;
#else
// No pulse counter on the C3: decode the quadrature in a GPIO ISR.
// state = (A<<1)|B; a valid step changes one bit, a bounce that changes both counts 0.
static volatile int32_t sw_count;
static uint8_t sw_prev;
static void IRAM_ATTR enc_isr(void *arg)
{
    (void)arg;
    static const int8_t step[16] = { 0, -1, 1, 0,  1, 0, 0, -1,  -1, 0, 0, 1,  0, 1, -1, 0 };
    uint8_t now = (gpio_get_level(PIN_ENC_A) << 1) | gpio_get_level(PIN_ENC_B);
    sw_count += step[(sw_prev << 2) | now];
    sw_prev = now;
}
static portMUX_TYPE enc_mux = portMUX_INITIALIZER_UNLOCKED;
static int enc_take(void)
{
    portENTER_CRITICAL(&enc_mux);
    int c = sw_count; sw_count = 0;
    portEXIT_CRITICAL(&enc_mux);
    return c;
}
#endif
static int enc_acc;
static bool was_pressed, long_fired;
static int64_t t_press;
#if HAS_JOY
static bool has_joy;
static adc_oneshot_unit_handle_t adc;
static float jx0, jy0, jx_f, jy_f;
static bool flick_armed[4] = { true, true, true, true };

#define CH_X ((adc_channel_t)(PIN_JOY_X - 1))   // ADC1 channel n is GPIO n+1 on the S3
#define CH_Y ((adc_channel_t)(PIN_JOY_Y - 1))

static int adc_read(adc_channel_t ch)
{
    int sum = 0;
    for (int i = 0; i < 4; i++) { int raw = 0; if (adc_oneshot_read(adc, ch, &raw) != ESP_OK) return -1; sum += raw; }
    return sum / 4;
}
#endif

void input_init(void)
{
    gpio_config_t in = {
#if HAS_JOY
        .pin_bit_mask = (1ULL << PIN_ENC_SW) | (1ULL << PIN_JOY_SW) | (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B),
#else
        .pin_bit_mask = (1ULL << PIN_ENC_SW) | (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B),
#endif
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));
#if PIN_REC >= 0
    gpio_config_t rec = { .pin_bit_mask = 1ULL << PIN_REC, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    ESP_ERROR_CHECK(gpio_config(&rec));
    ESP_LOGI(TAG, "record button on %d", PIN_REC);
#endif

#if SOC_PCNT_SUPPORTED
    pcnt_unit_config_t uc = { .high_limit = 30000, .low_limit = -30000 };
    ESP_ERROR_CHECK(pcnt_new_unit(&uc, &pcnt));
    pcnt_glitch_filter_config_t gf = { .max_glitch_ns = 1000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt, &gf));
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
#else
    sw_prev = (gpio_get_level(PIN_ENC_A) << 1) | gpio_get_level(PIN_ENC_B);
    ESP_ERROR_CHECK(gpio_set_intr_type(PIN_ENC_A, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_set_intr_type(PIN_ENC_B, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ENC_A, enc_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ENC_B, enc_isr, NULL));
    ESP_LOGI(TAG, "encoder on %d/%d (GPIO ISR), push on %d", PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);
#endif

#if HAS_JOY
    adc_oneshot_unit_init_cfg_t ac = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ac, &adc));
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, CH_X, &cc));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, CH_Y, &cc));

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
    ESP_LOGI(TAG, "joystick %s (centre %d,%d spread %d,%d)", has_joy ? "found" : "not found",
             (int)jx0, (int)jy0, maxx - minx, maxy - miny);
#endif
}

#if HAS_JOY
static float axis(int raw, float centre)
{
    float v = raw > centre ? (raw - centre) / (4095.0f - centre) : (raw - centre) / centre;
    float a = fabsf(v);
    if (a < 0.12f) return 0;
    v = (a - 0.12f) / 0.88f * (v < 0 ? -1 : 1);
    return v < -1 ? -1 : (v > 1 ? 1 : v);
}

static void flick(float v, bool *neg, bool *pos, int idx)
{
    if (v > 0.7f && flick_armed[idx]) { *pos = true; flick_armed[idx] = false; }
    else if (v < -0.7f && flick_armed[idx]) { *neg = true; flick_armed[idx] = false; }
    else if (fabsf(v) < 0.3f) flick_armed[idx] = true;
}
#endif

void input_poll(input_ev_t *ev)
{
    memset(ev, 0, sizeof *ev);
    int cnt = 0;
#if SOC_PCNT_SUPPORTED
    pcnt_unit_get_count(pcnt, &cnt);
    pcnt_unit_clear_count(pcnt);
#else
    cnt = enc_take();
#endif
    enc_acc += cnt * ENC_DIR;
    ev->enc = enc_acc / ENC_STEPS;
    enc_acc -= ev->enc * ENC_STEPS;

    bool pressed = gpio_get_level(PIN_ENC_SW) == 0;
    int64_t now = esp_timer_get_time();
    if (pressed && !was_pressed) { t_press = now; long_fired = false; }
    int held = pressed ? (int)((now - t_press) / 1000) : 0;
    if (pressed && !long_fired && held >= 3000) { ev->longp = true; long_fired = true; }
    if (!pressed && was_pressed && !long_fired) {
        int ms = (int)((now - t_press) / 1000);
        if (ms >= 40 && ms < 500) ev->tap = true;
        else if (ms >= 500) ev->hold = true;
    }
    ev->pressed = pressed; ev->held_ms = held;
    was_pressed = pressed;
#if PIN_REC >= 0
    ev->rec = gpio_get_level(PIN_REC) == 0;
#endif

#if HAS_JOY
    static bool joy_was, joy_long_fired;
    static int64_t t_joy;
    bool joy = gpio_get_level(PIN_JOY_SW) == 0;
    if (joy && !joy_was) { t_joy = now; joy_long_fired = false; }
    if (joy && !joy_long_fired && now - t_joy >= 700000) { ev->joy_long = true; joy_long_fired = true; }
    if (!joy && joy_was && !joy_long_fired && now - t_joy >= 30000) ev->joy_click = true;
    joy_was = joy;

    ev->has_joy = has_joy;
    if (has_joy) {
        float ax = axis(adc_read(CH_X), jx0), ay = axis(adc_read(CH_Y), jy0);
        float x = ay, y = -ax;                      // the module sits a quarter turn clockwise in the kit case
        static int off_centre;
        if (fabsf(x) > 0.5f || fabsf(y) > 0.5f) { if (++off_centre > 800) { has_joy = false; ESP_LOGW(TAG, "joystick pinned off centre for 8 s, ignoring it"); } }
        else off_centre = 0;
        jx_f += 0.3f * (x - jx_f); jy_f += 0.3f * (y - jy_f);
        ev->jx = jx_f; ev->jy = jy_f;
        flick(jx_f, &ev->flick_l, &ev->flick_r, 0);
        flick(jy_f, &ev->flick_d, &ev->flick_u, 1);
    }
#else
    ev->has_joy = false;
#endif
}
#endif
