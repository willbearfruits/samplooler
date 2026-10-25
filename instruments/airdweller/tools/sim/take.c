// See take.h: a recorded take (tools/motion_rec.py) back into the simulator, and the motion stream's messages back
// into numbers.
#include "take.h"
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- loading
static void take_err(const char *path, int line, const char *what)
{
    if (line) printf("  take %s, line %d: %s\n", path, line, what);
    else printf("  take %s: %s\n", path, what);
}

take_t *take_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { take_err(path, 0, "cannot open it"); return NULL; }
    take_t *t = calloc(1, sizeof *t);
    int cap = 0, lineno = 0;
    char *line = NULL; size_t lcap = 0; ssize_t len;
    bool bad = false, backwards = false;
    while ((len = getline(&line, &lcap, f)) >= 0) {
        lineno++;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#') continue;
        if (*s == '{') {
            if (t->header || t->n) { take_err(path, lineno, "a second header"); bad = true; break; }
            t->header = strdup(s);
            continue;
        }
        long long fr; float v[6]; int fl;
        if (sscanf(s, "%lld %f %f %f %f %f %f %d", &fr, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &fl) != 8) {
            take_err(path, lineno, "not 'frame ax ay az gx gy gz flags'"); bad = true; break;
        }
        if (t->n == cap) {
            cap = cap ? cap * 2 : 4096;
            t->frame = realloc(t->frame, (size_t)cap * sizeof *t->frame);
            t->acc = realloc(t->acc, (size_t)cap * sizeof *t->acc);
            t->gyro = realloc(t->gyro, (size_t)cap * sizeof *t->gyro);
            t->flags = realloc(t->flags, (size_t)cap);
        }
        int i = t->n++;
        if (i > 0 && fr < t->frame[i - 1]) { fr = t->frame[i - 1]; backwards = true; }
        t->frame[i] = fr;
        t->acc[i] = (vec3_t){ v[0], v[1], v[2] };
        t->gyro[i] = (vec3_t){ v[3], v[4], v[5] };
        t->flags[i] = (uint8_t)fl;
        if (fl & TAKE_GAP) t->gaps++;
    }
    free(line);
    fclose(f);
    if (!bad && t->n == 0) { take_err(path, 0, "no samples"); bad = true; }
    if (bad) { take_free(t); return NULL; }
    if (backwards) take_err(path, 0, "frames went backwards somewhere: held at the one before");
    if (!t->header) t->header = strdup("");
    if (t->frame[0] != 0) {                                     // frames count from the first sample
        int64_t f0 = t->frame[0];
        for (int i = 0; i < t->n; i++) t->frame[i] -= f0;
    }
    t->secs = (float)t->frame[t->n - 1] / (float)AUDIO_SR;
    return t;
}

void take_free(take_t *t)
{
    if (!t) return;
    free(t->frame); free(t->acc); free(t->gyro); free(t->flags); free(t->header);
    free(t);
}

// ---------------------------------------------------------------- the replay
void take_begin(take_t *t)
{
    t->pos = 0;
    t->base = audio_frames();
    t->rec = t->knob = false;
    t->knob_ms = 0;
}

bool take_done(const take_t *t) { return t->pos >= t->n; }

bool take_feed_at(take_t *t, uint32_t frames)
{
    if (take_done(t)) {                                         // the end: let go of REC and the knob
        SI.nburst = 0;
        SI.rec = false;
        if (SI.pressed && t->knob_ms >= 40 && t->knob_ms < 500) SI.tap = true;
        SI.pressed = false;
        t->rec = t->knob = false;
        t->knob_ms = 0;
        return false;
    }
    int64_t until = (int64_t)(uint32_t)(frames - t->base);     // this tick ends here, in the take's frames
    int k = 0;
    while (t->pos < t->n && t->frame[t->pos] < until && k < 64) {
        int i = t->pos++;
        SI.burst[k] = t->acc[i];
        SI.gburst[k] = (t->flags[i] & TAKE_GYRO) ? t->gyro[i] : (vec3_t){ 0, 0, 0 };
        t->rec = t->flags[i] & TAKE_REC;
        t->knob = t->flags[i] & TAKE_KNOB;
        k++;
    }
    SI.nburst = k;
    if (k) { SI.acc = SI.burst[k - 1]; SI.gyro = SI.gburst[k - 1]; }
    SI.rec = t->rec;
    if (SI.pressed && !t->knob && t->knob_ms >= 40 && t->knob_ms < 500) SI.tap = true;   // a short press (input.c)
    SI.pressed = t->knob;
    t->knob_ms = t->knob ? t->knob_ms + 10 : 0;
    return true;
}

bool take_feed(take_t *t) { return take_feed_at(t, audio_frames() + TICK_FRAMES); }

float take_run(take_t *t)
{
    float t0 = sim_t;
    take_begin(t);
    while (take_feed(t)) sim_tick();
    sim_tick();                                                 // the controls let go
    return sim_t - t0;
}

