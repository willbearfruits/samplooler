// The screen. main.c fills a ui_t every tick, the screen's task draws the latest with ui_draw(); everything visual
// lives here.
// 128x64: a 16-row header band (the yellow strip on two-colour panels) and a 48-row body.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define UI_ROWS   7
#define UI_VAL_N  20
enum { UI_KNOB_JOG, UI_KNOB_SPEED, UI_KNOB_PITCH, UI_KNOB_FILTER, UI_KNOB_N };
enum { UI_HAND_OFF, UI_HAND_FILTER, UI_HAND_PITCH, UI_HAND_BRAKE, UI_HAND_VOLUME, UI_HAND_ECHO, UI_HAND_N };

typedef struct {
    int   state, mode;               // REC_*, REC_MODE_*
    float speed_now, speed_cmd, spd_max;
    float pitch_ratio;
    float tilt_pitch, tilt_roll;     // degrees
    bool  has_imu, tilt_on, has_joy;
    bool  frozen;                    // the tilt reading is latched (encoder held while playing)
    float pos, rec_fill, loop_s;     // loop position 0..1, record fill 0..1, loop length in seconds
    int   max_s;
    float meter_in, meter_out;       // 0..1+
    bool  clip;
    bool  fx_delay, fx_stutter;
    int   cpu;
    // menu and the transient setting toast
    bool  menu, menu_edit;           // the list, or editing the chosen setting
    int   param, param_n;
    const char *param_name, *param_val;
    float param_frac;                // 0..1 position of the value in its range, <0 for actions
    int   toast_ms;                  // >0: show the setting bar for that long
    // a slot job in progress (the output is muted meanwhile), and a short note: the job's result, or the
    // reason for the last reboot when it was not a clean one ("CRASH: slot save", "BROWNOUT")
    const char *busy_text;           // "SAVING 3", NULL when idle
    float       busy_frac;           // 0..1
    const char *note;                // NULL for none
    int   idle_ms;                   // since the last touch of any control; the screensaver after 3 min idle
    const char *voice;               // the Voice's name, NULL when it is OFF
    int   peers;                     // boards heard on the link (flock.h): a glyph in the header
    int   beat_flash;                // > 0 just after a beat of the flock's grid (3 on the bar): the glyph pulses
    bool  closing;                   // REC let go, the take records on to the beat
    // ---- since rev E ----
    uint32_t now_ms;                 // the clock, for motion that keeps its pace whatever the frame rate; 0 (the hosts'
                                     // older sims): every frame is 50 ms after the last
    bool  waiting;                   // REC is down and the take waits for the first sound
    bool  has_knob, has_hand;        // the left knob is there; the hand's sensor answers
    int   knob_mode, hand_mode;      // the Knob and Hand settings: UI_KNOB_*, UI_HAND_*
    float jog;                       // the extra speed the knob's push gives the tape right now (x)
    int   knob_st;                   // Knob SPEED: semitones on the tape
    float hand;                      // 0 no hand .. 1 fully down
    bool  hand_seen;                 // a hand is over the sensor
    float filter;                    // -1 a closed low-pass .. 0 open .. 1 a high-pass
    // the menu as a list: the chosen row and three either side (rows outside the list have a NULL name)
    int   menu_sel, menu_n;          // menu_n 0: no list (an older caller), the strip shows param_name / param_val
    const char *menu_group;          // the chosen row's group, for the header
    const char *row_name[UI_ROWS];
    char  row_val[UI_ROWS][UI_VAL_N];
    bool  row_act[UI_ROWS];          // an action: a press runs it
    // lying untouched: the panel dims, and after ten minutes of a loop playing to nobody the screen goes sparse
    bool  dim;
    int   still_ms;
    // the board check (SYSTEM > Board check): every part and control, live
    bool  check;
    bool  chk_sw, chk_rec;           // the menu knob's push and REC are down
    int   chk_enc, chk_knob;         // detents turned since the check opened
    int   hand_mm;                   // the sensor's reading (0: nothing in range)
    const char *version;
} ui_t;

void ui_draw(const ui_t *u);         // draws one frame and flushes; the screen's task calls it (screen.fps, 30 by default)
int  ui_fps(void);                   // frames drawn in the last second
void ui_fault(const char *l1, const char *l2);   // a boot failure that stops the device: two short lines, stays on
