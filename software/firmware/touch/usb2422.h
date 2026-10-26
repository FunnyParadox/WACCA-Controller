#ifndef WACCA_USB2422_H
#define WACCA_USB2422_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WACCA_HUB_ADDRESS = 0x2C, WACCA_HUB_CONFIG_SIZE = 255, WACCA_HUB_PORT_SWAP = 0x06, WACCA_HUB_READY_ATTEMPTS = 40 };
enum wacca_hub_result { WACCA_HUB_OK, WACCA_HUB_BAD_ARGUMENT, WACCA_HUB_BAD_PORT_SWAP, WACCA_HUB_BUS_INIT_FAILED, WACCA_HUB_WRITE_FAILED, WACCA_HUB_READ_FAILED, WACCA_HUB_VERIFY_FAILED };

// Callbacks are synchronous and must enforce bounded bus timeouts
// The write/read callback sends a repeated START, then reads rx_length bytes, NACKs the last byte and sends STOP
// An unsuccessful transfer must still release the bus
// Addresses are seven-bit, not pre-shifted wire bytes
// prepare_bus releases SDA and SCL while RESET_N is asserted, selecting SMBus configuration when RESET_N rises (ctx may be NULL)
struct wacca_hub_io
{
	void *ctx;
	void (*set_reset)(void *ctx, bool asserted);
	bool (*prepare_bus)(void *ctx);
	void (*delay_us)(void *ctx, uint32_t us);
	bool (*write)(void *ctx, uint8_t address, const uint8_t *tx, size_t length);
	bool (*write_read)(void *ctx, uint8_t address, const uint8_t *tx, size_t tx_length, uint8_t *rx, size_t rx_length);
};

// Builds a bus-powered, compound hub profile for the two permanent MCUs
// No external overcurrent monitor is fitted and current budgets must be even 'mA's: total 100..500, hub circuitry 2..100, hub <= total
// These are declared USB budgets, not a measurement of board consumption
bool wacca_usb2422_config(uint8_t *image, size_t length, unsigned total_mA, unsigned hub_mA);

// Writes the complete configuration, verifies both polarity swaps, then attaches
// Any bus/verification failure reasserts reset and leaves the hub detached
// Invalid arguments are rejected before touching the hardware
// The caller must serialize access to the shared touch/hub I2C bus and keep image and callbacks unchanged for the duration of this operation
enum wacca_hub_result wacca_usb2422_start(const struct wacca_hub_io *io, const uint8_t *image, size_t length);

#endif
