// The host's stand-in for the radio (inst_ble.c on the device): a crowd of devices that advertise with
// real payloads (run through the firmware's own parser), each at its own interval with 0-10 ms of jitter,
// with packets lost and signal strengths that wander. Tests add, move and remove devices.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool     on;
    uint8_t  addr[6], kind;    // kind: the category it should be heard as (CAT_*)
    int      payload;          // which payload it sends (see sim_ble.c)
    float    interval, rssi, next;
    uint32_t who;              // what the firmware will call it
    int      sent, lost;
} sim_dev_t;

#define SIM_DEVS 40
extern sim_dev_t sim_dev[SIM_DEVS];
extern float sim_loss;         // share of adverts lost (0.12)
extern float sim_rssi_noise;   // dB (3)
extern bool  sim_ble_fail;     // the next ble_start() fails
extern int   sim_ble_share;    // BT TIME, as ETHER set it

void sim_crowd_clear(void);
void sim_crowd_default(void);  // a room: phones, earbuds, a watch, trackers, beacons, a laptop, a speaker
int  sim_crowd_add(int payload, float rssi, float interval);   // returns its index
int  sim_payload_count(void);
int  sim_payload_kind(int payload);                            // the category its payload should parse as
const char *sim_payload_name(int payload);
int  sim_payload_parse(int payload);                            // what the firmware's parser makes of it
