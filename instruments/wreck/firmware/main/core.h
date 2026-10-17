// What an instrument sees of the device. Each firmware is one instrument (instrument.c fills INST); the
// core runs everything else: the controls, the menu, the sounds (factory and user presets), settings kept
// in flash, MIDI, the looper, the master limiter, the screen's frame, the coach, crash notes.
//
//   front.c   the core's logic, host-testable: controls -> menu / sounds / instrument, the frame, the audio chain
//   shell.c   the device: boot, drivers, the 10 ms loop around front.c, saving, USB, logs
//   tools/sim the host: the same front.c and instrument.c against stand-ins, scripted, checked, rendered
//
// Threads: tick(), draw(), changed(), action() run in the UI task (core 0, 100 Hz / 25 fps); audio() runs in
// the audio task (core 1, 64 frames at 32 kHz = every 2 ms). They share plain 32-bit variables (pv[], your
// own volatile floats and ints) and spsc.h queues. audio() must not allocate, log, block, take a lock,
// use double or call libm per sample (coefficients at block rate are fine).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// A big buffer only the UI task (or the radio's task) touches: in PSRAM when the build allows it
// (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY), so internal RAM stays for the sound's code and the radio. It must be
// zero at start (no initializer). Never for anything the audio task reads per sample.
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define UI_RAM EXT_RAM_BSS_ATTR
#else
#define UI_RAM
#endif

#define AUDIO_SR     32000
#define AUDIO_BLOCK  64

typedef struct { float x, y, z; } vec3_t;

// ---------------------------------------------------------------- controls, each 10 ms tick
typedef struct {
    float    dt;               // seconds since the last tick, nominally 0.01
    uint32_t frames;           // audio_frames() at this tick
    uint32_t ticks;            // since boot
    // REC: the record button, BOOT, the joystick's click (same pin) or MIDI note 60.
    // With INST.looper the core's looper owns REC and the instrument sees rec = false.
    bool     rec, rec_press, rec_release;
    int      rec_ms;           // how long REC has been down; at rec_release, how long it was held
    // Motion, in the screen's axes: x to the right, y towards the bottom edge, z out of the screen. Flat on a
    // table, screen up: acc = (0, 0, 1). The IMU's own axes are mapped by SYSTEM > TILT SETUP.
    bool     has_imu;
    bool     frozen;           // the knob is held in play: motion stays where it was (gyro reads 0)
    vec3_t   acc;              // g
    vec3_t   gyro;             // deg/s
    float    gyro_mag;         // |gyro|
    float    lean_x, lean_y;   // degrees, smoothed: right edge down > 0, bottom edge down > 0
    const vec3_t *burst;       // INST.burst: the 1 kHz accelerometer samples since the last tick, oldest first
    int      nburst;
    // The joystick (Stack boards and the kit; found at boot, absent on the Mini). x right, y up, -1..1,
    // dead zone applied. 0, 0 without one.
    bool     has_joy;
    float    jx, jy;
    bool     menu_open;        // the menu has the knob; motion, joystick and REC still come through
    // The knob, only while the instrument holds it (knob_grab(true)): in play the core then leaves it alone
    // (no sound change, no menu, no motion hold) and hands it over here.
    int      knob;             // detents turned this tick, signed
    bool     knob_tap;         // pressed and let go within 0.5 s without turning
    bool     knob_hold;        // held 0.6 s without turning (fires once per press)
    bool     knob_down;        // the knob is down now
    bool     knob_turned;      // ... and has been turned since it went down (press and turn)
} ctl_t;

// ---------------------------------------------------------------- parameters
// Each instrument lists its parameters in a table; the core shows them in the menu (by group), keeps them
// in flash, sends and takes them as MIDI CC 20 + index, and loads them from presets. Values live in pv[]
// (floats), indexed like the table: read them anywhere, the audio task included. Write them only through
// param_set() (UI task), which clamps, tells changed() and marks the settings for saving.
enum { PK_NUM, PK_ENUM, PK_ACT };          // a number; one of labels[]; something to run (tap)
enum {
    PF_INT    = 1 << 0,                    // whole numbers
    PF_LOG    = 1 << 1,                    // step is a ratio per detent (1.06: 6 %); min must be > 0
    PF_NOCC   = 1 << 2,                    // not on MIDI CC
    PF_SIGNED = 1 << 3,                    // print a + on positive values
    PF_SYS    = 1 << 4,                    // a device setting: not part of a sound (preset)
    PF_HIDE   = 1 << 5,                    // not in the menu (kept and on CC only)
};
typedef struct {
    const char *name;          // menu label, caps, <= 60 px in F_SMALL (tools/sim checks)
    const char *tag;           // 4 chars, unique, for flash and presets. Never reuse one for a new meaning
    uint8_t     group;         // index into INST.groups
    uint8_t     kind, flags;
    float       min, max, step, def;
    const char *unit;          // printed after the number: "ms" "s" "dB" "%" "st" "Hz" "x" ""; "s" shows ms below 1 s
    const char *const *labels; // PK_ENUM: one per value from min to max
    const char *help;          // one line shown while editing, <= 124 px in F_TINY
} param_t;

