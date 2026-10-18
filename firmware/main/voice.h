// The voice on the Samplooler's input: SING's voices (voice_engines.h), picked by the Voice setting. OFF (0) is the
// Samplooler as it always was: the input goes straight through and nothing runs. Each other voice is one of SING's
// 18 sounds: autotune, harmonies, a choir, a vocoder, pitch and formant shift, echo and reverb, in the Key and Scale
// set here (the harmonies and the vocoder play the key's first chord). What the voice makes is what the
// Samplooler records and, with Monitor on, what you hear live.
#pragma once
#include <stdbool.h>

#define VOICE_N    19                      // OFF and SING's 18
#define VOICE_KEYS 12
#define VOICE_SCALES 6

const char *vox_name(int k);             // "OFF", "CLEAN", "NATURAL TUNE" ...
const char *vox_key_name(int k);         // "C" .. "B"
const char *vox_scale_name(int s);       // "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR"
void  vox_init(void);                      // at boot: allocates (about 60 KB of PSRAM, 33 KB of internal RAM)
void  vox_select(int k, int key, int scale);   // UI task: the voice, the key (0 = C), the scale
void  vox_tick(float loop_s);            // UI task, every 10 ms: the echo's time follows the loop (a bar, 0: none)
void  vox_run(float *io, int n, float out_env);   // audio task, in place: the input in, the voice out.
                                           // out_env: the output's peak envelope (the speaker guard listens for it)
bool  vox_on(void);                      // a voice is selected (not OFF)
float vox_note(void);                    // the note the tuned voice sings now (MIDI, 0: none), for the screen
