#include "mcu.h"
#include "diagnostics.h"
volatile uint32_t wacca_led_boot_stage;

int main(void)
{
	wacca_led_boot_stage = 1;
	if (wacca_clock_state != WACCA_CLOCK_READY) while (true) { }
	wacca_time_init();
	wacca_led_boot_stage = 2;
	// LED outputs remain inputs until the waveform engine is implemented
	wacca_usb_init(WACCA_ROLE_LED);
	wacca_led_boot_stage = 3;

	while (true)
	{
		wacca_diagnostics_poll(WACCA_ROLE_LED, wacca_led_boot_stage, 0, 0);
	}
}
