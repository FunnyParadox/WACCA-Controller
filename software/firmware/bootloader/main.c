#include "ch32v20x.h"
#include "usb_device.h"
#include "mcu.h"
#include "flash.h"

static struct wacca_update updater;
static struct wacca_decoder decoder;
static uint8_t reply[64];
static size_t reply_length;

#define STRING_VALUE_INNER(value) #value
#define STRING_VALUE(value) STRING_VALUE_INNER(value)
static __attribute__((noreturn, naked)) void enter_application(void)
{
	__asm__ volatile ("li t0, " STRING_VALUE(WACCA_APP_OFFSET) "\n\tjr t0");
}
static __attribute__((noreturn)) void jump_application(void)
{
	__disable_irq();
	for (unsigned i = 0; i < 8; ++i)
	{
		NVIC->IRER[i] = UINT32_MAX;
		NVIC->IPRR[i] = UINT32_MAX;
	}
	SysTick->CTLR = 0;
	// Application startup installs its own stack, globals, vectors and clock tree
	enter_application();
}
int main(void)
{
	if (wacca_clock_state != WACCA_CLOCK_READY) while (true) { }
	wacca_time_init();
	const enum wacca_role role = 
	#if defined(WACCA_TOUCH_MCU)
		WACCA_ROLE_TOUCH;
	#else
		WACCA_ROLE_LED;
	#endif
	wacca_update_init(&updater, role, wacca_boot_flash());
	if (!wacca_boot_update_requested(role) && wacca_update_app_valid(&updater)) jump_application();

	wacca_usb_init(role); // CH334F enumerates and enables logic power without either MCU running
	uint32_t generation = wacca_usb_generation();
	uint32_t reset_after = 0;
	bool resetting = false;
	while (true)
	{
		if (generation != wacca_usb_generation())
		{
			generation = wacca_usb_generation();
			wacca_decoder_reset(&decoder);
			reply_length = 0;
			// A USB reset cancels transfer state, never the persistent invalid marker
			wacca_update_init(&updater, role, wacca_boot_flash());
			resetting = false;
		}
		if (reply_length && wacca_usb_send(reply, reply_length))
		{
			reply_length = 0;
			if (updater.run_requested)
			{
				reset_after = wacca_now_us();
				resetting = true;
			}
		}
		if (resetting)
		{
			// Let the RUN reply complete before resetting and re-enumerating
			if ((uint32_t)(wacca_now_us() - reset_after) >= 10000 && wacca_usb_tx_idle()) { NVIC_SystemReset(); while (true) { } }
			continue;
		}
		if (reply_length || !wacca_usb_ready()) continue;

		for (unsigned budget = 0; budget < 64; ++budget)
		{
			uint8_t byte;
			struct wacca_message message;
			if (!wacca_usb_read(&byte, 1)) break;
			if (wacca_decoder_feed(&decoder, byte, &message))
			{
				uint8_t payload[WACCA_UPDATE_REPLY_SIZE];
				wacca_update_handle(&updater, &message, payload);
				reply_length = wacca_message_encode(WACCA_RESPONSE, message.sequence, payload, sizeof(payload), reply, sizeof(reply));
				break;
			}
		}
	}
}
