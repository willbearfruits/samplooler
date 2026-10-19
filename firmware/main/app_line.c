// See app_line.h. In: 'I' who are you, 'D' describe, 'P' pNN=+1|-1|1. Out: 'I', a 'D' per setting then
// 'D' end=<count>, and 'V' tag= text= for each setting whose text changed, while the app keeps talking (it
// says 'I' every 2 s). After 5 s of silence, or a message that could not go out, nothing goes out until the
// app speaks again; then the changes it missed follow.
#include "app_line.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "usb.h"

#define QUIET_MS 5000
#define MAX_SETTINGS 48
#define TEXT_N 28
extern char usb_serial[];                           // usb/usb_descriptors.c

static const app_settings_t *A;
static int  quiet_ms = -1;                          // since the app last spoke; -1: no app, or it stopped reading
static int  desc = -1, described;                   // the next one in order[] to describe; -1: none pending
static bool ready;                                  // the app has the whole description: changes go out
static int  check_ms;
static EXT_RAM_BSS_ATTR char seen[MAX_SETTINGS][TEXT_N];   // the text the app has
static int  order[MAX_SETTINGS], norder;

// The app groups the settings by these names, in this order; a setting not in the table goes under SETTINGS
// at the end. The help is a line under the setting (docs/MANUAL.md has the long version).
static const char *const GROUPS[] = { "PLAY", "TILT", "SOUND", "VOICE", "SLOTS", "LINK", "SETTINGS" };
static const struct { const char *name, *group, *help; } INFO[] = {
    { "Mode", "PLAY", "TAPE: the pitch follows the speed. GRAIN: they are apart." },
    { "Pitch", "PLAY", "Semitones, in GRAIN only." },
    { "Grain", "PLAY", "The grain's length, in GRAIN only. Short is smooth, long is choppy." },
    { "Snap", "PLAY", "Speeds rounded to octaves and fifths." },
    { "Rec button", "PLAY", "Off: the stick's button is ignored. BOOT and MIDI note 60 still record." },
    { "Tilt", "TILT", "Off: it plays forward at 1x, whatever the angle." },
    { "Speed max", "TILT", "The speed at 45 degrees." },
    { "Speed min", "TILT", "The crawl when flat. 0 stops the tape." },
    { "Mic gain", "SOUND", "How hard the mic listens." },
    { "Volume", "SOUND", "Above 100% it can clip." },
    { "Input", "SOUND", "USB records what the computer plays into the board." },
    { "Monitor", "SOUND", "When the input (the voice) is heard." },
    { "Speaker", "SOUND", "Off: the line out and USB only. The cure for feedback." },
    { "Voice", "VOICE", "A voice on the input, before the recorder." },
    { "Key", "VOICE", "The key the voice tunes to. Shared with the boards on the link." },
    { "Scale", "VOICE", "The notes of the key." },
    { "Slot", "SLOTS", "The slot Save and Load use, with its length." },
    { "Save", "SLOTS", "Writes the loop to the slot." },
    { "Load", "SLOTS", "Reads the slot back into the loop." },
    { "Link", "LINK", "The radio to the other instruments." },
    { "Group", "LINK", "Boards in the same group play together." },
    { "Range", "LINK", "The radio's power. Lower is kinder to the supply." },
    { "Who is there", "LINK", "Names the boards heard right now, on the board's screen." },
};

static int info_of(int p)
{
    const char *nm = A->name(p);
    for (int i = 0; i < (int)(sizeof INFO / sizeof INFO[0]); i++) if (!strcmp(nm, INFO[i].name)) return i;
    return -1;
}
static const char *group_of(int p) { int i = info_of(p); return i < 0 ? "SETTINGS" : INFO[i].group; }

void app_line_init(const app_settings_t *s)
{
    A = s;
    norder = 0;
    int n = s->n < MAX_SETTINGS ? s->n : MAX_SETTINGS;
    for (size_t g = 0; g < sizeof GROUPS / sizeof GROUPS[0]; g++)
        for (int p = 0; p < n; p++) if (!strcmp(group_of(p), GROUPS[g])) order[norder++] = p;
}

