// What sent an advertisement, guessed from its data, and the salted hash that stands in for its address.
// Only the structures named below are looked at; names, payloads and addresses are never kept.
// The guess is coarse: it picks a bird, nothing more.
#include "inst_chirp.h"

const char *const cat_name[CAT_N] = { "PHONE", "EARBUDS", "WATCH", "TRACKER", "BEACON", "LAPTOP", "SPEAKER", "OTHER" };

static uint8_t by_company(uint16_t id, const uint8_t *d, int n)
{
    switch (id) {
    case 0x004C:                                        // Apple: the Continuity message type that follows
        if (n >= 1) switch (d[0]) {
            case 0x02: return CAT_BEACON;               // iBeacon
            case 0x07: return CAT_EARBUDS;              // proximity pairing (AirPods, Beats)
            case 0x09: return CAT_SPEAKER;              // AirPlay target
            case 0x0B: return CAT_WATCH;                // Magic Switch
            case 0x12: return CAT_TRACKER;              // Find My (AirTag, or something offline)
            default: return CAT_PHONE;                  // Nearby Info, Handoff, AirDrop, tethering...
        }
        return CAT_PHONE;
    case 0x0006: return CAT_LAPTOP;                     // Microsoft (Windows, Swift Pair)
    case 0x0075: return CAT_PHONE;                      // Samsung
    case 0x00E0: return CAT_PHONE;                      // Google
    case 0x0087: return CAT_WATCH;                      // Garmin
    case 0x0157: case 0x038F: case 0x027D: return CAT_WATCH;   // Huami, Xiaomi, Huawei: mostly bands and watches
    case 0x009E: case 0x012D: return CAT_EARBUDS;       // Bose, Sony
    case 0x0057: case 0x0171: return CAT_SPEAKER;       // Harman (JBL), Amazon
    case 0x0059: case 0x0499: return CAT_BEACON;        // Nordic, Ruuvi: sensors and beacons
    default: return 0xFF;
    }
}

static uint8_t by_uuid16(uint16_t u)
{
    switch (u) {
    case 0xFEAA: return CAT_BEACON;                     // Eddystone
    case 0xFE2C: return CAT_EARBUDS;                    // Google Fast Pair (mostly headphones)
    case 0xFD6F: return CAT_PHONE;                      // exposure notification
    case 0xFE9F: return CAT_PHONE;                      // Google
    case 0xFEED: case 0xFEEC: case 0xFD5A: case 0xFD84: return CAT_TRACKER;   // Tile, Samsung SmartTag, Tile
    case 0x180D: case 0x1816: case 0x1814: return CAT_WATCH;    // heart rate, cycling, running
    case 0x1812: return CAT_LAPTOP;                     // HID: keyboards, mice
    default: return 0xFF;
    }
}

static uint8_t by_appearance(uint16_t a)
{
    switch (a >> 6) {
    case 1: return CAT_PHONE;
    case 2: case 15: return CAT_LAPTOP;                 // computer, HID
    case 3: case 13: case 17: case 18: return CAT_WATCH; // watch, heart rate, running, cycling
    case 5: case 10: case 33: return CAT_SPEAKER;       // display, media player, audio sink
    case 8: case 9: return CAT_TRACKER;                 // tag, keyring
    case 37: return CAT_EARBUDS;                        // wearable audio device
    default: return 0xFF;
    }
}

uint8_t adv_category(const uint8_t *data, int len, uint8_t adv_type)
{
    uint8_t mfg = 0xFF, uuid = 0xFF, app = 0xFF;
    for (int i = 0; i + 1 < len; ) {
        int l = data[i];
        if (l == 0 || i + 1 + l > len) break;
        uint8_t t = data[i + 1];
        const uint8_t *d = data + i + 2;
        int n = l - 1;
        if (t == 0xFF && n >= 2 && mfg == 0xFF) mfg = by_company((uint16_t)(d[0] | d[1] << 8), d + 2, n - 2);
        else if ((t == 0x02 || t == 0x03) && uuid == 0xFF)
            for (int k = 0; k + 1 < n && uuid == 0xFF; k += 2) uuid = by_uuid16((uint16_t)(d[k] | d[k + 1] << 8));
        else if (t == 0x16 && n >= 2 && uuid == 0xFF) uuid = by_uuid16((uint16_t)(d[0] | d[1] << 8));
        else if (t == 0x19 && n >= 2) app = by_appearance((uint16_t)(d[0] | d[1] << 8));
        i += 1 + l;
    }
    if (app != 0xFF) return app;                        // a device that says what it is
    if (uuid != 0xFF) return uuid;
    if (mfg != 0xFF) return mfg;
    return adv_type == 3 ? CAT_BEACON : CAT_OTHER;      // not connectable and unknown: probably a beacon
}

uint32_t adv_hash(const uint8_t addr[6], uint8_t addr_type, uint32_t salt)
{
    uint32_t x = 2166136261u ^ salt;                    // FNV-1a
    for (int i = 0; i < 6; i++) { x ^= addr[i]; x *= 16777619u; }
    x ^= addr_type; x *= 16777619u;
    x ^= x >> 15; x *= 0x2c1b3c6du; x ^= x >> 12;      // spread it: the bits pick pitch, wire and slot
    return x ? x : 1;
}