// ---------------------------------------------------------------- the header: just enough JSON to read a label
static const char *ws(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++; return s; }
static const char *skip_str(const char *s)                      // s on the opening quote
{
    for (s++; *s && *s != '"'; s++) if (*s == '\\' && s[1]) s++;
    return *s ? s + 1 : s;
}
static const char *skip_val(const char *s)
{
    s = ws(s);
    if (*s == '"') return skip_str(s);
    if (*s == '{' || *s == '[') {
        int depth = 0;
        while (*s) {
            if (*s == '"') { s = skip_str(s); continue; }
            if (*s == '{' || *s == '[') depth++;
            else if ((*s == '}' || *s == ']') && --depth == 0) return s + 1;
            s++;
        }
        return s;
    }
    while (*s && *s != ',' && *s != '}' && *s != ']') s++;
    return s;
}
// Calls back for each key of the object at s; stops when cb returns a value.
typedef const char *(*key_fn)(const char *key, int klen, const char *val, void *u);
static const char *obj_each(const char *s, key_fn cb, void *u)
{
    s = ws(s);
    if (*s != '{') return NULL;
    s = ws(s + 1);
    if (*s == '}') return NULL;
    for (;;) {
        if (*s != '"') return NULL;
        const char *k0 = s + 1, *e = skip_str(s);
        int kl = (int)(e - 1 - k0);
        s = ws(e);
        if (*s != ':') return NULL;
        s = ws(s + 1);
        const char *r = cb(k0, kl, s, u);
        if (r) return r;
        s = ws(skip_val(s));
        if (*s != ',') return NULL;
        s = ws(s + 1);
    }
}
typedef struct { const char *key; int klen; } want_t;
static const char *match(const char *key, int klen, const char *val, void *u)
{
    want_t *w = u;
    return klen == w->klen && !strncmp(key, w->key, (size_t)klen) ? val : NULL;
}
static const char *json_path(const char *json, const char *path)
{
    const char *s = json;
    while (s && *path) {
        const char *dot = strchr(path, '.');
        want_t w = { path, dot ? (int)(dot - path) : (int)strlen(path) };
        s = obj_each(s, match, &w);
        path += w.klen + (dot ? 1 : 0);
    }
    return s ? ws(s) : NULL;
}

bool take_get(const take_t *t, const char *path, char *out, int cap)
{
    const char *v = t && t->header ? json_path(t->header, path) : NULL;
    if (!v || !*v || cap <= 0) return false;
    const char *e = skip_val(v);
    if (*v == '"') { v++; if (e > v) e--; }                     // without the quotes
    int n = (int)(e - v);
    if (n > cap - 1) n = cap - 1;
    memcpy(out, v, (size_t)n);
    out[n] = 0;
    return true;
}

float take_num(const take_t *t, const char *path, float dflt)
{
    char b[64], *end;
    if (!take_get(t, path, b, sizeof b)) return dflt;
    float v = strtof(b, &end);
    return end == b ? dflt : v;
}

typedef struct { char (*keys)[24]; int n, max; } keys_t;
static const char *collect(const char *key, int klen, const char *val, void *u)
{
    (void)val;
    keys_t *k = u;
    if (k->n < k->max) { int n = klen < 23 ? klen : 23; memcpy(k->keys[k->n], key, (size_t)n); k->keys[k->n][n] = 0; k->n++; }
    return NULL;
}
int take_keys(const take_t *t, const char *path, char keys[][24], int max)
{
    const char *v = t && t->header ? json_path(t->header, path) : NULL;
    keys_t k = { keys, 0, max };
    if (v) obj_each(v, collect, &k);
    return k.n;
}

// ---------------------------------------------------------------- a motion-stream message
static int s14(const uint8_t *b) { int u = b[0] << 7 | b[1]; return u >= 8192 ? u - 16384 : u; }

bool take_unpack(const uint8_t *m, int len, take_pkt_t *p)
{
    if (len < 12 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 'm' || m[len - 1] != 0xF7) return false;
    for (int i = 1; i < len - 1; i++) if (m[i] & 0x80) return false;
    int n = m[10];
    if (n > MOTION_MAX || len != 12 + 12 * n) return false;
    p->seq = m[3];
    p->frames = 0;
    for (int i = 4; i < 9; i++) p->frames = p->frames << 7 | m[i];
    p->flags = m[9];
    p->n = n;
    for (int i = 0; i < n; i++) {
        const uint8_t *b = m + 11 + 12 * i;
        p->acc[i] = (vec3_t){ (float)s14(b) / 1024.0f, (float)s14(b + 2) / 1024.0f, (float)s14(b + 4) / 1024.0f };
        p->gyro[i] = (vec3_t){ (float)s14(b + 6) / 4.0f, (float)s14(b + 8) / 4.0f, (float)s14(b + 10) / 4.0f };
    }
    return true;
}
