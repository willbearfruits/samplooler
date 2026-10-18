// The voice (firmware/main/voice.c and SING's engines) on the host: a synthetic singer through every voice, in
// blocks of 64 like the board. Checks each voice sounds, stays under full scale, has no NaN, and that the tuned
// voices land in the key; prints the cost per block; writes out/<voice>.wav to listen to.
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR 32000
static int fails;
static void check(int ok, const char *what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

static float ph, z1[2], z2[2];
static float singer(float f0, float t)
{
    float f = f0 * (1.0f + 0.004f * sinf(6.2831853f * 5.5f * t));
    ph += f / SR; if (ph >= 1.0f) ph -= 1.0f;
    float x = (1.0f - 2.0f * ph) * (ph < 0.6f ? 1.0f : 0.4f);
    static const float fc[2] = { 730.0f, 1090.0f }, bw[2] = { 90.0f, 110.0f };
    float y = 0;
    for (int k = 0; k < 2; k++) {
        float r = expf(-3.14159f * bw[k] / SR), c = 2.0f * r * cosf(6.2831853f * fc[k] / SR);
        float o = (1.0f - r) * x + c * z1[k] - r * r * z2[k];
        z2[k] = z1[k]; z1[k] = o; y += o;
    }
    return 0.12f * y * 3.0f;
}
static void wav(const char *path, const float *x, int n)
{
    FILE *f = fopen(path, "wb"); if (!f) return;
    int bytes = n * 2;
    unsigned char h[45] = "RIFF____WAVEfmt \x10\0\0\0\x01\0\x01\0____\0\0\0\0\x02\0\x10\0data____";
    int v; v = 36 + bytes; memcpy(h + 4, &v, 4); v = SR; memcpy(h + 24, &v, 4); v = SR * 2; memcpy(h + 28, &v, 4); memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f);
    for (int i = 0; i < n; i++) { float s = x[i] * 32767.0f; s = s > 32767 ? 32767 : s < -32767 ? -32767 : s; short q = (short)s; fwrite(&q, 2, 1, f); }
    fclose(f);
}
static float pitch_of(const float *x, int n)
{
    int best = 0; double bc = 0;
    for (int lag = SR / 1000; lag < SR / 70; lag++) {
        double c = 0, e = 0;
        for (int i = 0; i < n - lag; i++) { c += (double)(x[i] * x[i + lag]); e += (double)(x[i + lag] * x[i + lag]); }
        if (e > 0 && c / sqrt(e) > bc) { bc = c / sqrt(e); best = lag; }
    }
    return best ? (float)SR / best : 0.0f;
}

int main(void)
{
    vox_init();
    const int secs = 3, N = secs * SR;
    float *out = malloc(N * sizeof(float));
    double worst_us = 0;
    for (int k = 0; k < VOICE_N; k++) {
        vox_select(k, 0, 0);                                       // C major
        ph = 0; memset(z1, 0, sizeof z1); memset(z2, 0, sizeof z2);
        float t = 0;
        struct timespec a, b; double us = 0;
        for (int i = 0; i < N; i += 64) {
            float blk[64];
            for (int j = 0; j < 64; j++) { blk[j] = singer(i < N / 2 ? 233.0f : 300.0f, t); t += 1.0f / SR; }   // Bb3, then off-key
            if (i % 320 == 0) vox_tick(0.0f);
            clock_gettime(CLOCK_MONOTONIC, &a);
            vox_run(blk, 64, 0.0f);
            clock_gettime(CLOCK_MONOTONIC, &b);
            us += (b.tv_sec - a.tv_sec) * 1e6 + (b.tv_nsec - a.tv_nsec) * 1e-3;
            memcpy(out + i, blk, sizeof blk);
        }
        us /= N / 64;
        if (us > worst_us) worst_us = us;
        float pk = 0; double e = 0; int bad = 0;
        for (int i = SR / 2; i < N; i++) { float v = out[i]; if (v != v) bad++; float m = fabsf(v); if (m > pk) pk = m; e += (double)(v * v); }
        float rms = (float)sqrt(e / (N - SR / 2));
        char b2[96]; snprintf(b2, sizeof b2, "out/%02d-%s.wav", k, vox_name(k));
        for (char *c = b2; *c; c++) if (*c == ' ') *c = '-';
        wav(b2, out, N);
        char msg[160];
        snprintf(msg, sizeof msg, "%-13s rms %.3f peak %.3f, %.1f us per block", vox_name(k), (double)rms, (double)pk, us);
        check(!bad && rms > 0.01f && pk < 0.99f, msg);
        if (k == 3) {                                              // HARD TUNE: 300 Hz (between D4 and D#4) lands on D4 (293.7)
            float f = pitch_of(out + N - SR / 2, SR / 4);
            snprintf(msg, sizeof msg, "HARD TUNE: 300 Hz sung comes out at %.1f Hz (D4 293.7)", (double)f);
            check(fabsf(f - 293.7f) < 3.0f, msg);
        }
    }
    printf("  worst %.1f us per 64-frame block on this PC\n", worst_us);
    printf("%d failed\n", fails);
    return fails ? 1 : 0;
}
