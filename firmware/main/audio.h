// I2S full duplex: INMP441 in, PCM5102A out, 64-frame blocks.
// Float path on core 1.
#pragma once
#include <stdint.h>
#include <stdbool.h>
void audio_i2s_init(void);    // claims the I2S port and its DMA memory; do this before recorder_init
void audio_run(void);         // starts the audio task
int  audio_cpu_percent(void); // share of the block budget used, smoothed
uint32_t audio_frames(void);  // frames handled before the current block (the recorder's recorder_frames() is the same count)
int64_t  audio_epoch_us(void); // esp_timer's time at frame 0, from the blocks' start times (the link maps frames between boards with it)
int  audio_mic_slot(void);    // 0 = mic data in the left slot, 1 = right (its L/R pin)
void  audio_set_mic_gain_db(int db);   // 0..36 dB
int   audio_mic_gain_db(void);
#define AUDIO_SCOPE_N 128
void  audio_scope(int8_t *dst);   // the last 128 input samples, one in 8 (32 ms at 32 kHz), oldest first
float audio_in_peak(void);    // peak since the last call, 0..1+, of the signal going into the recorder
float audio_out_peak(void);
int   audio_in_clips(void);   // samples that hit the rail since the last call
enum { AUDIO_IN_MIC, AUDIO_IN_USB };
void  audio_set_monitor(bool on);  // the Monitor setting: hear the input (the voice) all the time, not only while recording
void  audio_set_input(int src);   // AUDIO_IN_USB takes what the host plays into the device
int   audio_input(void);

// Health. Counters since boot; cpu_max is the worst block of the last second.
typedef struct { uint32_t blocks, rx_ovf, tx_ovf, read_err, stalls_flash; int cpu_avg, cpu_max; } audio_stats_t;
void  audio_get_stats(audio_stats_t *s);
void  audio_flash_hint(void);   // call before writing flash: DMA overflows in the next 500 ms count as flash stalls, not errors
void  audio_set_mute(bool on);  // 20 ms fade of everything going out (DAC, speaker, USB); the engine keeps running
bool  audio_silent(void);       // the mute fade has reached silence
void  audio_amp_enable(bool on);   // the speaker amp's SD pin (PIN_AMP_SD), raised by the audio task after 50 ms of good clock
void  audio_speaker(bool on);      // the Speaker setting: off holds the amp in shutdown (the line out and USB play on)
