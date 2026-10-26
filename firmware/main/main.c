// Samplooler: hold BOOT to record the mic, let go and it loops.
// Tilt nose down to play forward faster, nose up to play backwards.
// Roll left for a delay whose tail rings out, roll right to stutter the loop.
// Builds for the ESP32-S3 SuperMini (float, PSRAM, 0.96" OLED, record button, encoder).
//
// Controls:
//   record button   hold to record
//   encoder tap     play: open the menu (after 0.3 s, so a second tap can still come); menu: dive into the
//                   setting (edit), or run Save/Load; editing: back to the list
//   encoder 2 taps  play: the knob sets the Volume, until it rests 3 s or is tapped (as in every instrument)
//   encoder turn    play: adjust the last setting (a toast shows it); menu: choose; editing: adjust
//   encoder hold    leave the menu (0.5 s)
// With HAS_JOY (a kit with the joystick): Y sets the speed when tilt is off, X the pitch in GRAIN.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_task_wdt.h"
#include "pins.h"
#include "audio.h"
#include "recorder.h"
#include "imu.h"
#include "oled.h"
#include "ui.h"
#include "usb.h"
#include "slots.h"
#include "fx.h"
#include "diag.h"
#include "voice.h"
#include "flock.h"
#include "settings.h"
#include "app_line.h"
#include "tune.h"
#include "motion.h"
#include "gesture.h"
#include "rig.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#if HAS_CONTROLS
#include "input.h"
#endif

static const char *TAG = "main";

// The motion's numbers (axes, fusion, dead zones, full angles, signs, the curve, detents) are the tuning table's
// (tune.h); motion.c turns the IMU into angles, gesture.c the angles into the speed and the effects.

// ---- settings, editable from the encoder (settings_t is in settings.h; NVS keeps a copy) ----
static void apply_settings(void);
#define SETTINGS_DEFAULTS { .mic_db = 12, .mode = REC_MODE_TAPE, .pitch_st = 0, .tilt = true, \
                            .spd_max = 2.0f, .spd_min = 0.05f, .snap = false, .grain_ms = 50, .vol_pct = 100, \
                            .input = AUDIO_IN_MIC, .slot = 0, .speaker = true, .voice = 0, .key = 0, .scale = 0, .monitor = false, \
                            .rec_button = true, .link = true, .group = 1, .range = 1 }
static settings_t S = SETTINGS_DEFAULTS;
static const settings_t S_DEFAULTS = SETTINGS_DEFAULTS;

// The settings before P_SAVE are on MIDI (CC 20 + index); the rest are the device's own. A new one also gets a
// line in app_line.c's INFO: its group in the desktop app and a line of help (without one it lands under SETTINGS).
enum { P_MIC, P_MODE, P_PITCH, P_TILT, P_SPDMAX, P_SPDMIN, P_SNAP, P_GRAIN, P_VOL, P_INPUT, P_VOICE, P_KEY, P_SCALE, P_MONITOR,
       P_SLOT, P_SAVE, P_LOAD, P_SPEAKER, P_RECBTN, P_LINK, P_GROUP, P_RANGE, P_PEERS, P_N };
static const char *param_name[P_N] = { "Mic gain", "Mode", "Pitch", "Tilt", "Speed max", "Speed min", "Snap", "Grain", "Volume", "Input",
                                       "Voice", "Key", "Scale", "Monitor", "Slot", "Save", "Load", "Speaker", "Rec button",
                                       "Link", "Group", "Range", "Who is there" };
static bool is_action(int p) { return p == P_SAVE || p == P_LOAD || p == P_PEERS; }
static const char *const range_names[3] = { "NEAR", "ROOM", "FAR" };
static void slot_save(void);
static void slot_load(void);
static void who_is_there(void);
static void midi_send_setting(int p);
static void setting_changed(void) { settings_changed(); }
static void link_apply(void) { flock_enable(S.link, S.group, S.range); }

#if HAS_CONTROLS
static void param_adjust(int p, int d)
{
    switch (p) {
    case P_MIC:    S.mic_db += d; if (S.mic_db < 0) S.mic_db = 0; if (S.mic_db > 36) S.mic_db = 36; break;
    case P_MODE:   S.mode = d > 0 ? REC_MODE_GRAIN : REC_MODE_TAPE; break;
    case P_PITCH:  S.pitch_st += d; if (S.pitch_st < -24) S.pitch_st = -24; if (S.pitch_st > 24) S.pitch_st = 24; break;
    case P_TILT:   S.tilt = d > 0; break;
    case P_SPDMAX: S.spd_max += 0.25f * d; if (S.spd_max < 0.25f) S.spd_max = 0.25f; if (S.spd_max > REC_SPEED_LIMIT) S.spd_max = REC_SPEED_LIMIT; break;
    case P_SPDMIN: S.spd_min += 0.05f * d; if (S.spd_min < 0) S.spd_min = 0; if (S.spd_min > 1) S.spd_min = 1; break;
    case P_SNAP:   S.snap = d > 0; break;
    case P_GRAIN:  S.grain_ms += 10 * d; if (S.grain_ms < 20) S.grain_ms = 20; if (S.grain_ms > 200) S.grain_ms = 200; break;
    case P_VOL:    S.vol_pct += 5 * d; if (S.vol_pct < 0) S.vol_pct = 0; if (S.vol_pct > 150) S.vol_pct = 150; break;
    case P_INPUT:  S.input = d > 0 ? AUDIO_IN_USB : AUDIO_IN_MIC; break;
    case P_VOICE:  S.voice = ((S.voice + d) % VOICE_N + VOICE_N) % VOICE_N; break;
    case P_KEY:    S.key = ((S.key + d) % VOICE_KEYS + VOICE_KEYS) % VOICE_KEYS; flock_harm_set(S.key, S.scale); break;
    case P_SCALE:  S.scale += d; if (S.scale < 0) S.scale = 0; if (S.scale >= VOICE_SCALES) S.scale = VOICE_SCALES - 1; flock_harm_set(S.key, S.scale); break;
    case P_MONITOR: S.monitor = d > 0; break;
    case P_SLOT:   S.slot += d; if (S.slot < 0) S.slot = 0; if (S.slot >= SLOT_N) S.slot = SLOT_N - 1; break;
    case P_SAVE:   if (d > 0) slot_save(); return;
    case P_LOAD:   if (d > 0) slot_load(); return;
    case P_SPEAKER: S.speaker = d > 0; audio_speaker(S.speaker); break;
    case P_RECBTN: S.rec_button = d > 0; break;
    case P_LINK:   S.link = d > 0; link_apply(); break;
    case P_GROUP:  S.group += d; if (S.group < 1) S.group = 1; if (S.group > 8) S.group = 8; link_apply(); break;
    case P_RANGE:  S.range += d; if (S.range < 0) S.range = 0; if (S.range > 2) S.range = 2; link_apply(); break;
    case P_PEERS:  if (d > 0) who_is_there(); return;
    }
    setting_changed();
    midi_send_setting(p);
}
#endif

