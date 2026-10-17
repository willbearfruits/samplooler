# ---- the instrument this firmware is ----
# Its name on USB (and in ALSA, so tools/sl_midi.py finds it), the product ID (Espressif's vendor ID with a
# product ID of our own, as the Samplooler's 0x4020: apply at github.com/espressif/usb-pids before boards
# go out), the serial number's prefix, and what its own files need from ESP-IDF.
set(INSTRUMENT_NAME   "SING")
set(INSTRUMENT_PID    0x4029)
set(INSTRUMENT_SERIAL "SG")
set(INSTRUMENT_REQUIRES esp_wifi esp_event)

# the core, then instrument.c and any inst_*.c next to it
file(GLOB inst_extra "${CMAKE_CURRENT_LIST_DIR}/inst_*.c" "${CMAKE_CURRENT_LIST_DIR}/layer_*.c")
set(srcs "shell.c" "front.c" "params.c" "looper.c" "calib.c" "ui.c" "audio.c" "imu.c" "oled.c" "input.c" "led.c"
         "usb.c" "diag.c" "settings.c" "font_data.c" "instrument.c" "usb/usb_descriptors.c" "link.c" "link_esp.c" ${inst_extra})
set(reqs driver esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_driver_pcnt esp_driver_rmt esp_adc esp_timer nvs_flash
         esp_app_format esp_system esp_hw_support usb xtensa ${INSTRUMENT_REQUIRES})
file(GLOB inst_lf "${CMAKE_CURRENT_LIST_DIR}/*.lf")    # an instrument's linker fragments (code in IRAM)
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "." REQUIRES ${reqs} LDFRAGMENTS ${inst_lf})
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -ffast-math -fno-math-errno -Wall -Wextra -Wdouble-promotion -Wno-format-truncation -Werror)
target_compile_definitions(${COMPONENT_LIB} PRIVATE USB_NAME="${INSTRUMENT_NAME}" USB_PID=${INSTRUMENT_PID}
                           USB_SERIAL_PREFIX="${INSTRUMENT_SERIAL}")
# Panics are recorded in RTC memory by diag.c's __wrap_esp_panic_handler before the real handler runs.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler" "-u __wrap_esp_panic_handler")

# Our tusb_config.h and the UAC part's headers go to TinyUSB itself (and to usb_device_uac through it).
idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(uac_dir espressif__usb_device_uac COMPONENT_DIR)
target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}/usb" "${uac_dir}/tusb_uac")
# Nothing in main references the descriptor callbacks, TinyUSB does, and it is scanned after libmain.a.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-u tud_descriptor_device_cb" "-u tud_descriptor_configuration_cb" "-u tud_descriptor_string_cb")
