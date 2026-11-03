// The front panel: everything between the controls and the instrument that is not a driver. The menu, the
// sound list (factory, user, your last edit), freeze, the looper's button, the coach, toasts, the splash,
// tilt setup, ABOUT, the screensaver, the frame, MIDI in and out, and the audio chain (instrument ->
// looper -> master). shell.c drives it on the device, tools/sim on the host; both give it the same inputs.
//
// Controls (the same on every instrument):
//   play      turn = change the sound (or the setting last edited in the menu); tap = menu;
//             hold the knob 0.6 s = hold the motion (freeze), again to let go; REC = the instrument's
//             (with INST.looper: hold = record / add, tap = stop / play)
//   menu      turn = choose; tap = open, edit, run; tap again = back up; hold = back to play
#pragma once
#include "core.h"

typedef struct {
    int      enc;              // detents since the last tick
    bool     tap;              // the knob went down and up within 0.5 s
    bool     pressed;          // the knob is down
    int      held_ms;          // ... for this long
    bool     rec;              // REC is down (debounced)
    bool     imu_ok;           // there is an IMU
    vec3_t   acc_raw, gyro_raw;            // in the IMU's own axes
    const vec3_t *burst_raw;   int nburst; // ditto, 1 kHz (instruments with INST.burst)
    bool     joy_ok;           // a joystick was found
    float    jx, jy;
    float    dt;
    uint32_t frames;
} front_in_t;

// Kept in flash next to the params (shell.c / the sim's stand-in store it as one blob).
typedef struct {
    int8_t   axis[3];          // screen x, y, z from the IMU's axes: 1..3 = +X..+Z, negative = reversed
    uint8_t  calibrated;
    uint8_t  user_mask;        // user sounds that exist (bit k: USER k+1)
    uint8_t  pad;
    int16_t  sound;            // the sound loaded last: 0.. factory, 100 + k user k, -1 your last edit
    uint32_t learned;          // coach bits
    char     quick[4];         // the param the knob turns in play (its tag); zeros: the sound
} front_meta_t;
extern front_meta_t meta;

typedef struct { const char *sound; bool edited, clip, frozen; float out_level, in_level; } front_status_t;
const front_status_t *front_status(void);

void front_init(bool has_imu, bool safe_mode, bool from_flash);   // after params_init() and settings; from_flash: the params came from flash
void front_tick(const front_in_t *in);           // 100 Hz
void front_draw(void);                           // one frame into oled.c's buffer
void front_audio(const float *in, float *out, int n);   // the audio task's process function
void front_midi(uint8_t status, uint8_t d1, uint8_t d2);   // a channel message from USB MIDI (UI task)
void front_sysex(uint8_t cmd, const uint8_t *body, int n); // one of the app's SysEx messages, F0 7D <cmd> <body> F7 (UI task)
void front_note(const char *s);                  // a boot note (CRASH: ..., WATCHDOG) for the first seconds
bool front_dirty(void);                          // params or meta changed since front_saved()
void front_saved(void);
bool front_quiet(void);                          // the output has been near silent for 0.3 s: a good moment to write flash
bool front_busy(void);                           // splash or tilt setup on screen (the shell holds off saving)
int  front_menu_level(void);                     // 0 closed, 1 pages, 2 items, 3 editing (for tools/sim)
int  front_screen(void);                         // 0 splash, 1 play, 2 tilt setup, 3 about; 4 the screensaver (for tools/sim)

// Asked of the platform (shell.c on the device, tools/sim on the host). UI task.
void plat_midi3(uint8_t status, uint8_t d1, uint8_t d2);
void plat_led(uint8_t r, uint8_t g, uint8_t b);
void plat_mic_gain(int db);
void plat_speaker(bool on);                             // the speaker amp on or off (the line out always plays)
void plat_rec_button(bool on);                          // the REC pin counts (off: only BOOT and MIDI record)
bool plat_save_user(int k, const uint8_t *b, int n);    // user sound k (0..7): its blob
int  plat_load_user(int k, uint8_t *b, int cap);        // bytes read, 0 if none
void plat_factory_reset(void);                          // erase the saved params and user sounds
int  plat_about(char lines[][32], int max);             // status lines for ABOUT: version, CPU, memory, uptime
bool plat_sysex(const uint8_t *m, int n);               // a whole SysEx message out on USB MIDI; false: not sent
const char *plat_version(void);                         // the firmware's version ("0.2.0")
const char *plat_serial(void);                          // the USB serial ("SG-727600D0"), "" without USB
