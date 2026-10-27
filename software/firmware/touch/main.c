#include "mcu.h"
#include "hardware.h"
#include "diagnostics.h"
#include "i2c.h"
#include <string.h>

struct boot_diagnostics { uint32_t magic, stage, clock_hz, i2c_errors, pending_sectors; };
volatile struct boot_diagnostics wacca_touch_boot;

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

	// The CH334F needs no MCU setup. A disconnected/faulty sector bus must not
	// prevent USB enumeration or entry into the firmware updater.
	for (unsigned bus = 0; bus < WACCA_I2C_BUS_COUNT; ++bus)
	{
		if (!wacca_i2c_init((enum wacca_i2c_bus)bus, WACCA_I2C_FAST_HZ))
			wacca_touch_boot.i2c_errors |= 1U << bus;
	}
	wacca_usb_init(WACCA_ROLE_TOUCH);
	wacca_touch_boot.stage = 3;

	while (true)
	{
		(void)wacca_i2c_poll(WACCA_I2C_BUS1);
		(void)wacca_i2c_poll(WACCA_I2C_BUS2);
		wacca_touch_boot.pending_sectors = wacca_touch_irq_pending();
		wacca_diagnostics_poll(WACCA_ROLE_TOUCH, wacca_touch_boot.stage,
			wacca_touch_boot.pending_sectors, wacca_touch_boot.i2c_errors);
	}
}
