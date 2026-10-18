// What SING's voice engines expect from the instruments' core (core/firmware/main/core.h), for the Samplooler.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define AUDIO_SR     32000                 // the Samplooler's REC_SAMPLE_RATE
#define AUDIO_BLOCK  64                    // audio.c's FRAMES

void *app_alloc(size_t n);                 // zeroed, PSRAM when there is some (voice.c)
void *app_alloc_fast(size_t n);            // zeroed, internal RAM first: lines read all over every sample
void  app_free(void *p);
void  app_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
