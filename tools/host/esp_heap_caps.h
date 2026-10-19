// Host stand-in for the firmware's allocator (tools/flocksim, tools/soak): plain malloc.
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM   1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT     4
static inline void *heap_caps_malloc(size_t n, int caps) { (void)caps; return malloc(n); }
static inline void *heap_caps_calloc(size_t n, size_t s, int caps) { (void)caps; return calloc(n, s); }
