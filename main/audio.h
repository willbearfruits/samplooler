// I2S full duplex: INMP441 in, PCM5102A out, 64-frame blocks.
// S3: float path on core 1. C3: Q15 path at high priority on the single core.
#pragma once
#include <stdint.h>
void audio_i2s_init(void);    // claims the I2S port and its DMA memory; do this before recorder_init
void audio_run(void);         // starts the audio task
int  audio_cpu_percent(void); // share of the block budget used, smoothed
int  audio_mic_slot(void);    // 0 = mic data in the left slot, 1 = right (its L/R pin)
void  audio_set_mic_gain_db(int db);   // 0..36 dB in the S3 float path; the C3 rounds to 6 dB steps
int   audio_mic_gain_db(void);
#define AUDIO_SCOPE_N 128
void  audio_scope(int8_t *dst);   // the last 128 input samples, one in 8 (32 ms at 32 kHz), oldest first
float audio_in_peak(void);    // peak since the last call, 0..1+, of the signal going into the recorder
float audio_out_peak(void);
int   audio_in_clips(void);   // samples that hit the rail since the last call
enum { AUDIO_IN_MIC, AUDIO_IN_USB };
void  audio_set_input(int src);   // AUDIO_IN_USB takes what the host plays into the device (S3 only)
int   audio_input(void);
