// Adapted from the ESP32-S3 SuperMini kit's input.c (MIT), S3 only.
#include "input.h"
#include "pins.h"
#include <string.h>
#include <math.h>
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "input";
static pcnt_unit_handle_t pcnt;
static int  enc_acc;
static bool was_pressed;
static int64_t t_press;
static bool has_joy;
static adc_oneshot_unit_handle_t adc;
static float jx0, jy0, jx_f, jy_f;
static int off_centre;

#define CH_X ((adc_channel_t)(PIN_JOY_X - 1))   // ADC1 channel n is GPIO n+1 on the S3
#define CH_Y ((adc_channel_t)(PIN_JOY_Y - 1))

static int adc_read(adc_channel_t ch)
{
    int sum = 0;
    for (int i = 0; i < 4; i++) { int raw = 0; if (adc_oneshot_read(adc, ch, &raw) != ESP_OK) return -1; sum += raw; }
    return sum / 4;
}

// Is a joystick there? A pot's wiper is a few kilohms, so the pad's weak (about 45 k) pull-up and
// pull-down hardly move it; a pin with nothing on it follows them to the rails. The Mini has no
// joystick and leaves these pins open.
static bool wiper_on(adc_channel_t ch, int gpio)
{
    gpio_pullup_en(gpio); gpio_pulldown_dis(gpio); vTaskDelay(pdMS_TO_TICKS(3));
    int hi = adc_read(ch);
    gpio_pullup_dis(gpio); gpio_pulldown_en(gpio); vTaskDelay(pdMS_TO_TICKS(3));
    int lo = adc_read(ch);
    gpio_pulldown_dis(gpio); vTaskDelay(pdMS_TO_TICKS(3));
    ESP_LOGI(TAG, "joystick pin %d: %d with the pull-up, %d with the pull-down", gpio, hi, lo);
    return hi >= 0 && lo >= 0 && hi - lo < 1500;
}

void input_init(void)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_ENC_SW) | (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B) | (1ULL << PIN_REC),
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

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
    ESP_LOGI(TAG, "encoder on %d/%d, push on %d, record button on %d", PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW, PIN_REC);

    adc_oneshot_unit_init_cfg_t ac = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ac, &adc));
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, CH_X, &cc));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, CH_Y, &cc));
    bool wired = wiper_on(CH_X, PIN_JOY_X) && wiper_on(CH_Y, PIN_JOY_Y);
    int minx = 9999, maxx = 0, miny = 9999, maxy = 0; long sx = 0, sy = 0;
    for (int i = 0; i < 64; i++) {
        if (i % 8 == 7) vTaskDelay(1);
        int x = adc_read(CH_X), y = adc_read(CH_Y);
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
        sx += x; sy += y;
    }
    jx0 = (float)sx / 64.0f; jy0 = (float)sy / 64.0f;
    has_joy = wired && jx0 > 900 && jx0 < 3300 && jy0 > 900 && jy0 < 3300 && maxx - minx < 500 && maxy - miny < 500;
    ESP_LOGI(TAG, "joystick %s (centre %d,%d spread %d,%d)", has_joy ? "found" : "not found", (int)jx0, (int)jy0, maxx - minx, maxy - miny);
}

static float axis(int raw, float centre)
{
    float v = (float)raw > centre ? ((float)raw - centre) / (4095.0f - centre) : ((float)raw - centre) / centre;
    float a = fabsf(v);
    if (a < 0.10f) return 0;
    v = (a - 0.10f) / 0.90f * (v < 0 ? -1.0f : 1.0f);
    return v < -1 ? -1 : (v > 1 ? 1 : v);
}

void input_poll(input_ev_t *ev)
{
    memset(ev, 0, sizeof *ev);
    int cnt = 0;
    pcnt_unit_get_count(pcnt, &cnt);
    pcnt_unit_clear_count(pcnt);
    enc_acc += cnt * ENC_DIR;
    ev->enc = enc_acc / ENC_STEPS;
    enc_acc -= ev->enc * ENC_STEPS;

    bool pressed = gpio_get_level(PIN_ENC_SW) == 0;
    int64_t now = esp_timer_get_time();
    if (pressed && !was_pressed) t_press = now;
    if (!pressed && was_pressed) {
        int ms = (int)((now - t_press) / 1000);
        if (ms >= 40 && ms < 500) ev->tap = true;
    }
    ev->pressed = pressed; ev->held_ms = pressed ? (int)((now - t_press) / 1000) : 0;
    was_pressed = pressed;
    ev->rec = gpio_get_level(PIN_REC) == 0;

    ev->has_joy = has_joy;
    if (has_joy) {
        float ax = axis(adc_read(CH_X), jx0), ay = axis(adc_read(CH_Y), jy0);
        float x = ay, y = -ax;                      // the module sits a quarter turn clockwise on the kit and the Stack
        if (fabsf(x) > 0.5f || fabsf(y) > 0.5f) { if (++off_centre > 800) { has_joy = false; ESP_LOGW(TAG, "joystick pinned off centre for 8 s: ignoring it until the next boot"); } }
        else off_centre = 0;
        jx_f += 0.35f * (x - jx_f); jy_f += 0.35f * (y - jy_f);
        ev->jx = jx_f; ev->jy = jy_f;
    }
}