static int addf(char *b, int cap, int k, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static int addf(char *b, int cap, int k, const char *fmt, ...)
{
    if (k >= cap - 1) return k;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(b + k, (size_t)(cap - k), fmt, ap);
    va_end(ap);
    return w < 0 ? k : (k + w > cap - 1 ? cap - 1 : k + w);
}

static bool send(char letter, const char *text)
{
    static EXT_RAM_BSS_ATTR uint8_t m[400];
    int k = 0;
    m[k++] = 0xF0; m[k++] = 0x7D; m[k++] = (uint8_t)letter;
    for (const char *s = text; *s && k < (int)sizeof m - 1; s++) {
        uint8_t c = (uint8_t)*s;
        if (c == '\n' || (c >= 0x20 && c < 0x7F)) m[k++] = c;
    }
    m[k++] = 0xF7;
    if (usb_midi_send_sysex(m, k)) return true;
    quiet_ms = -1; desc = -1;                       // nobody reads: quiet until the app speaks again
    return false;
}

static void value(int p)
{
    char b[80], now[TEXT_N];
    A->text(p, now, sizeof now);
    snprintf(b, sizeof b, "tag=p%02d\ntext=%s", p, now);
    if (send('V', b)) memcpy(seen[p], now, sizeof now);   // one that did not go out goes again later
}

static void describe(int p)
{
    char b[300];
    int i = info_of(p);
    A->text(p, seen[p], sizeof seen[p]);
    int k = addf(b, sizeof b, 0, "i=%d\ntag=p%02d\nname=%s\ngroup=%s\nkind=%s\ntext=%s", p, p, A->name(p), group_of(p),
                 A->is_action(p) ? "act" : "step", seen[p]);
    if (i >= 0 && INFO[i].help) k = addf(b, sizeof b, k, "\nhelp=%s", INFO[i].help);
    if (!strcmp(A->name(p), "Save")) k = addf(b, sizeof b, k, "\nconfirm=It writes over what is in the slot.");
    send('D', b);
}

void app_line_sysex(uint8_t cmd, const uint8_t *body, int n)
{
    if (!A) return;
    char t[32];
    if (n > (int)sizeof t - 1) n = (int)sizeof t - 1;
    for (int i = 0; i < n; i++) t[i] = (char)body[i];
    t[n < 0 ? 0 : n] = 0;
    quiet_ms = 0;
    if (cmd == 'I') {
        char b[160];
        snprintf(b, sizeof b, "name=Samplooler\nversion=%s\nserial=%s\nproto=1\nparams=%d",
                 esp_app_get_description()->version, usb_serial, norder);
        send('I', b);
    } else if (cmd == 'D') {
        desc = 0; described = 0; ready = false;
    } else if (cmd == 'P' && t[0] == 'p') {
        char *eq = strchr(t, '=');
        int p = atoi(t + 1);
        if (!eq || p < 0 || p >= A->n || p >= MAX_SETTINGS) return;
        int d = atoi(eq + 1);
        if (A->is_action(p)) { if (d >= 1) A->step(p, 1); }
        else if (d) A->step(p, d > 0 ? 1 : -1);
        value(p);                                   // also when it did not move: the app shows what the board has
    }
}

void app_line_tick(int dtm)
{
    if (!A || quiet_ms < 0) return;
    quiet_ms += dtm;
    if (quiet_ms > QUIET_MS) { quiet_ms = -1; desc = -1; return; }   // the app has gone
    for (int sent = 0; desc >= 0 && sent < 2; sent++) {
        if (desc >= norder) {
            char b[24];
            snprintf(b, sizeof b, "end=%d", described);
            desc = -1;
            ready = send('D', b);                   // not sent: the app asks again
            break;
        }
        describe(order[desc++]);
        described++;
    }
    if (!ready || (check_ms += dtm) < 100) return;
    check_ms = 0;
    char now[TEXT_N];
    for (int i = 0, n = 0; i < norder && n < 4 && quiet_ms >= 0; i++) {
        int p = order[i];
        A->text(p, now, sizeof now);
        if (strcmp(now, seen[p])) { value(p); n++; }
    }
}
