#ifndef WACCA_CH32_I2C_H
#define WACCA_CH32_I2C_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Hardware I2C2, PB10 SCL / PB11 SDA, 100 kHz. Call after wacca_time_init().
// Init releases both lines; keep the hub reset asserted when selecting SMBus.
bool wacca_i2c_init(void);
// Synchronous, single-owner master; no GPIO bit-banging or dynamic allocation.
// Seven-bit address, 1..256 TX bytes, optional 1..256 RX bytes after repeated START.
// A 20 ms transaction deadline and bounded STOP cleanup cover errors/stuck buses.
// A failed transfer disables the local peripheral to release its bus drivers;
// the next transfer retries initialization. External shorts cannot be cleared here.
bool wacca_i2c_transfer(uint8_t address, const uint8_t *tx, size_t tx_length,
                        uint8_t *rx, size_t rx_length);
#endif
