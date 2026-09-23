#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
typedef void *i2c_master_bus_handle_t; typedef void *i2c_master_dev_handle_t;
typedef struct { int dev_addr_length, device_address, scl_speed_hz; } i2c_device_config_t;
enum { I2C_ADDR_BIT_LEN_7 };
static inline esp_err_t i2c_master_probe(i2c_master_bus_handle_t b, int a, int t) { return 0; }
static inline esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b, const i2c_device_config_t *c, i2c_master_dev_handle_t *d) { return 0; }
static inline esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d, const uint8_t *b, int n, int t) { return 0; }
