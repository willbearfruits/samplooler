// The app's line (app/ on a computer) for the Samplooler: the SysEx protocol the instruments' core speaks
// (core/firmware/main/front.c, protocol 1), over main.c's settings. They are steps, not ranges: each one is
// described as kind=step and moved with 'P' pNN=+1 or -1, like a turn of the encoder; Save, Load and the like
// are kind=act and run with 'P' pNN=1. The text is main.c's own, as the screen shows it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int n;                                          // settings, indexed 0..n-1
    const char *(*name)(int p);
    void (*text)(int p, char *b, int n);
    void (*step)(int p, int d);                     // -1 or +1, as the encoder; an action runs at +1
    bool (*is_action)(int p);
} app_settings_t;

void app_line_init(const app_settings_t *s);
void app_line_sysex(uint8_t cmd, const uint8_t *body, int n);   // from usb_midi_poll(), UI task
void app_line_tick(int dtm);                                    // the 10 ms loop
