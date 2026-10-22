// I2S full duplex: INMP441 in, PCM5102A (and the optional MAX98357A) out, 64-frame blocks at 32 kHz,
// float, on core 1. The block in the middle is whatever audio_set_process() points at: the open app.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "core.h"           // AUDIO_SR, AUDIO_BLOCK

typedef void (*audio_process_t)(const float *in, float *out, int n);

void audio_i2s_init(void);    // claims the I2S port and its DMA memory
void audio_run(void);         // starts the audio task
void audio_set_process(audio_process_t fn);   // NULL: silence. Takes effect at the next block
uint32_t audio_blocks(void);  // blocks done since boot: wait for it to move on before freeing what the old process used
uint32_t audio_frames(void);  // frame counter at the start of the current block (in the audio task) or of the last one
int64_t  audio_epoch_us(void); // esp_timer's time at frame 0 (from the blocks' start times; for the link)
int  audio_cpu_percent(void); // share of the block budget used, smoothed
int  audio_mic_slot(void);    // 0 = mic data in the left slot, 1 = right (its L/R pin)
void audio_set_mic_gain_db(int db);   // 0..36 dB
int  audio_mic_gain_db(void);
#define AUDIO_SCOPE_N 128
void  audio_scope(int8_t *dst);   // the last 128 input samples, one in 8 (32 ms), oldest first
float audio_in_peak(void);    // peak since the last call, 0..1+
float audio_out_peak(void);
int   audio_in_clips(void);   // samples that hit the rail since the last call

// Health. Counters since boot; cpu_max is the worst block of the last second.
typedef struct { uint32_t blocks, rx_ovf, tx_ovf, read_err, stalls_flash; int cpu_avg, cpu_max; } audio_stats_t;
void  audio_get_stats(audio_stats_t *s);
void  audio_flash_hint(void);   // call before writing flash: DMA overflows in the next 500 ms count as flash stalls
void  audio_set_mute(bool on);  // 20 ms fade of everything going out (DAC, speaker, USB); the engine keeps running
bool  audio_silent(void);       // the mute fade has reached silence
void  audio_amp_enable(bool on);   // the speaker amp's SD pin, raised by the audio task after 50 ms of good clock
void  audio_speaker(bool on);      // SYSTEM > SPEAKER: off holds the speaker amp in shutdown (the line out and USB play on)
