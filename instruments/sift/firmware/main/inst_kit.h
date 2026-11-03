// KIT: WRECK's kit as SIFT keeps it (inst_amen.c): the types its renderer uses. The kit plays a break's pattern on a
// synthesized acoustic kit, through a room and a print, into a buffer: SIFT's corpus, four takes of the Amen.
#pragma once
#include "core.h"

// the breaks (patterns), and the kit's settings (kitp[] in inst_amen.c)
enum { B_AMEN, B_THINK, B_APACHE, B_FUNKY, B_IMPEACH, B_BOOMBAP, B_BLAST, B_DOOM, B_DRUMS, NPAT, B_SAMPLED = NPAT };
enum { KP_BREAK, KP_PRINT, KP_ROOM, KP_TUNE, KP_TONE, KP_DECAY, KP_SWING, KP_GHOSTS, KP_HUMAN, KP_TAKE,
       KP_LENGTH, KP_SLICES, KP_GATE, KP_N };
extern const char *const kit_break_l[NPAT + 1];
#define KIT_SLICES_MAX 64
enum { KS_KICK, KS_SNARE, KS_HAT, KS_RIDE, KS_CRASH, KS_TOM, KS_OTHER };   // what a slice mostly is

// A printed take: where it is, how long, its tempo, and where its hits are.
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

const brk_t *kit_break(void);                 // the take printed last

// SIFT: the Amen into the corpus (UI task)
int   amen_length(int takes);                 // frames for that many takes
bool  amen_begin(int16_t *dst, int max, int takes);
int   amen_step(void);                        // a tick's worth; frames final so far
bool  amen_busy(void);
int   amen_cuts(int32_t *out, int max);       // where every hit starts, frames into dst
