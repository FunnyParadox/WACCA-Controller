#include "ch32v20x.h"
#include "mcu.h"
#include "usb_device.h"
#include "update.h"

// Both linker layouts put this object at 0x20004FF0, outside BSS and the stack.
static volatile struct wacca_boot_request request __attribute__((section(".boot_request"), used));

bool wacca_boot_update_requested(enum wacca_role role)
{
	const bool software_reset = (RCC->RSTSCKR & RCC_SFTRSTF) != 0;
	const bool requested = wacca_boot_request_take(&request, role, software_reset);
	RCC->RSTSCKR |= RCC_RMVF;
	return (requested);
}
void wacca_enter_updater(enum wacca_role role)
{
	// Give Windows/hub time to observe a real disconnect before re-enumeration.
	wacca_usb_detach();
	wacca_delay_us(100000);
	__disable_irq();
	wacca_boot_request_store(&request, role);
	__asm__ volatile ("fence iorw, iorw" ::: "memory");
	NVIC_SystemReset();
	while (true) { }
}
