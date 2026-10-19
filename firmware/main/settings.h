// The settings table and its copy in NVS.
//
// Every setting is stored under a 4-character tag, so a blob written by another firmware version still loads:
// unknown tags are skipped, missing ones keep their default. Writes are debounced (3 s after the last change)
// and never happen while recording; while a loop plays they wait up to SETTINGS_MAX_DEFER_MS for an idle
// moment, then go ahead anyway (a page program is about a millisecond; the rare page erase stalls the audio
// for about 40 ms, which audio_flash_hint() files under "flash stalls").
#pragma once
#include <stdbool.h>

typedef struct {
    int   mic_db;        // 0..36
    int   mode;          // REC_MODE_TAPE / REC_MODE_GRAIN
    int   pitch_st;      // -24..24 semitones (GRAIN)
    bool  tilt;          // tilt drives the speed
    float spd_max;       // 0.25..REC_SPEED_LIMIT
    float spd_min;       // 0..1, the crawl at flat
    bool  snap;          // quantise speed to octaves and fifths
    int   grain_ms;      // 20..200
    int   vol_pct;       // 0..150
    int   input;         // AUDIO_IN_MIC / AUDIO_IN_USB
    int   slot;          // 0..SLOT_N-1
    bool  speaker;       // the speaker amp plays (off: only the line out; the cure for feedback)
    int   voice;         // 0 = OFF, else one of SING's voices on the input (voice.h)
    int   key;           // 0..11, C = 0: the key the voice tunes to
    int   scale;         // 0..5: MAJOR, MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR
    bool  monitor;       // hear the input (the voice) all the time, not only while recording
    bool  rec_button;    // the REC pin counts (off: BOOT and MIDI note 60 still record)
    bool  link;          // the flock (flock.h): the radio
    int   group;         // 1..8: boards in the same group sync
    int   range;         // 0 NEAR, 1 ROOM, 2 FAR: the radio's power
} settings_t;

#define SETTINGS_DEBOUNCE_MS   3000
#define SETTINGS_MAX_DEFER_MS  30000

bool settings_init(settings_t *s);                 // mounts NVS and loads over the defaults in *s
void settings_changed(void);                       // call after any change; schedules a write
void settings_poll(const settings_t *s);           // from the 10 ms loop
bool settings_pending(void);                       // a write is waiting
void settings_flush(const settings_t *s);          // write now if pending (before a reboot)
void settings_erase(void);                         // factory reset: forget the saved settings (the slots stay)