static void param_text(int p, char *b, size_t n)
{
    switch (p) {
    case P_MIC:    snprintf(b, n, "%d dB", S.mic_db); break;
    case P_MODE:   snprintf(b, n, "%s", S.mode == REC_MODE_GRAIN ? "GRAIN" : "TAPE"); break;
    case P_PITCH:  snprintf(b, n, S.mode == REC_MODE_GRAIN ? "%+d st" : "= speed", S.pitch_st); break;
    case P_TILT:   snprintf(b, n, "%s", S.tilt ? "ON" : "OFF (joy Y)"); break;
    case P_SPDMAX: snprintf(b, n, "%.2fx", (double)S.spd_max); break;
    case P_SPDMIN: snprintf(b, n, "%.2fx", (double)S.spd_min); break;
    case P_SNAP:   snprintf(b, n, "%s", S.snap ? "ON" : "OFF"); break;
    case P_GRAIN:  snprintf(b, n, "%d ms", S.grain_ms); break;
    case P_VOL:    snprintf(b, n, "%d%%", S.vol_pct); break;
    case P_INPUT:  snprintf(b, n, "%s", S.input == AUDIO_IN_USB ? "USB" : "MIC"); break;
    case P_VOICE:  snprintf(b, n, "%s", vox_name(S.voice)); break;
    case P_KEY:    snprintf(b, n, "%s", vox_key_name(S.key)); break;
    case P_SCALE:  snprintf(b, n, "%s", vox_scale_name(S.scale)); break;
    case P_MONITOR: snprintf(b, n, "%s", S.monitor ? "ALWAYS" : "WHILE REC"); break;
    case P_SLOT:   { int l = slots_length(S.slot); if (l) snprintf(b, n, "%d %.1fs", S.slot + 1, (double)l / REC_SAMPLE_RATE); else snprintf(b, n, "%d empty", S.slot + 1); } break;
    case P_SAVE:   snprintf(b, n, "-> slot %d", S.slot + 1); break;
    case P_LOAD:   snprintf(b, n, "<- slot %d", S.slot + 1); break;
    case P_SPEAKER: snprintf(b, n, "%s", S.speaker ? "ON" : "OFF (line out)"); break;
    case P_RECBTN: snprintf(b, n, "%s", S.rec_button ? "ON" : "OFF (BOOT)"); break;
    case P_LINK:   snprintf(b, n, "%s", S.link ? "ON" : "OFF"); break;
    case P_GROUP:  snprintf(b, n, "%d", S.group); break;
    case P_RANGE:  snprintf(b, n, "%s", range_names[S.range < 0 ? 0 : S.range > 2 ? 2 : S.range]); break;
    case P_PEERS:  { int np = flock_peers(); if (!S.link) snprintf(b, n, "link off"); else if (np) snprintf(b, n, "%d board%s", np, np == 1 ? "" : "s"); else snprintf(b, n, "alone"); } break;
    }
}

// Slot jobs. The output fades out first (a flash erase stalls both cores; muted, that is silence rather
// than a stutter), the file is moved in chunks from the 10 ms loop so the screen and MIDI stay alive,
// then the output fades back in. Recording is ignored while a job runs: both directions touch the take.
static enum { JOB_NONE, JOB_SAVE, JOB_LOAD } job;
static int  job_wait;                       // ticks spent waiting for silence
static char job_text[32], result_text[40];
static int  result_ms;
static void slot_save(void)
{
    if (job != JOB_NONE || slots_busy()) return;
    int n = 0;
    if (!recorder_take(&n) || n <= 0) { ESP_LOGW(TAG, "nothing to save"); snprintf(result_text, sizeof result_text, "NOTHING TO SAVE"); result_ms = 1500; return; }
    job = JOB_SAVE; job_wait = 0; snprintf(job_text, sizeof job_text, "SAVING %d", S.slot + 1);
    audio_set_mute(true);
}
static void slot_load(void)
{
    if (job != JOB_NONE || slots_busy() || recorder_state() == REC_RECORDING) return;
    if (!slots_length(S.slot)) { snprintf(result_text, sizeof result_text, "SLOT %d EMPTY", S.slot + 1); result_ms = 1500; return; }
    job = JOB_LOAD; job_wait = 0; snprintf(job_text, sizeof job_text, "LOADING %d", S.slot + 1);
    audio_set_mute(true);
}
static void who_is_there(void) { flock_who(result_text, sizeof result_text); result_ms = 2500; }
static void job_poll(void)
{
    if (job == JOB_NONE) return;
    if (!slots_busy()) {
        if (!audio_silent() && ++job_wait < 20) return;              // the fade takes 20 ms; 200 ms at most
        bool started = false;
        if (job == JOB_SAVE) { int n = 0; const int16_t *t = recorder_take(&n); started = slots_save_begin(S.slot, t, n); }
        else { int max = 0; int16_t *b = recorder_load_begin(&max); if (b) flock_loop_gone(); started = b && slots_load_begin(S.slot, b, max); }
        if (!started) { snprintf(result_text, sizeof result_text, "%s FAILED", job == JOB_SAVE ? "SAVE" : "LOAD"); result_ms = 2000; job = JOB_NONE; audio_set_mute(false); }
        return;
    }
    if (slots_step()) return;
    bool ok = slots_result_ok();
    if (job == JOB_LOAD) {                                           // with boards around: whole beats of their tempo, on the beat
        int n = ok ? slots_loaded() : 0, le = flock_snap_len(n);
        recorder_load_end(n, le, le > 0);
    }
    snprintf(result_text, sizeof result_text, "%s %d%s", job == JOB_SAVE ? "SAVED" : "LOADED", S.slot + 1, ok ? "" : " FAILED");
    result_ms = 1500; job = JOB_NONE;
    audio_set_mute(false);
}

