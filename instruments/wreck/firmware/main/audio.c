// The Samplooler's audio engine (gesture-recorder/firmware/main/audio.c), S3 float path only, with the
// recorder replaced by a process function: front_audio() (the instrument, the looper, the master limiter).
// Here: the mic in (slot detection, gain, an 80 Hz high pass), the 20 ms mute ramp, a NaN guard, the
// conversion to the DAC and the USB sound card, the speaker amp's wake-up, and the health counters.
#include "audio.h"
#include "pins.h"
#include "usb.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_cpu.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

static const char *TAG = "audio";

#define FRAMES   AUDIO_BLOCK
#define SAMPLES  (FRAMES * 2)
#define MIC_GAIN_DB_DEFAULT 12      // x4. The INMP441 hits full scale around 120 dB SPL.

static i2s_chan_handle_t tx, rx;
static volatile audio_process_t process;
static volatile uint32_t frame_now;
// When frame 0 began on the microsecond clock: each block's start time minus its frames, the lowest seen lately
// (a block is never handled early, only sometimes late). The link maps frames between boards with it.
static uint64_t frames64;
static int64_t  epoch_us = INT64_MAX;
static volatile uint32_t epoch_seq;
static volatile int64_t  epoch_pub;
static volatile int cpu_pct;
static volatile int mic_slot;
static int          slot_vote;
static volatile int   mic_db = MIC_GAIN_DB_DEFAULT;
static volatile float in_peak, out_peak;
static volatile int   in_clips;
// health
static volatile uint32_t blocks, rx_ovf, tx_ovf, read_err, stalls_flash;
static volatile int64_t  flash_t = -1000000000;       // last audio_flash_hint()
static volatile int      cpu_max, win_max;
static int               win_n;
// output mute: a per-block gain ramp, 20 ms end to end
static volatile int   mute_req;
static float          mute_g = 1.0f;
#define MUTE_STEP 0.1f
#define AMP_GOOD_BLOCKS 25                              // 50 ms of clean reads before the speaker amp wakes
static int good_blocks;
static int8_t scope[AUDIO_SCOPE_N];
static int    scope_w, scope_dec;
static inline void scope_push(float x)
{
    if (++scope_dec < 8) return;
    scope_dec = 0;
    int v = (int)(x * 127.0f); if (v > 127) v = 127; else if (v < -127) v = -127;
    scope[scope_w] = (int8_t)v; scope_w = (scope_w + 1) & (AUDIO_SCOPE_N - 1);
}
void audio_scope(int8_t *dst) { int w = scope_w; for (int i = 0; i < AUDIO_SCOPE_N; i++) dst[i] = scope[(w + i) & (AUDIO_SCOPE_N - 1)]; }

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
static biquad_t hp;
static void biquad_highpass(biquad_t *q, float fc, float qf)
{
    float w = 2.0f * (float)M_PI * fc / (float)AUDIO_SR;
    float c = cosf(w), a = sinf(w) / (2.0f * qf), n = 1.0f / (1.0f + a);
    q->b0 = (1.0f + c) * 0.5f * n; q->b1 = -(1.0f + c) * n; q->b2 = q->b0;
    q->a1 = -2.0f * c * n; q->a2 = (1.0f - a) * n; q->z1 = q->z2 = 0.0f;
}
static inline float biquad_run(biquad_t *q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

// A DMA queue overflow means the task was late for a block. Within 500 ms of a flash write it is the
// write (the cache goes off and both cores stall); otherwise it is a real overrun.
static bool IRAM_ATTR late(void)
{
    int64_t now = esp_timer_get_time();
    if (now - flash_t < 500000) { stalls_flash++; return false; }
    return true;
}
static bool IRAM_ATTR rx_ovf_cb(i2s_chan_handle_t h, i2s_event_data_t *e, void *ctx) { (void)h; (void)e; (void)ctx; if (late()) rx_ovf++; return false; }
static bool IRAM_ATTR tx_ovf_cb(i2s_chan_handle_t h, i2s_event_data_t *e, void *ctx) { (void)h; (void)e; (void)ctx; if (late()) tx_ovf++; return false; }

void audio_i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_frame_num = FRAMES;
    chan_cfg.dma_desc_num = 3;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, &rx));
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SR),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS,
                      .dout = PIN_I2S_DOUT, .din = PIN_I2S_DIN },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &std_cfg));
    i2s_event_callbacks_t rx_cb = { .on_recv_q_ovf = rx_ovf_cb }, tx_cb = { .on_send_q_ovf = tx_ovf_cb };
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(rx, &rx_cb, NULL));
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx, &tx_cb, NULL));
    // The channels start in audio_run(), right before the task that drains them.
}

