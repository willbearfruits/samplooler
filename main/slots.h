// Sample slots: WAV files on the FAT partition. Mono 16-bit at the engine rate.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SLOT_N 8
bool slots_init(void);                                   // mounts /storage, formats it the first time
bool slots_save(int slot, const int16_t *samples, int n);
int  slots_load(int slot, int16_t *dst, int max);        // samples read, 0 if empty or missing
int  slots_length(int slot);                             // samples, 0 if empty
