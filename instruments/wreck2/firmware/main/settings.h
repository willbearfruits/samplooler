// The params, the front panel's meta (tilt, coach, the sound) and the user sounds in NVS, in a namespace
// named after the instrument. Blobs of tag/value pairs (params.c), so a firmware with more or fewer params
// still loads what it knows. Device only (the sim has its own stand-in).
#pragma once
#include <stdbool.h>
#include <stdint.h>

void settings_init(void);                  // NVS up (re-formatted only if it cannot be used as it is)
int  settings_load(bool params);           // meta (tilt, coach, sound) and, unless false (safe mode), the params; returns how many params
void settings_save(void);                  // now: params and meta (call when the output is quiet)
bool settings_user_save(int k, const uint8_t *b, int n);
int  settings_user_load(int k, uint8_t *b, int cap);
void settings_erase(void);                 // params and user sounds (meta is rewritten by the caller)