// Before a reboot: the settings that were still waiting, and no click on the way out.
void app_prepare_reboot(void)
{
    settings_flush(&S);
    audio_set_mute(true);
    vTaskDelay(pdMS_TO_TICKS(40));
}

// ---- MIDI map ----
// out: CC1 pitch angle, CC2 roll, CC3 speed, CC4 loop position, CC5 joy X, CC6 joy Y, CC7 input level;
//      note 60 record, note 62 playing. in: CC 20 + setting index (scaled 0..127), note 60 = record hold,
//      note 61 = load slot, note 63 = save slot.
static int setting_to_127(int p)
{
    switch (p) {
    case P_MIC:    return S.mic_db * 127 / 36;
    case P_MODE:   return S.mode == REC_MODE_GRAIN ? 127 : 0;
    case P_PITCH:  return 64 + S.pitch_st * 2;
    case P_TILT:   return S.tilt ? 127 : 0;
    case P_SPDMAX: return (int)((S.spd_max - 0.25f) / (REC_SPEED_LIMIT - 0.25f) * 127.0f + 0.5f);
    case P_SPDMIN: return (int)(S.spd_min * 127.0f);
    case P_SNAP:   return S.snap ? 127 : 0;
    case P_GRAIN:  return (S.grain_ms - 20) * 127 / 180;
    case P_VOL:    return S.vol_pct * 127 / 150;
    case P_INPUT:  return S.input == AUDIO_IN_USB ? 127 : 0;
    case P_VOICE:  return S.voice * 127 / (VOICE_N - 1);
    case P_KEY:    return S.key * 127 / (VOICE_KEYS - 1);
    case P_SCALE:  return S.scale * 127 / (VOICE_SCALES - 1);
    case P_MONITOR: return S.monitor ? 127 : 0;
    case P_SLOT:   return S.slot * 16;
    case P_SPEAKER: return S.speaker ? 127 : 0;          // these are not on MIDI: the slider on the screen only
    case P_RECBTN: return S.rec_button ? 127 : 0;
    case P_LINK:   return S.link ? 127 : 0;
    case P_GROUP:  return (S.group - 1) * 127 / 7;
    case P_RANGE:  return S.range * 127 / 2;
    default:       return 0;
    }
}
static void setting_from_127(int p, int v)
{
    switch (p) {
    case P_MIC:    S.mic_db = v * 36 / 127; break;
    case P_MODE:   S.mode = v >= 64 ? REC_MODE_GRAIN : REC_MODE_TAPE; break;
    case P_PITCH:  S.pitch_st = (v - 64) / 2; if (S.pitch_st < -24) S.pitch_st = -24; if (S.pitch_st > 24) S.pitch_st = 24; break;
    case P_TILT:   S.tilt = v >= 64; break;
    case P_SPDMAX: S.spd_max = 0.25f + (REC_SPEED_LIMIT - 0.25f) * v / 127.0f; break;
    case P_SPDMIN: S.spd_min = v / 127.0f; break;
    case P_SNAP:   S.snap = v >= 64; break;
    case P_GRAIN:  S.grain_ms = 20 + v * 180 / 127; break;
    case P_VOL:    S.vol_pct = v * 150 / 127; break;
    case P_INPUT:  S.input = v >= 64 ? AUDIO_IN_USB : AUDIO_IN_MIC; break;
    case P_VOICE:  S.voice = (v * (VOICE_N - 1) + 63) / 127; break;
    case P_KEY:    S.key = (v * (VOICE_KEYS - 1) + 63) / 127; flock_harm_set(S.key, S.scale); break;
    case P_SCALE:  S.scale = (v * (VOICE_SCALES - 1) + 63) / 127; flock_harm_set(S.key, S.scale); break;
    case P_MONITOR: S.monitor = v >= 64; break;
    case P_SLOT:   S.slot = v / 16; if (S.slot >= SLOT_N) S.slot = SLOT_N - 1; break;
    default: break;
    }
}
static void midi_send_setting(int p) { if (p < P_SAVE) usb_midi_send_cc(20 + p, setting_to_127(p)); }
#if !HAS_CONTROLS
static inline void midi_send_setting_unused_guard(void) { (void)midi_send_setting; }
#endif
static volatile bool midi_rec_hold;
static volatile bool midi_freeze_req;              // note 64: toggle the tilt latch, picked up by the loop
static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if (st == 0xB0 && d1 >= 20 && d1 < 20 + P_SAVE) { setting_from_127(d1 - 20, d2); apply_settings(); setting_changed(); }
    else if (st == 0xC0) { S.voice = d1 % VOICE_N; apply_settings(); setting_changed(); midi_send_setting(P_VOICE); }   // program change: the voice
    else if (st == 0x90 && d2 > 0) {
        if (d1 == 60) midi_rec_hold = true;
        else if (d1 == 64) midi_freeze_req = true;
        else if (d1 == 61) slot_load();
        else if (d1 == 63) slot_save();
    } else if (st == 0x80 || (st == 0x90 && d2 == 0)) {
        if (d1 == 60) midi_rec_hold = false;
    }
}

