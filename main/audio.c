#include "audio.h"
#include "pins.h"
#include "recorder.h"
#include "usb.h"
#include "fx.h"
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_attr.h"
#include "sdkconfig.h"

static const char *TAG = "audio";

#define FRAMES   64
#define SAMPLES  (FRAMES * 2)
#define MIC_GAIN_DB_DEFAULT 12      // x4. The INMP441 hits full scale around 120 dB SPL; x8 clipped a close voice.

static i2s_chan_handle_t tx, rx;
static volatile int cpu_pct;
static volatile int mic_slot;
static int          slot_vote;
static volatile int   mic_db = MIC_GAIN_DB_DEFAULT;
static volatile float in_peak, out_peak;
static volatile int   in_clips;
static volatile int   in_src = AUDIO_IN_MIC;
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

#if REC_FIXED_POINT
// One-pole DC blocker, Q15 coefficient. Cheaper than a biquad and enough for a mic.
static int32_t dc_x1, dc_y1;
static inline int16_t dc_block(int32_t x)
{
    int32_t y = x - dc_x1 + ((dc_y1 * 32440) >> 15);   // pole at 0.99: about 25 Hz corner at 16 kHz
    dc_x1 = x; dc_y1 = y;
    if (y > 32767) y = 32767; else if (y < -32768) y = -32768;
    return (int16_t)y;
}
#else
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
static biquad_t hp;
static void biquad_highpass(biquad_t *q, float fc, float qf)
{
    float w = 2.0f * (float)M_PI * fc / (float)REC_SAMPLE_RATE;
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
static inline float softclip(float x)
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - x * x * x * (4.0f / 27.0f);
}
#endif

void audio_i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_frame_num = FRAMES;
    chan_cfg.dma_desc_num = 3;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, &rx));
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(REC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS,
                      .dout = PIN_I2S_DOUT, .din = PIN_I2S_DIN },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_ERROR_CHECK(i2s_channel_enable(rx));
}

