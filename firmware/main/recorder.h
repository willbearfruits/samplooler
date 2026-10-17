// The DSP core: one take in RAM, a read head that runs forwards or backwards at
// any speed, seam crossfaded. Two playback modes:
//   TAPE   speed and pitch are one thing, like varispeed tape (cheapest, cleanest).
//   GRAIN  speed and pitch are separate: two overlapping Hann grains follow the
//          head at the chosen speed while each grain plays at the chosen pitch.
//          Speed 0 freezes the sound; backwards scrubbing keeps words intelligible.
// No allocation in the audio path, no double math, no libm calls.
// Float, 32 kHz, a 20 s take in PSRAM: recorder_f32.c.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define REC_SAMPLE_RATE  32000
typedef float rec_sample_t;

#define REC_SPEED_LIMIT  8.0f       // |speed| ceiling; above 2x the recording aliases audibly
#define REC_PITCH_MIN    0.25f
#define REC_PITCH_MAX    4.0f

enum { REC_IDLE, REC_RECORDING, REC_PLAYING };
enum { REC_MODE_TAPE, REC_MODE_GRAIN };

bool  recorder_init(void);
int   recorder_max_seconds(void);
void  recorder_process(const rec_sample_t *in, rec_sample_t *out, int n);   // audio task only

// From the UI task. Plain 32-bit stores; the audio task picks them up per block.
void  recorder_set_record(bool held);
void  recorder_set_speed(float s);          // head movement per sample: -LIMIT..+LIMIT, negative = backwards
void  recorder_set_pitch(float ratio);      // GRAIN only: 0.25..4, 1 = as recorded
void  recorder_set_mode(int mode);
void  recorder_set_grain_ms(int ms);        // GRAIN: 20..200
void  recorder_set_volume(float v);         // 0..2

int   recorder_state(void);
int   recorder_mode(void);
float recorder_loop_seconds(void);
float recorder_position(void);
float recorder_speed_now(void);             // slewed speed actually in use
float recorder_record_fill(void);
// Peak per column over the take so far (recording) or the loop (playing), 0..255. Sparse
// sampling, about 32 reads per column, cheap enough for every frame. Zeros when there is no take.
void  recorder_envelope(uint8_t *cols, int ncols);

// Take access for the sample slots (UI task, never while recording).
const int16_t *recorder_take(int *samples);          // the recorded samples, NULL if none
int16_t *recorder_load_begin(int *max_samples);      // stops playback, hands out the buffer to fill
void     recorder_load_end(int samples);             // samples written; starts playing them
int      recorder_input_is_usb(void);                // debugging aid
