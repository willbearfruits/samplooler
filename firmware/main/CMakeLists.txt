set(srcs "main.c" "audio.c" "recorder_f32.c" "recorder_q15.c" "imu.c" "oled.c" "input.c" "slots.c" "usb.c" "fx.c" "ui.c" "diag.c" "settings.c" "font_data.c")
set(reqs driver esp_driver_i2s esp_driver_i2c esp_driver_gpio esp_driver_pcnt esp_adc esp_timer fatfs wear_levelling nvs_flash esp_app_format esp_system esp_hw_support)
if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    list(APPEND reqs xtensa)
endif()
if(CONFIG_IDF_TARGET_ESP32S3)
    list(APPEND srcs "usb/usb_descriptors.c")
    list(APPEND reqs usb)
endif()
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "." REQUIRES ${reqs})
target_compile_options(${COMPONENT_LIB} PRIVATE -O3 -ffast-math -fno-math-errno -Wall -Wextra -Wdouble-promotion -Werror)
# Panics are recorded in RTC memory by diag.c's __wrap_esp_panic_handler before the real handler runs.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler" "-u __wrap_esp_panic_handler")

if(CONFIG_IDF_TARGET_ESP32S3)
    # Our tusb_config.h and the UAC part's headers go to TinyUSB itself (and to usb_device_uac through it).
    idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
    idf_component_get_property(uac_dir espressif__usb_device_uac COMPONENT_DIR)
    target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}/usb" "${uac_dir}/tusb_uac")
    # Nothing in main references the descriptor callbacks, TinyUSB does, and it is scanned after
    # libmain.a. Force the linker to pull usb_descriptors.c.obj on its first pass.
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-u tud_descriptor_device_cb" "-u tud_descriptor_configuration_cb" "-u tud_descriptor_string_cb")
endif()