#define PARAMS_MAX 256
extern float pv[PARAMS_MAX];                           // the instrument's params first, then the core's
void  param_set(int p, float v);                       // UI task only
const char *param_text(int p, char *b, int n);         // the value as the menu shows it

// ---------------------------------------------------------------- sounds (presets) and the coach
// A factory sound: a name and the params it sets, as "tag=value tag=value ..." (enums by index).
// Everything not named takes its default.
typedef struct { const char *name; const char *set; } preset_t;

// The coach shows each hint at the bottom of the play screen while the player is idle, until the player
// has done what it says once (learn), then never again (SYSTEM > HINTS brings them back).
enum { LEARN_TURN, LEARN_MENU, LEARN_REC, LEARN_HOLD, LEARN_JOY, LEARN_TILT, LEARN_INST = 8 };   // 8..31: yours
typedef struct { const char *text; uint8_t learn; } hint_t;   // text: two lines of 112 px in F_SMALL at most
void coach_done(int learn);                            // the player did it (UI task)

// ---------------------------------------------------------------- what draw() gets
typedef struct {
    uint32_t frame;            // frames drawn since boot (25 per second)
    float    t;                // seconds since boot
    int      cover;            // rows at the bottom the core draws over this frame: 0, or 18 for the edit strip,
                               // a hint or the sound card; the list menu covers 16..63 (draw() is then skipped)
    float    out_level, in_level;   // peak meters 0..1, decaying
} view_t;

// ---------------------------------------------------------------- the instrument
typedef struct {
    const char *name;          // "KNOCK": header, splash, USB, flash namespace (lowercase)
    const char *tagline;       // "KNOCK ON IT TO DRUM": the splash, the ABOUT card
    const param_t *params;     int nparams;
    const char *const *groups; int ngroups;    // menu pages of your params, in order
    const preset_t *presets;   int npresets;   // factory sounds, the first is the default
    const hint_t *hints;       int nhints;
    bool  burst;               // wants the 1 kHz accelerometer stream (ctl.burst)
    bool  looper;              // the core looper records what audio() plays: REC is its (hold = record/add, tap = stop/play)
    const char *knob;          // tag of the param the knob turns in play (until the player edits another in the menu);
                               // NULL: the knob changes the sound
    bool  midi_notes;          // MIDI notes play the instrument: notes 60 and 64 go to midi_in(), not to REC / hold
                               // (REC from MIDI is then the sustain pedal, CC 64, which works for every instrument)
    void (*init)(void);        // once at boot, UI task, before audio() first runs: allocate here (app_alloc)
    void (*audio)(const float *in, float *out, int n);   // in: the mic, high-passed at 80 Hz, gain applied
    void (*tick)(const ctl_t *c);                        // 100 Hz
    void (*draw)(const view_t *v);                       // the whole 128x64 frame; ui_head() for the header band
    bool (*splash)(int frame);                           // boot animation, 25 fps: draw frame n, false when done (<= 100 frames)
    void (*changed)(int p);    // param p changed (from the menu, MIDI or a sound); -1: all of them. May be NULL
    void (*action)(int p);     // a PK_ACT param was run from the menu. May be NULL
    void (*midi_in)(uint8_t status, uint8_t d1, uint8_t d2);   // channel messages the core does not use. May be NULL
    void (*saver)(uint32_t frame);                       // screensaver frame; NULL: the core's
    bool (*hold)(void);        // the knob held 0.6 s in play: true if the instrument used it (then the motion is not held).
                               // May be NULL
} instrument_t;
extern const instrument_t INST;

// ---------------------------------------------------------------- the platform (shell.c, tools/sim)
void    *app_alloc(size_t n);  // zeroed, PSRAM when there is some; NULL when out of memory. In init()
// zeroed, internal RAM first (PSRAM if there is none left): for what the audio reads all over, every sample (reverb
// lines, grain rings). PSRAM goes through a 32 KB cache, and buffers 4 KB apart share its slots: several such lines
// read and written per sample miss on nearly every access (measured: a reverb at 2 ms of every 2 ms block). In init()
void    *app_alloc_fast(size_t n);
void     app_free(void *p);
uint32_t audio_frames(void);   // frames since boot at the start of the current (audio task) or last block
void     midi_note(int note, int vel);           // on the MIDI channel set in SYSTEM; vel 0 = note off
void     midi_cc(int cc, int val);
void     midi_note_ch(int ch, int note, int vel); // on channel ch (1..16), e.g. drums on 10
void     midi_bend(int value);                   // pitch bend, 0..16383 (8192 = none), on the MIDI channel
void     app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void     led_rgb(uint8_t r, uint8_t g, uint8_t b);   // the board's RGB LED, 0..255, scaled by SYSTEM > LED
void     toast(const char *text, int ms);           // a message box over the scene
void     ui_awake(void);       // playing without touching the controls (knocking, blowing): keep the screen on
void     menu_leave(void);     // close the menu (an action that needs the whole screen, e.g. a setup flow in draw())
void     coach_pause(void);    // a full-screen flow of the instrument's is up: no hint over it (call every tick while it lasts)
void     knob_grab(bool on);   // the instrument takes the knob in play (an editor of its own): ctl.knob*, no menu, no hints
