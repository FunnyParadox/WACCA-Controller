#include "mcu.h"
#include "hardware.h"
#include "diagnostics.h"
#include "i2c.h"
#include <string.h>

struct boot_diagnostics { uint32_t magic, stage, clock_hz, hub_result, pending_sectors; };
volatile struct boot_diagnostics wacca_touch_boot;
static uint8_t hub_image[WACCA_HUB_CONFIG_SIZE];

int main(void)
{
	uint32_t marker;
	memcpy(&marker, "WCON", sizeof(marker));
	wacca_touch_boot.magic = marker; // Preserve the volatile destination access.
	wacca_touch_boot.clock_hz = SystemCoreClock;
	wacca_touch_boot.stage = 1;
	if (wacca_clock_state != WACCA_CLOCK_READY) while (true) { }

	wacca_time_init();
	wacca_touch_irq_init();
	wacca_touch_boot.stage = 2;

	if (!wacca_usb2422_config(hub_image, sizeof(hub_image), 500, 100)) while (true) { };

	const enum wacca_hub_result status = wacca_usb2422_start(wacca_hub_interface(), hub_image, sizeof(hub_image));
	wacca_touch_boot.hub_result = (uint32_t)status;
	if (status != WACCA_HUB_OK)
	{
		wacca_touch_boot.stage = 0xE0;
		while (true) { }
	};
	// The hub has acknowledged SMBus power-down. Its configuration used 100 kHz;
	// the touch chips support 400 kHz. This sets transport speed, not scan rate.
	if (!wacca_i2c_set_speed(WACCA_I2C_BUS2, WACCA_I2C_FAST_HZ))
	{
		const struct wacca_hub_io *hub = wacca_hub_interface();
		hub->set_reset(hub->ctx, true);
		wacca_touch_boot.stage = 0xE1;
		while (true) { }
	}
	// Independent PB6/PB7 bus for sectors 1..6; IRQs 10..12 use PB13..PB15.
	if (!wacca_i2c_init(WACCA_I2C_BUS1, WACCA_I2C_FAST_HZ))
	{
		const struct wacca_hub_io *hub = wacca_hub_interface();
		hub->set_reset(hub->ctx, true);
		wacca_touch_boot.stage = 0xE2;
		while (true) { }
	}
	wacca_usb_init(WACCA_ROLE_TOUCH);
	wacca_touch_boot.stage = 3;

	while (true)
	{
		(void)wacca_i2c_poll(WACCA_I2C_BUS1);
		(void)wacca_i2c_poll(WACCA_I2C_BUS2);
		wacca_touch_boot.pending_sectors = wacca_touch_irq_pending();
		wacca_diagnostics_poll(WACCA_ROLE_TOUCH, wacca_touch_boot.stage, wacca_touch_boot.pending_sectors, wacca_touch_boot.hub_result);
	}
}

