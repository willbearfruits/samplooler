// The record button, the encoder (PCNT + push) and the joystick (ADC1, when joy.on): the pins only. What a press or a
// turn means is gesture.c's, by the tuning table. The rig's keys (rig_keys) come in here as if from the pins.
// HAS_CONTROLS targets only.
#pragma once
#include <stdbool.h>

typedef struct {
    int   enc;                 // detents turned since last poll, signed
    bool  tap, hold, longp;    // encoder push: released before enc.hold; released after it; held enc.long (once)
    bool  pressed; int held_ms;
    bool  rec;                 // the record button is down (PIN_REC)
    bool  rec_pc;              // the rig holds REC down (like MIDI note 60: not subject to the Rec button setting)
    bool  flick_l, flick_r, flick_u, flick_d;
    float jx, jy;              // -1..1 after gesture.c, 0 when no joystick
    int   raw_x, raw_y;        // the joystick's ADC (0..4095), 0 when off
    bool  has_joy;
} input_ev_t;

void input_init(void);
void input_poll(input_ev_t *ev);   // call every ~10 ms
void input_tune(void);             // the tuning table changed (the encoder's glitch filter)
void input_joy_centre(void);       // the joystick's rest position, now
