// The rig's byte packing (rig/PROTOCOL.md), shared by rig.c, rig/twin and the tests. Every SysEx data byte is 7 bits.
#pragma once
#include <stdint.h>

// a signed 16-bit value as three bytes: bits 15..14, 13..7, 6..0
static inline uint8_t *rig_put16(uint8_t *p, int16_t v)
{
    uint16_t u = (uint16_t)v;
    *p++ = (uint8_t)((u >> 14) & 0x03); *p++ = (uint8_t)((u >> 7) & 0x7F); *p++ = (uint8_t)(u & 0x7F);
    return p;
}
static inline int16_t rig_get16(const uint8_t *p) { return (int16_t)(uint16_t)((p[0] & 3) << 14 | (p[1] & 0x7F) << 7 | (p[2] & 0x7F)); }

// a signed 14-bit value as two bytes, high first (clamped)
static inline uint8_t *rig_put14(uint8_t *p, int v)
{
    if (v > 8191) v = 8191;
    if (v < -8192) v = -8192;
    uint16_t u = (uint16_t)v & 0x3FFF;
    *p++ = (uint8_t)(u >> 7); *p++ = (uint8_t)(u & 0x7F);
    return p;
}
static inline int rig_get14(const uint8_t *p) { int u = (p[0] & 0x7F) << 7 | (p[1] & 0x7F); return u >= 8192 ? u - 16384 : u; }

// 32 bits as five 7-bit bytes, most significant first (the core's motion stream does the same)
static inline uint8_t *rig_put32(uint8_t *p, uint32_t v)
{
    for (int i = 4; i >= 0; i--) *p++ = (uint8_t)((v >> (7 * i)) & 0x7F);
    return p;
}
static inline uint32_t rig_get32(const uint8_t *p) { uint32_t v = 0; for (int i = 0; i < 5; i++) v = v << 7 | (p[i] & 0x7F); return v; }

// 8-bit bytes in groups of 7, each group led by a byte of their top bits (bit j = byte j's bit 7). n bytes become
// n + ceil(n / 7) bytes; 128 become 147.
static inline int rig_pack7(uint8_t *dst, const uint8_t *src, int n)
{
    int k = 0;
    for (int i = 0; i < n; i += 7) {
        int m = n - i < 7 ? n - i : 7;
        uint8_t hi = 0;
        for (int j = 0; j < m; j++) hi |= (uint8_t)((src[i + j] >> 7) << j);
        dst[k++] = hi;
        for (int j = 0; j < m; j++) dst[k++] = src[i + j] & 0x7F;
    }
    return k;
}
static inline int rig_unpack7(uint8_t *dst, const uint8_t *src, int n)
{
    int k = 0;
    for (int i = 0; i < n; i += 8) {
        int m = n - i < 8 ? n - i - 1 : 7;
        uint8_t hi = src[i];
        for (int j = 0; j < m; j++) dst[k++] = (uint8_t)(src[i + 1 + j] | (((hi >> j) & 1) << 7));
    }
    return k;
}
