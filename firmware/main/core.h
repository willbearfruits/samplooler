// What the files copied from the instruments' core (link.c, link_esp.c; core/firmware/main/core.h) need of it, on
// the Samplooler: app_log() and the allocators (voice_core.h), and UI_RAM for a buffer only the UI task and the
// radio touch. Keep link.c, link.h, link_esp.c and spsc.h identical to the core's copies.
#pragma once
#include "voice_core.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "esp_attr.h"
#endif
#ifdef CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define UI_RAM EXT_RAM_BSS_ATTR
#else
#define UI_RAM
#endif
