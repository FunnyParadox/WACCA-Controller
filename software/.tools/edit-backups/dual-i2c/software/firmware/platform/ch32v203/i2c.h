#ifndef WACCA_CH32_I2C_H
#define WACCA_CH32_I2C_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WACCA_I2C_STANDARD_HZ = 100000, WACCA_I2C_FAST_HZ = 400000 };
// Hardware I2C2, PB10 SCL / PB11 SDA. Call after wacca_time_init().
// Init selects 100 kHz and releases both lines; keep the hub reset asserted.
bool wacca_i2c_init(void);
// Select 100/400 kHz while idle. Before 400 kHz, finish hub configuration and
// power down its SMBus interface; every remaining target must support Fast-mode.
// Error recovery retains this selection; explicit init returns to 100 kHz.
bool wacca_i2c_set_speed(uint32_t hz);
// Synchronous, single-owner master; no GPIO bit-banging or dynamic allocation.
// Seven-bit address, 0..256 TX and 0..256 RX bytes; at least one direction.
// Combined transfers use repeated START. Pure reads avoid a register-write phase.
// A 20 ms transaction deadline and bounded STOP cleanup cover errors/stuck buses.
// A failed transfer disables the local peripheral to release its bus drivers;
// the next transfer retries initialization. External shorts cannot be cleared here.
bool wacca_i2c_transfer(uint8_t address, const uint8_t *tx, size_t tx_length,
                        uint8_t *rx, size_t rx_length);
#endif
