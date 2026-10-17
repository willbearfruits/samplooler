// KIT (inst_kit.c): the break, and the slices in it. Owned by the KIT builder; the host and BEAT use what is here.
//
// Built-in breaks are rendered, not stored: an acoustic kit (kick, snare with its wires, hats, ride, crash, toms),
// played as a drummer would (ghost notes, a little swing and push, every hit a bit different), through the room it
// was "recorded" in and a bit of grit (a band-limited, slightly saturated, lightly crushed print, as off a record),
// written into a buffer in PSRAM. So BEAT chops them exactly as it chops anything sampled. No recording of anyone
// else's is in this firmware: the patterns are the classic ones (a rhythm is not anyone's), the sound is ours.
// A new built-in break (BREAK, or a change of PRINT, ROOM, TUNE ...) is rendered a little every tick into the spare
// buffer while the old one plays on (kit_rendering() says how far), then swapped in whole.
//
// A sampled break: REC held records the mic, the USB sound card (what the computer plays into the board) or WRECK's
// own output (RESAMPLE: mangle, resample, mangle again). The take starts at its first sound (a resample starts on the
// machine's step instead) and REC's own click is never in it. LENGTH AUTO trims it to whole beats at the machine's
// tempo (its bpm is then the machine's); 1, 2 or 4 BARS makes it that many bars and takes its tempo from its length
// (a resample: that many bars at the machine's tempo). It is cut into slices at its hits (snapped to the nearest
// sixteenth when close) or into 8, 16 or 32 equal parts (SLICES), each guessed a kind from its spectrum. Every break
// is printed to peak at -6 dBFS.
#pragma once
#include "core.h"

#define KIT_SLICES_MAX 64
enum { KS_KICK, KS_SNARE, KS_HAT, KS_RIDE, KS_CRASH, KS_TOM, KS_OTHER };   // what a slice mostly is

// The break now. A new one is swapped in whole (BEAT takes it at a block's start); the old buffer is not written
// again while beat_reading() still reports its serial.
typedef struct {
    const int16_t *pcm;        // mono, PSRAM
    int      len;              // frames
    int      steps;            // its length in sixteenths of its own beat: 4 x its beats (16 or 32 for the built-in
                               // ones; a take: 4 .. 64)
    float    bpm;              // the tempo it was played (or recorded) at: len = steps * 15 * AUDIO_SR / bpm (rounded)
    int      nslices;          // its hits, in order; slice 0 starts at 0
    int32_t  slice[KIT_SLICES_MAX];       // where each starts (frames), increasing
    uint8_t  kind[KIT_SLICES_MAX];        // KS_*: the built-in ones know, a recording guesses (low = kick, bright = hat...)
    uint8_t  step[KIT_SLICES_MAX];        // the sixteenth each starts on (0..steps-1), never decreasing
    uint8_t  peak[KIT_SLICES_MAX];        // its loudness, 0..255 (255: the break's loudest, -6 dBFS)
    uint32_t serial;           // new for every break (never 0)
    const char *name;          // "AMEN", "THINK", ... or "SAMPLED"
} brk_t;

const brk_t *kit_break(void);                 // any task: the current break (never NULL after init)
int   kit_rendering(void);                    // 0, or 1..99: how far a built-in break being rendered has got
// sampling (the host calls these; UI task)
enum { KSRC_MIC, KSRC_USB, KSRC_RESAMPLE };
void  kit_rec_press(int source);              // REC went down (the take may start now: nothing is lost to the hold test)
void  kit_rec_confirm(void);                  // ... it is a hold (0.3 s): sampling
void  kit_rec_release(bool confirmed);        // let go (confirmed false: it was a tap, forget the take)
bool  kit_recording(void);                    // a take is being recorded (or waits for its first sound)
bool  kit_armed(void);                        // ... it waits for its first sound (nothing heard yet)
float kit_rec_seconds(void);                  // how long it is so far
void  kit_feed(const float *in, int n, uint32_t f0);   // audio task, every block: what the take records from (the
                                              // host passes the mic, the USB input or the mix, as the source says)
// for the screen (UI task)
void  kit_envelope(uint8_t *cols, int n);     // peak per column over the break, 0..255