static int   clampi(int v, int lo, int hi)       { return v < lo ? lo : v > hi ? hi : v; }
static float clampf(float v, float lo, float hi) { return !(v >= lo) ? lo : v > hi ? hi : v; }   // NaN lands on lo
static void clamp_settings(void)
{
    S.mic_db   = clampi(S.mic_db, 0, 36);
    S.mode     = S.mode == REC_MODE_GRAIN ? REC_MODE_GRAIN : REC_MODE_TAPE;
    S.pitch_st = clampi(S.pitch_st, -24, 24);
    S.spd_max  = clampf(S.spd_max, 0.25f, REC_SPEED_LIMIT);
    S.spd_min  = clampf(S.spd_min, 0.0f, 1.0f);
    S.grain_ms = clampi(S.grain_ms, 20, 200);
    S.vol_pct  = clampi(S.vol_pct, 0, 150);
    S.input    = S.input == AUDIO_IN_USB ? AUDIO_IN_USB : AUDIO_IN_MIC;
    S.slot     = clampi(S.slot, 0, SLOT_N - 1);
    S.voice    = clampi(S.voice, 0, VOICE_N - 1);
    S.key      = clampi(S.key, 0, VOICE_KEYS - 1);
    S.scale    = clampi(S.scale, 0, VOICE_SCALES - 1);
    S.group    = clampi(S.group, 1, 8);
    S.range    = clampi(S.range, 0, 2);
}
static void apply_settings(void)
{
    audio_set_mic_gain_db(S.mic_db);
    recorder_set_mode(S.mode);
    recorder_set_grain_ms(S.grain_ms);
    recorder_set_volume(S.vol_pct / 100.0f);
    audio_set_input(S.input);
    audio_speaker(S.speaker);
    audio_set_monitor(S.monitor);
    vox_select(S.voice, S.key, S.scale);
}

// The tuning table moved: hand its numbers to what keeps a copy (the IMU's registers, the mic's filter, the tape's
// glide, the encoder's glitch filter). Only when something changed.
static bool imu_ok;
static void apply_tune(void)
{
    static uint32_t seen;
    static int imu_have[4] = { -1, -1, -1, -1 };
    if (tune_gen() == seen) return;
    seen = tune_gen();
    int want[4] = { TI(TU_IMU_ARANGE), TI(TU_IMU_GRANGE), TI(TU_IMU_DLPF), TI(TU_IMU_RATE) };
    if (imu_ok && memcmp(want, imu_have, sizeof want)) { imu_config(want[0], want[1], want[2], want[3]); memcpy(imu_have, want, sizeof want); }
    audio_set_mic_tune(TV(TU_MI_HPF), TI(TU_MI_SLOT), TV(TU_MI_TRIM), TI(TU_US_OUT));
    recorder_set_slew_ms(TV(TU_TI_SLEW));
#if HAS_CONTROLS
    input_tune();
#endif
}

static void tune_action(int id)
{
    if (id == TU_IMU_BIAS_KEEP) motion_bias_keep();
    else if (id == TU_MO_YAW_RESET) motion_yaw_reset();
#if HAS_CONTROLS
    else if (id == TU_JO_CENTRE) input_joy_centre();
#endif
}

// The rig's line first (rig.c: streams, tuning, injection), then the app's (settings).
static void sysex_in(uint8_t cmd, const uint8_t *b, int n) { if (!rig_sysex(cmd, b, n)) app_line_sysex(cmd, b, n); }
bool     rig_port_send(const uint8_t *m, int n) { return usb_midi_send_sysex(m, n); }
uint32_t rig_port_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void     oled_on_flush(const uint8_t *fb) { rig_screen(fb); }   // oled.c, after every frame: the rig mirrors the screen

static void i2c_recover(void)
{
    gpio_config_t io = { .pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
                         .mode = GPIO_MODE_INPUT_OUTPUT_OD, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);
    gpio_set_level(PIN_I2C_SDA, 1); gpio_set_level(PIN_I2C_SCL, 1);
    esp_rom_delay_us(20);
    for (int i = 0; i < 9 && gpio_get_level(PIN_I2C_SDA) == 0; i++) {
        gpio_set_level(PIN_I2C_SCL, 0); esp_rom_delay_us(10);
        gpio_set_level(PIN_I2C_SCL, 1); esp_rom_delay_us(10);
    }
    gpio_set_level(PIN_I2C_SDA, 0); esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SCL, 1); esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SDA, 1); esp_rom_delay_us(10);
    gpio_reset_pin(PIN_I2C_SDA); gpio_reset_pin(PIN_I2C_SCL);
}

static i2c_master_bus_handle_t i2c_start(void)
{
    i2c_recover();
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT, .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    return bus;
}

static const char *state_name(int s)
{
    return s == REC_RECORDING ? "REC" : s == REC_PLAYING ? "PLAY" : "IDLE";
}

// The app's line (app_line.c): the settings as the menu has them, stepped as the encoder steps them.
static const char *app_name(int p) { return param_name[p]; }
static void app_text(int p, char *b, int n) { param_text(p, b, (size_t)n); }
#if HAS_CONTROLS
static void app_step(int p, int d) { param_adjust(p, d); apply_settings(); }
#else
static void app_step(int p, int d) { (void)p; (void)d; }
#endif
static const app_settings_t APP_SETTINGS = { P_N, app_name, app_text, app_step, is_action };

static float meter_in, meter_out;                    // decaying peak meters for the screen and MIDI
static int   clip_hold;
#if OLED_ENABLE
static float param_frac(int p) { return is_action(p) ? -1.0f : setting_to_127(p) / 127.0f; }
#endif

