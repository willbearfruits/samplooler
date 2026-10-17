// Single-producer single-consumer queues of small structs, lock free. One side pushes (an app's
// tick, the Wi-Fi task, the audio task), the other pops. Neither side ever waits: a full queue drops.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define SPSC(name, type, N)                                                                              \
    typedef struct { type buf[N]; uint32_t w, r; } name##_t;                                             \
    static inline bool name##_push(name##_t *q, const type *v)                                           \
    {                                                                                                    \
        uint32_t w = __atomic_load_n(&q->w, __ATOMIC_RELAXED);                                           \
        if (w - __atomic_load_n(&q->r, __ATOMIC_ACQUIRE) >= (N)) return false;                           \
        q->buf[w % (N)] = *v;                                                                            \
        __atomic_store_n(&q->w, w + 1, __ATOMIC_RELEASE);                                                \
        return true;                                                                                     \
    }                                                                                                    \
    static inline bool name##_pop(name##_t *q, type *v)                                                  \
    {                                                                                                    \
        uint32_t r = __atomic_load_n(&q->r, __ATOMIC_RELAXED);                                           \
        if (r == __atomic_load_n(&q->w, __ATOMIC_ACQUIRE)) return false;                                 \
        *v = q->buf[r % (N)];                                                                            \
        __atomic_store_n(&q->r, r + 1, __ATOMIC_RELEASE);                                                \
        return true;                                                                                     \
    }                                                                                                    \
    static inline void name##_clear(name##_t *q) { q->w = q->r = 0; }   /* only while neither side runs */
