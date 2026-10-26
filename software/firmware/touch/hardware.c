#include "ch32v20x.h"
#include "hardware.h"
#include "board_pins.h"
#include "mcu.h"
#include "i2c.h"

static void delay(void *ctx, uint32_t us)
{
	(void)ctx;
	wacca_delay_us(us);
}

static void reset(void *ctx, bool asserted)
{
	(void)ctx;
	if (asserted) { GPIOB->BCR = GPIO_Pin_9; } else { GPIOB->BSHR = GPIO_Pin_9; }
}
static bool prepare(void *ctx)
{
	(void)ctx;
	return wacca_i2c_init(WACCA_I2C_BUS2, WACCA_I2C_STANDARD_HZ);
}
static bool write_bus(void *ctx, uint8_t addr, const uint8_t *tx, size_t n)
{
	(void)ctx;
	return wacca_i2c_transfer(WACCA_I2C_BUS2, addr, tx, n, NULL, 0);
}
static bool read_bus(void *ctx, uint8_t addr, const uint8_t *tx, size_t n, uint8_t *rx, size_t rx_n)
{
	(void)ctx;
	return wacca_i2c_transfer(WACCA_I2C_BUS2, addr, tx, n, rx, rx_n);
}
const struct wacca_hub_io *wacca_hub_interface(void)
{
	static const struct wacca_hub_io io = { NULL, reset, prepare, delay, write_bus, read_bus };
	return (&io);
}
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