void app_main(void)
{
    diag_init();
#if PIN_AMP_SD >= 0
    // The speaker amp stays off until the audio task has seen 50 ms of good clock (audio_amp_enable),
    // so the I2S start-up and the DAC's wake-up never reach the speaker.
    gpio_config_t amp = { .pin_bit_mask = 1ULL << PIN_AMP_SD, .mode = GPIO_MODE_OUTPUT, .pull_down_en = GPIO_PULLDOWN_ENABLE };
    gpio_config(&amp);
    gpio_set_level(PIN_AMP_SD, 0);
#endif
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "samplooler on %s: hold BOOT to record, tilt to play", CONFIG_IDF_TARGET);

    gpio_config_t btn = { .pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&btn);

    tune_load();                            // the tuning table (NVS "tune"): the controls' numbers, before the controls start
    tune_on_action(tune_action);
    gest_on_event(rig_event);
    rig_init();
    motion_init();
    i2c_master_bus_handle_t bus = i2c_start();
    bool has_imu = imu_init(bus);
    imu_ok = has_imu;
    apply_tune();
    fx_init();
#if OLED_ENABLE
    oled_init(bus);
#endif
#if HAS_CONTROLS
    input_init();
#endif
    audio_i2s_init();
    vox_init();                             // the voice's engines (SING's): idle until a Voice is chosen
    if (!recorder_init()) {
        ESP_LOGE(TAG, "halt");
#if OLED_ENABLE
        if (oled_present()) ui_fault("NO PSRAM", "for the take");
#endif
        while (1) vTaskDelay(1000);
    }
    settings_init(&S);
    clamp_settings();
    if (diag_safe_mode()) {                 // three crashes in a row: start from the defaults and keep them
        ESP_LOGW(TAG, "safe mode: %d crashes in a row, settings reset to defaults", diag_crash_streak());
        S = S_DEFAULTS; setting_changed();
        tune_defaults();                    // the tuning too (not saved: it comes back if the crashes were not its doing)
    }
    bool rec_stuck = false;                 // REC down since power-on: a stuck button or a short on its pin