static void IRAM_ATTR audio_task(void *arg)
{
    (void)arg;
    int32_t      *raw = malloc(SAMPLES * sizeof(int32_t));
    rec_sample_t *in  = malloc(FRAMES * sizeof(rec_sample_t));
    rec_sample_t *out = malloc(FRAMES * sizeof(rec_sample_t));
    assert(raw && in && out);
#if !REC_FIXED_POINT
    biquad_highpass(&hp, 80.0f, 0.707f);
#endif
    const uint32_t budget = (uint32_t)FRAMES * (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000u / REC_SAMPLE_RATE);

    while (1) {
        size_t got = 0;
        ESP_ERROR_CHECK(i2s_channel_read(rx, raw, SAMPLES * sizeof(int32_t), &got, portMAX_DELAY));
        int frames = (int)(got / (2 * sizeof(int32_t)));
        uint32_t c0 = esp_cpu_get_cycle_count();

        // INMP441: top 24 bits are data, low 8 junk. It drives one slot and
        // tri-states the other (reads all 0 or all 1); the live slot is the mic.
        // No logging here: a console write spins the calling task.
        int ll = 0, lr = 0;
        for (int f = 0; f < frames; f++) {
            int wl = raw[2 * f] >> 8, wr = raw[2 * f + 1] >> 8;
            ll += wl != 0 && wl != -1;
            lr += wr != 0 && wr != -1;
        }
        int want = (ll < frames / 4 && lr > frames / 2) ? 1 : (lr < frames / 4 && ll > frames / 2) ? 0 : mic_slot;
        if (want != mic_slot) { if (++slot_vote > 50) { mic_slot = want; slot_vote = 0; } }
        else slot_vote = 0;

#if REC_FIXED_POINT
        int shift = 8 - (mic_db + 3) / 6;                                         // 6 dB per bit
        if (shift < 2) shift = 2;
        if (shift > 8) shift = 8;
        int32_t pk = 0, clips = 0;
        for (int f = 0; f < frames; f++) {
            int32_t v = (raw[2 * f + mic_slot] >> 8) >> shift;
            if (v > 32767) { v = 32767; clips++; } else if (v < -32768) { v = -32768; clips++; }
            int32_t a = v < 0 ? -v : v; if (a > pk) pk = a;
            in[f] = dc_block(v);
            scope_push(in[f] * (1.0f / 32768.0f));
        }
        if (pk * (1.0f / 32768.0f) > in_peak) in_peak = pk * (1.0f / 32768.0f);
        in_clips += clips;
        recorder_process(in, out, frames);
        int32_t opk = 0;
        for (int f = 0; f < frames; f++) {
            int32_t o = out[f]; int32_t a = o < 0 ? -o : o; if (a > opk) opk = a;
            int32_t v = o << 15;                                                  // -6 dB of headroom at the DAC
            raw[2 * f] = v; raw[2 * f + 1] = v;
        }
        if (opk * (1.0f / 32768.0f) > out_peak) out_peak = opk * (1.0f / 32768.0f);
#else
        // dB to linear without powf: 10^(db/20) = 2^(db/6.0206)
        const float k = exp2f((float)mic_db / 6.0206f) / 8388608.0f;
        float pk = 0; int clips = 0;
        if (in_src == AUDIO_IN_USB) {
            static int16_t usb_in[FRAMES];
            usb_audio_in_pull(usb_in, frames);
            for (int f = 0; f < frames; f++) {
                float x = usb_in[f] * (1.0f / 32768.0f);
                float a = fabsf(x); if (a > pk) pk = a;
                in[f] = x;
            }
        } else
        for (int f = 0; f < frames; f++) {
            float x = biquad_run(&hp, (float)(raw[2 * f + mic_slot] >> 8) * k);
            float a = fabsf(x); if (a > pk) pk = a;
            if (a >= 1.0f) clips++;
            in[f] = x;
            scope_push(x);
        }
        if (pk > in_peak) in_peak = pk;
        in_clips += clips;
        recorder_process(in, out, frames);
        fx_process(out, frames);
        float opk = 0;
        static int16_t usb_out[FRAMES];
        for (int f = 0; f < frames; f++) {
            float o = out[f]; float a = fabsf(o); if (a > opk) opk = a;
            float c = softclip(o) * 0.9f;
            int32_t v = (int32_t)(c * 2147483000.0f);
            raw[2 * f] = v; raw[2 * f + 1] = v;
            usb_out[f] = (int16_t)(c * 32767.0f);
        }
        if (opk > out_peak) out_peak = opk;
        usb_audio_out_push(usb_out, frames);
#endif
        uint32_t used = esp_cpu_get_cycle_count() - c0;
        int pct = (int)((uint64_t)used * 100 / budget);
        cpu_pct += (pct - cpu_pct) / 8;

        size_t put = 0;
        ESP_ERROR_CHECK(i2s_channel_write(tx, raw, got, &put, portMAX_DELAY));
    }
}

void audio_run(void)
{
#if CONFIG_IDF_TARGET_ESP32C3
    xTaskCreate(audio_task, "audio", 4096, NULL, configMAX_PRIORITIES - 2, NULL);
    ESP_LOGI(TAG, "I2S full duplex, %d Hz, %d-frame blocks, Q15, single core at priority %d",
             REC_SAMPLE_RATE, FRAMES, configMAX_PRIORITIES - 2);
#else
    xTaskCreatePinnedToCore(audio_task, "audio", 8192, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "I2S full duplex, %d Hz, %d-frame blocks, float, core 1", REC_SAMPLE_RATE, FRAMES);
#endif
}

int audio_cpu_percent(void) { return cpu_pct; }
int audio_mic_slot(void)    { return mic_slot; }
void  audio_set_mic_gain_db(int db) { mic_db = db < 0 ? 0 : db > 36 ? 36 : db; }
int   audio_mic_gain_db(void)       { return mic_db; }
float audio_in_peak(void)  { float p = in_peak;  in_peak = 0;  return p; }
float audio_out_peak(void) { float p = out_peak; out_peak = 0; return p; }
int   audio_in_clips(void) { int c = in_clips;   in_clips = 0; return c; }
void  audio_set_input(int src) { in_src = src == AUDIO_IN_USB ? AUDIO_IN_USB : AUDIO_IN_MIC; }
int   audio_input(void)        { return in_src; }
