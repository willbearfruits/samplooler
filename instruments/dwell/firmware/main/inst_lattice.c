// The lattice: ratios of small whole numbers on one fundamental (just intonation), as La Monte Young tunes a drone.
// A cell (b, c) is 3^b * p^c brought into one octave, p the LIMIT's prime (7, 5 or 3: with 3 only the fifths' axis
// is there). b runs along the fifths (3/2, 9/8, 27/16 up; 4/3, 16/9, 32/27 down), c along the sevenths (7/4, 49/32 up;
// 8/7, 64/49 down), and the corners are their products (21/16, 7/6, 63/32 ...). Only integers here: the names are exact.
#include "inst_dwell.h"
#include <math.h>
#include <stdio.h>

static int prime_of(int limit) { return limit == LIM_5 ? 5 : limit == LIM_3 ? 3 : 7; }

// numerator and denominator before the octave: 3^b p^c as n/d, then powers of two until 1 <= n/d < 2
static void nd(int limit, int b, int c, uint32_t *n, uint32_t *d)
{
    if (limit == LIM_3) c = 0;
    uint32_t p = (uint32_t)prime_of(limit), num = 1, den = 1;
    for (int i = 0; i < b; i++) num *= 3;
    for (int i = 0; i < -b; i++) den *= 3;
    for (int i = 0; i < c; i++) num *= p;
    for (int i = 0; i < -c; i++) den *= p;
    while (num >= 2 * den) den *= 2;
    while (num < den) num *= 2;
    *n = num; *d = den;
}

float lat_ratio(int limit, int b, int c)
{
    uint32_t n, d;
    nd(limit, b, c, &n, &d);
    return (float)n / (float)d;
}

void lat_name(int limit, int b, int c, char *buf, int n)
{
    uint32_t num, den;
    nd(limit, b, c, &num, &den);
    snprintf(buf, (size_t)n, "%u/%u", (unsigned)num, (unsigned)den);
}

float lat_cents(float ratio) { return 1200.0f * log2f(ratio); }

float lat_cents_et(float ratio)
{
    float c = lat_cents(ratio);
    float r = c - 100.0f * roundf(c / 100.0f);
    return r;
}

int lat_nearest(int limit, float cents, int *b, int *c)
{
    float best = 1e9f;
    int found = 0;
    cents = fmodf(cents, 1200.0f);
    if (cents < 0.0f) cents += 1200.0f;
    for (int bb = -LAT_B; bb <= LAT_B; bb++)
        for (int cc = -(limit == LIM_3 ? 0 : LAT_C); cc <= (limit == LIM_3 ? 0 : LAT_C); cc++) {
            float d = fabsf(lat_cents(lat_ratio(limit, bb, cc)) - cents);
            if (d > 600.0f) d = 1200.0f - d;
            if (d < best) { best = d; *b = bb; *c = cc; found = 1; }
        }
    return found;
}