#if HAS_CONTROLS && PIN_REC >= 0
    // Factory reset: the record button held from power-on, kept down for 3 s, then let go. Letting go earlier
    // does nothing. A REC still down after 6 s is not a hand (a stuck button, a shorted pin): no reset, and REC is
    // ignored until it has been up for 0.1 s; BOOT records meanwhile. Not with Rec button OFF. The slots stay.
    if (S.rec_button && gpio_get_level(PIN_REC) == 0) {
        int held_ms = 0;
        const int reset_ms = TI(TU_RE_RESET), stuck_ms = TI(TU_RE_STUCK) - TI(TU_RE_RESET);
        for (; held_ms < reset_ms && gpio_get_level(PIN_REC) == 0; held_ms += 100) {
            char l2[32]; snprintf(l2, sizeof l2, "keep holding REC  %d", (reset_ms - held_ms + 999) / 1000);
#if OLED_ENABLE
            if (oled_present()) ui_fault("RESET?", l2);
#endif
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        int wait_ms = 0;
        for (; held_ms >= reset_ms && wait_ms < stuck_ms && gpio_get_level(PIN_REC) == 0; wait_ms += 100) {
#if OLED_ENABLE
            if (oled_present()) ui_fault("RESET?", "let go of REC to reset");
#endif
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (wait_ms >= stuck_ms) {
            ESP_LOGW(TAG, "REC (GPIO %d) stays down from power-on: no reset; REC is ignored until it lets go, BOOT records", PIN_REC);
#if OLED_ENABLE
            if (oled_present()) ui_fault("REC IS STUCK", "BOOT records for now");
#endif
            vTaskDelay(pdMS_TO_TICKS(1500));
            rec_stuck = true;
        } else if (held_ms >= reset_ms) {
            ESP_LOGW(TAG, "factory reset: record button held %d ms at boot", reset_ms);
            settings_erase(); S = S_DEFAULTS;
            tune_erase(); tune_defaults(); apply_tune();
#if OLED_ENABLE
            if (oled_present()) ui_fault("RESET", "defaults restored");
#endif
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }
#endif
    slots_init();
    apply_settings();
    audio_run();
    flock_init(S.key, S.scale);             // the flock (flock.h): the radio comes up after USB, below
    bool keep_console = false;
#if HAS_CONTROLS
    keep_console = gpio_get_level(PIN_ENC_SW) == 0;      // encoder held at power-on: stay on the Serial/JTAG console
#endif
    if (keep_console) ESP_LOGW(TAG, "encoder held at boot: USB MIDI/audio off, console kept");
    else {
        vTaskDelay(pdMS_TO_TICKS(200)); ESP_LOGI(TAG, "switching the USB port to MIDI + audio + CDC console"); usb_start();
        app_line_init(&APP_SETTINGS); usb_midi_on_sysex(sysex_in);
    }
    link_apply();

    int  held_n = 0, last_state = -1, last_slot = -1, tick = 0, param = 0, toast = 0, idle_ms = 0;
    uint32_t loop_seen = 0xFFFFFFFFu;                     // the loop whose start the flock has been told of
    audio_stats_t last_stats = { 0 };
    bool held = false, menu = false, menu_edit = false;
    int  click_ms = 0, vol_ms = 0;                         // a tap in play waiting for a second; the knob on Volume
    bool click_menu = false;                               // no second tap: the menu opens (not after a tap that ended Volume)
    bool frozen = false;                                  // tilt latched: the angles stop following the sensor
    float roll = 0, pitch = 0, speed_cmd = 1.0f, pitch_ratio = 1.0f, joy_pitch_st = 0;   // pitch + with the nose down (motion.h)
    tilt_out_t to = { .speed = 1.0f };
    bool injected = false, in_has_joy = false;
    float in_jx = 0, in_jy = 0;
    int  enc_total = 0, in_raw_x = 0, in_raw_y = 0;
#if HAS_CONTROLS
    input_ev_t in = { 0 };
#endif

    // The task watchdog (5 s, CONFIG_ESP_TASK_WDT_TIMEOUT_S) now watches this loop too: a hang in the UI,
    // a slot job or a driver call resets the device instead of leaving it playing with dead controls.
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    int64_t loop_us = 0;                                  // the last pass's work, for the rig (I2C, the screen, the streams)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        int64_t t_pass = esp_timer_get_time();
        tick++;
        esp_task_wdt_reset();
        if (tick == 1000) diag_mark_alive();                      // 10 s up: this boot counts as a good one

        usb_midi_poll(midi_in);
        app_line_tick(10);
        job_poll();
        settings_poll(&S);
        vox_tick(recorder_state() == REC_PLAYING ? recorder_loop_seconds() : 0.0f);
        flock_tick(0.01f, S.snap);
        {   // the flock: a key or scale from another board; a board that came or went
            int k, sc;
            if (flock_harm_news(&k, &sc) && (k != S.key || sc != S.scale)) {
                S.key = k; S.scale = clampi(sc, 0, VOICE_SCALES - 1);
                apply_settings(); setting_changed(); midi_send_setting(P_KEY); midi_send_setting(P_SCALE);
            }
            const char *nw = flock_news();
            if (nw) { snprintf(result_text, sizeof result_text, "%s", nw); result_ms = 1500; }
        }
        apply_tune();
        rig_tick(10);
        {   // the motion: every sample from the sensor's FIFO (or what the rig injects) into motion.c and out to the rig
            static int16_t raw[128][6], inj[128][6];
            bool ovf = false;
            int n = has_imu ? imu_read(raw, 128, &ovf) : 0, flags = (ovf ? 2 : 0) | (has_imu ? 4 : 0);
            const int16_t (*src)[6] = raw;
            int ni = rig_inject(inj, 128);
            injected = ni >= 0;
            if (injected) { src = (const int16_t (*)[6])inj; n = ni; flags |= 1 | 4; }
            float al = imu_acc_lsb(), gl = imu_gyro_lsb(), dt = 1.0f / (float)imu_rate_hz();
            for (int i = 0; i < n; i++) {
                float a[3] = { src[i][0] / al, src[i][1] / al, src[i][2] / al };
                float g[3] = { src[i][3] / gl, src[i][4] / gl, src[i][5] / gl };
                motion_sample(a, g, dt);
            }
            uint32_t f0 = audio_frames() - (uint32_t)(n * (REC_SAMPLE_RATE / imu_rate_hz()));
            rig_motion(src, n, f0, TI(TU_IMU_ARANGE) | TI(TU_IMU_GRANGE) << 2, flags);
        }
        bool down = gpio_get_level(PIN_BOOT) == 0 || midi_rec_hold;
#if HAS_CONTROLS
        input_poll(&in);
        in_has_joy = in.has_joy; in_jx = in.jx; in_jy = in.jy; in_raw_x = in.raw_x; in_raw_y = in.raw_y;
        enc_total += in.enc;
        if (rec_stuck) {                                     // REC down since power-on counts for nothing until it has been up 0.1 s
            static int rec_up_ms;
            rec_up_ms = in.rec ? 0 : rec_up_ms + 10;
            if (rec_up_ms >= 100) { rec_stuck = false; ESP_LOGI(TAG, "REC let go: it records again"); }
        }
        down = down || (in.rec && S.rec_button && !rec_stuck) || in.rec_pc;
#endif
        if (job != JOB_NONE) down = false;                    // a slot job owns the take
        idle_ms += 10;
        if (down) idle_ms = 0;
#if HAS_CONTROLS
        if (in.enc || in.tap || in.hold || in.pressed || in.rec) idle_ms = 0;
#endif
        const int deb = TI(TU_RE_DEB);                        // REC must agree this many ticks in a row
        held_n = down ? (held_n < deb ? held_n + 1 : deb) : (held_n > 0 ? held_n - 1 : 0);
        bool now = held_n >= deb ? true : held_n == 0 ? false : held;
        if (now != held) { held = now; recorder_set_record(held); gest_emit(held ? "rec.down" : "rec.up", 0, 0, ""); }

#if HAS_CONTROLS
        // the encoder: tap opens the menu, dives into a setting, runs an action or climbs back out;
        // hold leaves the menu; turn chooses in the list, adjusts while editing or in play mode
        if (in.tap) {
            if (!menu) {                                      // one tap opens the menu after 0.3 s; two put the knob on Volume
                if (click_ms > 0) { click_ms = 0; vol_ms = TI(TU_EN_VOL_MS); toast = vol_ms; ESP_LOGI(TAG, "the knob: volume"); gest_emit("enc.double", 0, 0, "volume"); }
                else { click_ms = TI(TU_EN_DOUBLE) > 10 ? TI(TU_EN_DOUBLE) : 10; click_menu = vol_ms <= 0; if (vol_ms > 0) { vol_ms = 0; toast = 0; } }
            }
            else if (menu_edit) menu_edit = false;
            else if (is_action(param)) param_adjust(param, +1);
            else menu_edit = true;
        }
        if (in.hold && menu) { menu = false; menu_edit = false; ESP_LOGI(TAG, "play"); gest_emit("menu.close", 0, 0, ""); }
        else if (in.hold) midi_freeze_req = true;             // held while playing: freeze or release the tilt
        if (click_ms > 0 && (click_ms -= 10) <= 0) {
            click_ms = 0;
            if (click_menu && !menu) { menu = true; menu_edit = false; ESP_LOGI(TAG, "menu"); gest_emit("menu.open", 0, 0, ""); }
        }
        if (vol_ms > 0) vol_ms -= 10;
        if (in.enc && !menu && vol_ms > 0) {
            param_adjust(P_VOL, in.enc); apply_settings(); vol_ms = TI(TU_EN_VOL_MS); toast = vol_ms;
            ESP_LOGI(TAG, "volume %d%%", S.vol_pct);
        } else if (in.enc) {
            if (menu && !menu_edit) param = ((param + in.enc) % P_N + P_N) % P_N;
            else if (!menu && is_action(param)) { }           // an action runs from the menu only, never from a turn in play
            else { param_adjust(param, in.enc); apply_settings(); if (!menu) toast = 1500; }
            char v[24]; param_text(param, v, sizeof v); ESP_LOGI(TAG, "%d/%d %s %s", param + 1, P_N, param_name[param], v);
        }
#endif
        joy_pitch_st = in_has_joy && !menu ? TV(TU_JO_PITCH) * in_jx : 0.0f;

        if (midi_freeze_req) {
            midi_freeze_req = false; frozen = !frozen; toast = 0;
            gest_emit(frozen ? "freeze.on" : "freeze.off", 0, 0, "");
            ESP_LOGI(TAG, "tilt %s at pitch %+.1f roll %+.1f", frozen ? "frozen" : "released", (double)pitch, (double)roll);
        }
        if (TI(TU_MO_RATE) == 1 || tick % 2 == 0) {          // the control rate: 50 Hz (v1) or 100
            bool imu = has_imu || injected;
            motion_tick(frozen || !imu);                      // frozen: speed, direction and effects stay where they were
            const motion_t *mo = motion_get();
            pitch = mo->pitch; roll = mo->roll;
            // the speed: the tilt; or the joystick's Y; or plain 1x (Tilt off, or no sensor). Roll: delay or stutter.
            gest_tilt(pitch, roll, S.tilt, imu, in_has_joy && !menu && TI(TU_JO_SPEED) == 0, in_jy, S.spd_min, S.spd_max, &to);
            speed_cmd = to.speed;
            fx_set_delay(to.delay);
            fx_set_stutter(to.stutter);
            if (S.snap) speed_cmd = gest_snap(speed_cmd);
            recorder_set_speed(speed_cmd);
            float st = (float)S.pitch_st + joy_pitch_st;
            pitch_ratio = exp2f(st / 12.0f);
            recorder_set_pitch(pitch_ratio);

            if (usb_connected()) {                            // MIDI out: only what changed
                static int last[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
                int v[8] = { 0 };
                v[1] = (int)(64 - pitch * 64.0f / 90.0f); v[2] = (int)(64 + roll * 64.0f / 90.0f);   // CC 1 as before: + nose up
                v[3] = (int)(64 + speed_cmd * 63.0f / REC_SPEED_LIMIT); v[4] = (int)(recorder_position() * 127.0f);
                v[5] = in_has_joy ? (int)(64 + in_jx * 63.0f) : 64; v[6] = in_has_joy ? (int)(64 + in_jy * 63.0f) : 64;
                v[7] = (int)(meter_in * 127.0f);
                for (int c = 1; c < 8; c++) {
                    if (v[c] < 0) v[c] = 0;
                    if (v[c] > 127) v[c] = 127;
                    int d = v[c] - last[c]; if (d < 0) d = -d;
                    // a deadband of 2 keeps sensor noise off the wire; the ends always get through
                    if (d >= 2 || (v[c] != last[c] && (v[c] == 0 || v[c] == 127))) { last[c] = v[c]; usb_midi_send_cc(c, v[c]); }
                }
            }
        }

        if (rig_state_on() && (TI(TU_MO_RATE) == 1 || tick % 2 == 0)) {   // the rig: everything the controls decided
            const motion_t *mo = motion_get();
            float v[RS_N] = { 0 };
            int fl = (frozen ? RF_FROZEN : 0) | (menu ? RF_MENU : 0) | (menu_edit ? RF_MENU_EDIT : 0) | (injected ? RF_INJECTED : 0)
                   | (held ? RF_REC_DOWN : 0) | (in_has_joy ? RF_JOY : 0) | (has_imu ? RF_IMU : 0) | (mo->rest ? RF_REST : 0)
                   | (recorder_state() == REC_RECORDING ? RF_RECORDING : 0) | (recorder_state() == REC_PLAYING ? RF_PLAYING : 0);
#if HAS_CONTROLS
            if (in.pressed) fl |= RF_ENC_DOWN;
#endif
            v[RS_state] = (float)recorder_state(); v[RS_mode] = (float)S.mode; v[RS_flags] = (float)fl;
            v[RS_qw] = mo->q[0]; v[RS_qx] = mo->q[1]; v[RS_qy] = mo->q[2]; v[RS_qz] = mo->q[3];
            v[RS_pitch] = mo->pitch; v[RS_roll] = mo->roll; v[RS_yaw] = mo->yaw;
            v[RS_tilt_u] = to.u; v[RS_zone] = (float)to.zone; v[RS_detent] = to.detent;
            v[RS_speed_cmd] = speed_cmd; v[RS_speed_now] = recorder_speed_now(); v[RS_pos] = recorder_position();
            v[RS_loop_s] = recorder_loop_seconds(); v[RS_delay] = to.delay; v[RS_stutter] = to.stutter;
            v[RS_joy_x] = in_jx; v[RS_joy_y] = in_jy; v[RS_joy_rx] = (float)in_raw_x; v[RS_joy_ry] = (float)in_raw_y;
            v[RS_enc] = (float)(((enc_total + 8192) & 16383) - 8192);
            v[RS_in_peak] = meter_in; v[RS_out_peak] = meter_out; v[RS_in_clips] = clip_hold > 0 ? 1.0f : 0.0f;
            v[RS_cpu] = (float)audio_cpu_percent(); v[RS_heap_kb] = (float)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
            v[RS_i2c_fail] = (float)(imu_fails() > 8191 ? 8191 : imu_fails());
            v[RS_bias_x] = mo->bias[0]; v[RS_bias_y] = mo->bias[1]; v[RS_bias_z] = mo->bias[2];
            v[RS_rate_hz] = (float)imu_rate_hz(); v[RS_menu_param] = (float)param;
            v[RS_loop_ms] = (float)loop_us / 1000.0f; v[RS_wire_drops] = (float)(rig_drops() > 8191 ? 8191 : rig_drops());
            rig_state(v, audio_frames());
        }

        int s = recorder_state();
        if (s != last_state) {
            static const char *SN[] = { "idle", "rec", "play" };
            gest_emit("take.state", (float)s, 0, SN[s < 0 || s > 2 ? 0 : s]);
            if (s == REC_RECORDING) usb_midi_send_note(60, 100, true);
            else if (last_state == REC_RECORDING) usb_midi_send_note(60, 0, false);
            if (s == REC_PLAYING) usb_midi_send_note(62, 100, true);
            else if (last_state == REC_PLAYING) usb_midi_send_note(62, 0, false);
            if (s != REC_PLAYING) flock_loop_gone();          // a new take, or a tap: the flock's tempo is free again
            last_state = s;
            if (s == REC_PLAYING) ESP_LOGI(TAG, "%s  loop %.2f s", state_name(s), (double)recorder_loop_seconds());
            else ESP_LOGI(TAG, "%s", state_name(s));
        }
        if (s == REC_PLAYING && recorder_loop_start() != loop_seen) {   // a loop has begun: the flock's tempo, or its beats
            loop_seen = recorder_loop_start();
            flock_loop_made(recorder_loop_frames(), loop_seen);
            ESP_LOGI(TAG, "loop %.2f s: %s, %.0f BPM", (double)recorder_loop_seconds(), flock_snapping() ? "on the flock's beats" : "sets the tempo", (double)flock_bpm());
        }
        int slot = audio_mic_slot();
        if (slot != last_slot) { last_slot = slot; ESP_LOGI(TAG, "mic data in the %s slot", slot ? "RIGHT" : "LEFT"); }

        {   // meters decay between reads
            float ip = audio_in_peak(), op = audio_out_peak();
            meter_in = ip > meter_in ? ip : meter_in * 0.9f;
            meter_out = op > meter_out ? op : meter_out * 0.9f;
            if (audio_in_clips() > 0) clip_hold = 50; else if (clip_hold > 0) clip_hold--;
        }
        if (toast > 0) toast -= 10;
        if (result_ms > 0) result_ms -= 10;
#if OLED_ENABLE
        if (tick % 5 == 0 && (oled_present() || rig_screen_on())) {   // no panel: drawn for the rig's mirror alone
            int shown = vol_ms > 0 && !menu ? P_VOL : param;   // the toast: Volume while the knob is on it
            char pv[24]; param_text(shown, pv, sizeof pv);
            ui_t u = {
                .state = s, .mode = S.mode, .speed_now = recorder_speed_now(), .speed_cmd = speed_cmd, .spd_max = S.spd_max,
                .pitch_ratio = pitch_ratio, .tilt_pitch = TI(TU_TI_AXIS) ? roll : -pitch, .tilt_roll = TI(TU_TI_AXIS) ? -pitch : roll,
                .has_imu = has_imu, .tilt_on = S.tilt, .frozen = frozen, .pos = recorder_position(), .rec_fill = recorder_record_fill(),
                .loop_s = recorder_loop_seconds(), .max_s = recorder_max_seconds(), .meter_in = meter_in, .meter_out = meter_out,
                .clip = clip_hold > 0, .cpu = audio_cpu_percent(), .menu = menu, .menu_edit = menu_edit, .param = param, .param_n = P_N,
                .param_name = param_name[shown], .param_val = pv, .param_frac = param_frac(shown), .toast_ms = toast,
                .busy_text = job != JOB_NONE ? job_text : NULL, .busy_frac = slots_progress(),
                .note = result_ms > 0 ? result_text : tick < 700 ? diag_note() : NULL,
                .idle_ms = recorder_state() == REC_IDLE ? idle_ms : 0,
                .voice = vox_on() ? vox_name(S.voice) : NULL,
                .peers = S.link ? flock_peers() : 0, .beat_flash = flock_beat_flash(), .closing = recorder_closing(),
            };
            u.has_joy = in_has_joy;
            u.fx_delay = fx_delay_active(); u.fx_stutter = fx_stutter_active();
            ui_draw(&u);
        }
#endif
        if (tick % 100 == 0) {
            ESP_LOGI(TAG, "%-4s %s speed %+5.2f (cmd %+5.2f) pitch %.2f pos %3d%%  pitch %+5.1f roll %+5.1f  mic %ddB  cpu %2d%%",
                     state_name(s), S.mode == REC_MODE_GRAIN ? "GRN" : "TAPE", (double)recorder_speed_now(), (double)speed_cmd,
                     (double)pitch_ratio, (int)(recorder_position() * 100.0f), (double)pitch, (double)roll, S.mic_db, audio_cpu_percent());
            audio_stats_t a; audio_get_stats(&a);               // dropouts are worth a line of their own, when they happen
            if (a.rx_ovf != last_stats.rx_ovf || a.tx_ovf != last_stats.tx_ovf || a.read_err != last_stats.read_err)
                ESP_LOGW(TAG, "audio: %lu rx overruns, %lu tx underruns, %lu read errors since boot (%lu flash stalls)",
                         (unsigned long)a.rx_ovf, (unsigned long)a.tx_ovf, (unsigned long)a.read_err, (unsigned long)a.stalls_flash);
            last_stats = a;
        }
        if (tick % 6000 == 0) diag_log_status();                // once a minute, for the long soak logs
        loop_us = esp_timer_get_time() - t_pass;
    }
}