static void IRAM_ATTR audio_task(void *arg)
{
    (void)arg;
    int32_t *raw = malloc(SAMPLES * sizeof(int32_t));
    float    *in  = malloc(FRAMES * sizeof(float));
    float    *out = malloc(FRAMES * sizeof(float));
    assert(raw && in && out);
    biquad_highpass(&hp, 80.0f, 0.707f);
    const uint32_t budget = (uint32_t)FRAMES * (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000u / AUDIO_SR);

    esp_task_wdt_add(NULL);          // a stuck audio task (5 s without a block) resets the device

    while (1) {
        size_t got = 0;
        esp_task_wdt_reset();
        if (i2s_channel_read(rx, raw, SAMPLES * sizeof(int32_t), &got, pdMS_TO_TICKS(100)) != ESP_OK || got == 0) {
            read_err++; good_blocks = 0;
            memset(raw, 0, SAMPLES * sizeof(int32_t)); got = SAMPLES * sizeof(int32_t);
        } else if (good_blocks < AMP_GOOD_BLOCKS && ++good_blocks == AMP_GOOD_BLOCKS) audio_amp_enable(true);
        int frames = (int)(got / (2 * sizeof(int32_t)));
        {
            int64_t e = esp_timer_get_time() - (int64_t)(frames64 * 125u / 4u);
            if (e < epoch_us) epoch_us = e; else epoch_us += (e - epoch_us) / 1024;   // follows a slow drift upwards
            __atomic_add_fetch(&epoch_seq, 1, __ATOMIC_ACQ_REL);
            epoch_pub = epoch_us;
            __atomic_add_fetch(&epoch_seq, 1, __ATOMIC_ACQ_REL);
        }
        uint32_t c0 = esp_cpu_get_cycle_count();
        float mg = mute_g, mt = mute_req ? 0.0f : 1.0f;
        if (mg < mt) { mg += MUTE_STEP; if (mg > mt) mg = mt; } else if (mg > mt) { mg -= MUTE_STEP; if (mg < mt) mg = mt; }
        mute_g = mg;

        // INMP441: top 24 bits are data. It drives one slot and tri-states the other; the live slot is the mic.
        int ll = 0, lr = 0;
        for (int f = 0; f < frames; f++) {
            int wl = raw[2 * f] >> 8, wr = raw[2 * f + 1] >> 8;
            ll += wl != 0 && wl != -1;
            lr += wr != 0 && wr != -1;
        }
        int want = (ll < frames / 4 && lr > frames / 2) ? 1 : (lr < frames / 4 && ll > frames / 2) ? 0 : mic_slot;
        if (want != mic_slot) { if (++slot_vote > 50) { mic_slot = want; slot_vote = 0; } }
        else slot_vote = 0;

        const float k = exp2f((float)mic_db / 6.0206f) / 8388608.0f;
        float pk = 0; int clips = 0;
        for (int f = 0; f < frames; f++) {
            float x = biquad_run(&hp, (float)(raw[2 * f + mic_slot] >> 8) * k);
            float a = fabsf(x); if (a > pk) pk = a;
            if (a >= 1.0f) clips++;
            in[f] = x;
            scope_push(x);
        }
        if (pk > in_peak) in_peak = pk;
        in_clips += clips;

        audio_process_t p = process;
        if (p) p(in, out, frames); else memset(out, 0, frames * sizeof(float));
        frame_now += (uint32_t)frames;
        frames64 += (uint64_t)frames;
        blocks++;

        float opk = 0;
        static int16_t usb_out[FRAMES];
        for (int f = 0; f < frames; f++) {
            float o = out[f];
            uint32_t bits; memcpy(&bits, &o, 4);
            if ((bits & 0x7f800000u) == 0x7f800000u) o = 0.0f;   // NaN or inf (fast-math hides x != x)
            float a = fabsf(o); if (a > opk) opk = a;
            float c = (o > 1.0f ? 1.0f : o < -1.0f ? -1.0f : o) * mg;   // the master limiter already keeps it inside
            int32_t v = (int32_t)(c * 2147483000.0f);
            raw[2 * f] = v; raw[2 * f + 1] = v;
            usb_out[f] = (int16_t)(c * 32767.0f);
        }
        if (opk > out_peak) out_peak = opk;
        usb_audio_out_push(usb_out, frames);

        uint32_t used = esp_cpu_get_cycle_count() - c0;
        int pct = (int)((uint64_t)used * 100 / budget);
        cpu_pct += (pct - cpu_pct) / 8;
        if (pct > win_max) win_max = pct;
        if (++win_n >= AUDIO_SR / FRAMES) { cpu_max = win_max; win_max = 0; win_n = 0; }   // once a second

        size_t put = 0;
        if (i2s_channel_write(tx, raw, got, &put, pdMS_TO_TICKS(100)) != ESP_OK) read_err++;
    }
}

