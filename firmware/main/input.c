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
static pcnt_unit_handle_t pcnt, pcnt2;
static int glitch_ns = -1;
static bool has_knob;
static int  left_kind;

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

static esp_err_t set_glitch(pcnt_unit_handle_t u, int ns)
{
    if (!u) return ESP_OK;
    pcnt_unit_stop(u);
    pcnt_unit_disable(u);
    pcnt_glitch_filter_config_t gf = { .max_glitch_ns = (uint32_t)ns };
    esp_err_t e = pcnt_unit_set_glitch_filter(u, ns > 0 ? &gf : NULL);
    pcnt_unit_enable(u);
    pcnt_unit_start(u);
    return e;
}

void input_tune(void)
{
    int ns = TI(TU_EN_GLITCH);
    if (ns == glitch_ns || !pcnt) return;
    esp_err_t e = set_glitch(pcnt, ns);
    esp_err_t e2 = set_glitch(pcnt2, ns);
    glitch_ns = ns;
    ESP_LOGI(TAG, "encoder glitch filter %d ns%s", ns, e == ESP_OK && e2 == ESP_OK ? "" : " (refused)");
}

// A quadrature encoder on two pins with pull-ups, every edge counted (four counts a detent on an EC11).
static bool encoder_unit(pcnt_unit_handle_t *u, int pin_a, int pin_b)
{
    pcnt_unit_config_t uc = { .high_limit = 30000, .low_limit = -30000 };
    if (pcnt_new_unit(&uc, u) != ESP_OK) { *u = NULL; return false; }
    pcnt_glitch_filter_config_t gf = { .max_glitch_ns = (uint32_t)glitch_ns };
    if (glitch_ns > 0 && pcnt_unit_set_glitch_filter(*u, &gf) != ESP_OK) ESP_LOGW(TAG, "glitch filter %d ns refused", glitch_ns);
    pcnt_channel_handle_t ca, cb;
    pcnt_chan_config_t cca = { .edge_gpio_num = pin_a, .level_gpio_num = pin_b };
    pcnt_chan_config_t ccb = { .edge_gpio_num = pin_b, .level_gpio_num = pin_a };
    bool ok = pcnt_new_channel(*u, &cca, &ca) == ESP_OK && pcnt_new_channel(*u, &ccb, &cb) == ESP_OK;
    if (ok) {
        pcnt_channel_set_edge_action(ca, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
        pcnt_channel_set_level_action(ca, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
        pcnt_channel_set_edge_action(cb, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
        pcnt_channel_set_level_action(cb, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
        ok = pcnt_unit_enable(*u) == ESP_OK && pcnt_unit_clear_count(*u) == ESP_OK && pcnt_unit_start(*u) == ESP_OK;
    }
    if (!ok) { pcnt_del_unit(*u); *u = NULL; }
    return ok;
}

// Rev C has a joystick on GPIO 1 and 2, rev E the left knob. A joystick's wipers rest near half the supply and are
// stiff (a 10 k track); a knob's contacts are open or closed to ground, so with the pull-ups on its pins read the
// supply or ground, and so do pins with nothing on them. Both pins near the middle, steadily: a joystick.
static bool joystick_there(void)
{
    adc_oneshot_unit_handle_t a;
    adc_oneshot_unit_init_cfg_t ac = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&ac, &a) != ESP_OK) return false;
    adc_oneshot_chan_cfg_t cc = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(a, CH_X, &cc);
    adc_oneshot_config_channel(a, CH_Y, &cc);
    gpio_pullup_en(PIN_JOY_X); gpio_pullup_en(PIN_JOY_Y);
    vTaskDelay(pdMS_TO_TICKS(3));
    int lox = 9999, hix = 0, loy = 9999, hiy = 0;
    bool ok = true;
    for (int i = 0; i < 16 && ok; i++) {
        int x = 0, y = 0;
        ok = adc_oneshot_read(a, CH_X, &x) == ESP_OK && adc_oneshot_read(a, CH_Y, &y) == ESP_OK;
        if (x < lox) lox = x;
        if (x > hix) hix = x;
        if (y < loy) loy = y;
        if (y > hiy) hiy = y;
    }
    adc_oneshot_del_unit(a);
    bool joy = ok && lox > 900 && hix < 3300 && loy > 900 && hiy < 3300 && hix - lox < 400 && hiy - loy < 400;
    ESP_LOGI(TAG, "GPIO %d/%d read %d..%d and %d..%d with the pull-ups on: %s", PIN_JOY_X, PIN_JOY_Y, lox, hix, loy, hiy,
             joy ? "a joystick (rev C)" : "no joystick, so the left knob (rev E)");
    return joy;
}

// What sits on GPIO 1 and 2, decided once at power-on: joy.on says a joystick (as before), knob.on says a knob or
// none, and auto looks.
static void left_start(void)
{
    int kn = TI(TU_KN_ON);                                    // 0 auto, 1 off, 2 on
    if (TI(TU_JO_ON)) { left_kind = 2; return; }              // the joystick comes up in input_poll, as it always did
    if (kn == 1) return;
    if (kn == 0 && joystick_there()) { left_kind = 2; return; }   // there, but joy.on is off: the pins are left alone
    gpio_config_t in = { .pin_bit_mask = (1ULL << PIN_KNOB_A) | (1ULL << PIN_KNOB_B), .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&in);
    has_knob = encoder_unit(&pcnt2, PIN_KNOB_A, PIN_KNOB_B);
    left_kind = has_knob ? 1 : 0;
    ESP_LOGI(TAG, "left knob on %d/%d (PCNT)%s", PIN_KNOB_A, PIN_KNOB_B, has_knob ? "" : ": NOT started");
}
bool input_has_knob(void) { return has_knob; }
int  input_left_kind(void) { return left_kind; }

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

    glitch_ns = TI(TU_EN_GLITCH);
    if (!encoder_unit(&pcnt, PIN_ENC_A, PIN_ENC_B)) ESP_LOGE(TAG, "the encoder's counter did not start: no menu knob");
    ESP_LOGI(TAG, "encoder on %d/%d (PCNT), push on %d", PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);
    left_start();
}

void input_poll(input_ev_t *ev)
{
    memset(ev, 0, sizeof *ev);
    int cnt = 0;
    if (pcnt) { pcnt_unit_get_count(pcnt, &cnt); pcnt_unit_clear_count(pcnt); }
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
    ev->hand_mm = k.hand;
    {   // the left knob: its counts, and the rig's turns of it (the rig can turn one that is not there)
        int c2 = 0;
        if (pcnt2) { pcnt_unit_get_count(pcnt2, &c2); pcnt_unit_clear_count(pcnt2); }
        knob_out_t ko;
        gest_knob(c2 + k.knob, &ko);
        ev->has_knob = has_knob || k.knob_on;
        ev->knob = ko.det; ev->knob_counts = ko.counts;
    }

    if (!TI(TU_JO_ON) || has_knob) { ev->has_joy = false; return; }   // the knob has the joystick's pins
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
