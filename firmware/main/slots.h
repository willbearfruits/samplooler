// Sample slots: 8 mono 16-bit WAV files on the wear-levelled FAT partition.
//
// Saving and loading are chunked jobs driven from the 10 ms loop, so the screen keeps drawing and MIDI
// keeps flowing while the flash works (a 20 s take is about 1.3 MB, well over ten seconds of erases).
// The caller mutes the output first: every sector erase stalls both cores for tens of milliseconds, and
// a muted stall is silence while an unmuted one is a stutter. A save writes to a temp file and renames
// it at the end, so a power cut mid-save never leaves a half slot.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SLOT_N 8

bool  slots_init(void);
int   slots_length(int slot);                          // samples in the slot's file, 0 if empty

bool  slots_save_begin(int slot, const int16_t *samples, int n);
bool  slots_load_begin(int slot, int16_t *dst, int max);
bool  slots_step(void);                                // one chunk; true while the job is still running
bool  slots_busy(void);
float slots_progress(void);                            // 0..1
bool  slots_result_ok(void);                           // of the last finished job
int   slots_loaded(void);                              // samples delivered by the last load
