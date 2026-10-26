#ifndef WACCA_CH32_I2C_H
#define WACCA_CH32_I2C_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board_pins.h"

enum { WACCA_I2C_STANDARD_HZ = 100000, WACCA_I2C_FAST_HZ = 400000 };
enum wacca_i2c_status { WACCA_I2C_IDLE, WACCA_I2C_PENDING, WACCA_I2C_COMPLETE, WACCA_I2C_ERROR };
// I2C1: PB6 SCL / PB7 SDA & I2C2: PB10 SCL / PB11 SDA (No GPIO bit-banging)
// Call after wacca_time_init(), only 100/400 kHz are accepted
// Hub startup must use I2C2 at 100 kHz, with reset asserted until its bus is prepared
// These blocking configuration calls also service transfers on the other bus
bool wacca_i2c_init(enum wacca_i2c_bus bus, uint32_t hz);
bool wacca_i2c_set_speed(enum wacca_i2c_bus bus, uint32_t hz);
// Single foreground owner; never call these from interrupts
// One active request per bus, with independent timings, buffers, recovery and completion status
// Begin queues the request without waiting for hardware; poll each bus often to let both run concurrently
// No interrupt/DMA engine or allocation is used, buffers must remain valid (TX unchanged) until COMPLETE/ERROR
// On ERROR the RX buffer can contain a partial result: discard it, poll leaves result sticky
// Invalid arguments or a busy bus are rejected without changing that request
// Seven-bit address 0x08..0x77; 0..256 bytes each way, at least one direction
// Combined transfers use repeated START; pure reads omit register selection
// A request has a 20 ms deadline plus up to 1 ms STOP cleanup, serviced by poll
// Failures release local driving; a subsequent request reinitializes only its own peripheral at the selected speed
// External shorts cannot be cleared here
bool wacca_i2c_begin(enum wacca_i2c_bus bus, uint8_t address, const uint8_t *tx, size_t tx_length, uint8_t *rx, size_t rx_length);
enum wacca_i2c_status wacca_i2c_poll(enum wacca_i2c_bus bus);
// Blocking convenience API for hub bootstrap; services both buses while waiting
bool wacca_i2c_transfer(enum wacca_i2c_bus bus, uint8_t address, const uint8_t *tx, size_t tx_length, uint8_t *rx, size_t rx_length);
#endif
