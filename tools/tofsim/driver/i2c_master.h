// Host stand-in for ESP-IDF's I2C master driver: the calls tof.c makes, with the driver's own signatures.
// tofsim.c answers them with a fake VL53L1X.
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK                0
#define ESP_FAIL              (-1)
#define ESP_ERR_INVALID_STATE 0x103          // what a NACK comes back as
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_TIMEOUT       0x107
typedef struct sim_bus *i2c_master_bus_handle_t;
typedef struct sim_dev *i2c_master_dev_handle_t;
typedef enum { I2C_ADDR_BIT_LEN_7 } i2c_addr_bit_len_t;
typedef struct { i2c_addr_bit_len_t dev_addr_length; uint16_t device_address; uint32_t scl_speed_hz; } i2c_device_config_t;
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int xfer_timeout_ms);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *cfg, i2c_master_dev_handle_t *ret);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d, const uint8_t *wr, size_t n, int xfer_timeout_ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d, const uint8_t *wr, size_t nw, uint8_t *rd, size_t nr, int xfer_timeout_ms);
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus);