void audio_run(void)
{
    ESP_LOGI(TAG, "I2S full duplex, %d Hz, %d-frame blocks, float, core 1", AUDIO_SR, FRAMES);
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_ERROR_CHECK(i2s_channel_enable(rx));
    xTaskCreatePinnedToCore(audio_task, "audio", 8192, NULL, 6, NULL, 1);
}

void audio_set_process(audio_process_t fn) { process = fn; }
uint32_t audio_blocks(void) { return blocks; }
uint32_t audio_frames(void) { return frame_now; }
int64_t audio_epoch_us(void)
{
    uint32_t q; int64_t e;
    do { q = __atomic_load_n(&epoch_seq, __ATOMIC_ACQUIRE); e = epoch_pub; } while ((q & 1u) || q != __atomic_load_n(&epoch_seq, __ATOMIC_ACQUIRE));
    return e;
}
int  audio_cpu_percent(void) { return cpu_pct; }
int  audio_mic_slot(void)    { return mic_slot; }
void audio_set_mic_gain_db(int db) { mic_db = db < 0 ? 0 : db > 36 ? 36 : db; }
int  audio_mic_gain_db(void)       { return mic_db; }
float audio_in_peak(void)  { float p = in_peak;  in_peak = 0;  return p; }
float audio_out_peak(void) { float p = out_peak; out_peak = 0; return p; }
int   audio_in_clips(void) { int c = in_clips;   in_clips = 0; return c; }
void  audio_get_stats(audio_stats_t *s)
{
    s->blocks = blocks; s->rx_ovf = rx_ovf; s->tx_ovf = tx_ovf; s->read_err = read_err; s->stalls_flash = stalls_flash;
    s->cpu_avg = cpu_pct; s->cpu_max = cpu_max;
}
void  audio_flash_hint(void) { flash_t = esp_timer_get_time(); }
void  audio_set_mute(bool on) { mute_req = on; }
bool  audio_silent(void)      { return mute_req && mute_g <= 0.0f; }
void  audio_amp_enable(bool on)
{
#if PIN_AMP_SD >= 0
    gpio_set_level(PIN_AMP_SD, on);
#else
    (void)on;
#endif
}
