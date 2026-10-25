// Why did we reboot, and what were we doing? Plus the one-line status report.
//
// The S3 in USB mode has no console, so an abnormal reset (brownout, panic, watchdog) would leave no trace.
// Modules drop a short crumb in RTC memory (it survives a reset) before a risky step; at the next boot
// diag_note() says e.g. "CRASH: slot save" and the UI shows it. Panics are captured in full (reason or
// abort text and a short backtrace, through a --wrap of esp_panic_handler) and reported as text:
// send SysEx F0 7D 53 F7 ('S') and the device logs its status and the last crash report, which
// `tools/sl_midi.py status` prints and decodes against firmware/build/<name>.elf.
#pragma once
#include <stddef.h>
#include <stdbool.h>

void        diag_init(void);                // first thing in app_main
void        diag_crumb(const char *step);   // <= 15 chars; NULL clears
const char *diag_note(void);                // NULL after a normal boot, else "CRASH: step", "BROWNOUT", "WATCHDOG", "SAFE MODE: n crashes"
int         diag_crash_streak(void);        // crash or watchdog resets in a row, none of them followed by 10 s of running
bool        diag_safe_mode(void);           // three or more: the app starts from default settings
void        diag_mark_alive(void);          // the app has run normally for a while: the streak is over
size_t      diag_crash_report(char *buf, size_t n);   // the last abnormal reset, or "OK ..."
size_t      diag_status(char *buf, size_t n);         // version, uptime, heap, stacks, audio counters
void        diag_log_status(void);                    // both of the above, as log lines
