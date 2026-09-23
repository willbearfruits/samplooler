#include "recorder.h"
#include "audio.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
static unsigned rs = 12345; static float rnd(void) { rs = rs * 1103515245u + 12345u; return ((rs >> 8) & 0xFFFF) / 65535.0f; }
void recorder_envelope(uint8_t *cols, int n) {
    for (int i = 0; i < n; i++) { float t = i / (float)n; float e = 0.15f + 0.85f * fabsf(sinf(t * 19.0f)) * (0.5f + 0.5f * sinf(t * 3.1f + 1.0f)); if (((i / 9) % 4) == 3) e *= 0.15f; cols[i] = (uint8_t)(e * 230 + rnd() * 20); }
}
void audio_scope(int8_t *dst) { static float ph; ph += 0.9f; for (int i = 0; i < AUDIO_SCOPE_N; i++) dst[i] = (int8_t)(70 * sinf(i * 0.25f + ph) * sinf(i * 0.03f + ph * 0.3f) + rnd() * 16 - 8); }
