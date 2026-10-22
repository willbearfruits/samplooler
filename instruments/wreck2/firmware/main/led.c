#include "led.h"
#include "pins.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

static const char *TAG = "led";
static rmt_channel_handle_t chan;
static rmt_encoder_handle_t enc;

void led_init(void)
{
    rmt_tx_channel_config_t tc = { .gpio_num = PIN_LED, .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10000000,
                                   .mem_block_symbols = 48, .trans_queue_depth = 2 };
    if (rmt_new_tx_channel(&tc, &chan) != ESP_OK) { ESP_LOGW(TAG, "no RMT channel: LED off"); chan = NULL; return; }
    // WS2812 bits at 10 MHz: 0 = 0.4 us high, 0.85 low; 1 = 0.8 high, 0.45 low; MSB first, GRB
    rmt_bytes_encoder_config_t bc = {
        .bit0 = { .level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8 },
        .bit1 = { .level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4 },
        .flags.msb_first = 1,
    };
    if (rmt_new_bytes_encoder(&bc, &enc) != ESP_OK || rmt_enable(chan) != ESP_OK) { ESP_LOGW(TAG, "RMT setup failed: LED off"); chan = NULL; return; }
    led_set(0, 0, 0);
}

void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!chan) return;
    static uint8_t grb[3];
    grb[0] = g; grb[1] = r; grb[2] = b;
    rmt_transmit_config_t t = { .loop_count = 0 };
    if (rmt_tx_wait_all_done(chan, 2) != ESP_OK) return;     // the last one is still going out: skip this one
    rmt_transmit(chan, enc, grb, sizeof grb, &t);
}
