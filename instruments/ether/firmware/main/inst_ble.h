// ETHER's Bluetooth side, from CHIRP: what the radio hands over for each advertisement, and the parser.
// inst_ble.c on the device (NimBLE observer), tools/sim/sim_ble.c on the host.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "core.h"

// A coarse guess at what sent an advertisement, from its manufacturer, services and appearance.
enum { CAT_PHONE, CAT_EARBUDS, CAT_WATCH, CAT_TRACKER, CAT_BEACON, CAT_LAPTOP, CAT_SPEAKER, CAT_OTHER, CAT_N };
extern const char *const cat_name[CAT_N];

typedef struct {
    uint32_t who;              // salted hash of the address (a new salt every boot); never 0
    int8_t   rssi;             // dBm
    uint8_t  cat;              // CAT_*
    uint8_t  adv_type;         // 0 connectable, 1 directed, 2 scannable, 3 not connectable, 4 scan response
    uint8_t  len;              // payload bytes
} ble_adv_t;

typedef void (*ble_cb_t)(const ble_adv_t *a);   // runs in the radio's task: push it somewhere and return
bool ble_start(ble_cb_t cb);   // UI task, at init; false if Bluetooth would not start
void ble_tick(void);           // UI task, every tick (the host stand-in makes its crowd here; a no-op on the device)
bool ble_running(void);
void ble_set_share(int pct);   // the share of the air time the scan listens (25, 50, 75 %); Wi-Fi gets the rest

// inst_adv.c: the category from the raw advertising data (AD structures); the salted hash
uint8_t  adv_category(const uint8_t *data, int len, uint8_t adv_type);
uint32_t adv_hash(const uint8_t addr[6], uint8_t addr_type, uint32_t salt);
