#include "ch32v20x.h"
#include "hardware.h"
#include "board_pins.h"

void wacca_touch_irq_init(void)
{
	GPIO_InitTypeDef pins = { WACCA_TOUCH_GPIOA_MASK, GPIO_Speed_2MHz, GPIO_Mode_IPU };
	GPIO_Init(GPIOA, &pins);
	pins.GPIO_Pin = WACCA_TOUCH_GPIOB_MASK; GPIO_Init(GPIOB, &pins);
}
uint16_t wacca_touch_irq_pending(void)
{
	uint16_t pending = 0;
	const uint32_t indr[] = {GPIOA->INDR, GPIOB->INDR, GPIOC->INDR, GPIOD->INDR};

	for (unsigned sector = 0; sector < WACCA_SECTORS; ++sector)
	{
		const struct wacca_pin pin = wacca_touch_irq[sector];
		if ((indr[pin.port] & (1U << pin.bit)) == 0) { pending |= (uint16_t)(1U << sector); }
	}
	return (pending);
}
