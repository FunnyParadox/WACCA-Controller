#include "ch32v20x.h"
#include "platform.h"
#include "board_pins.h"
#include "i2c.h"

static uint32_t ticks_per_us;
void wacca_time_init(void)
{
	ticks_per_us = SystemCoreClock / 1000000U;

	SysTick->CTLR = 0; // Stop the timer while configuring it
	SysTick->CNT = 0; // Start timer at 0
	SysTick->CMP = UINT64_MAX;
	SysTick->SR = 0; // Clear the status

	// LSB-Bit 4 / 0: count in reverse
	// LSB-Bit 3 / 0: automatic reload at comparison
	// LSB-Bit 2 / 1: use HCLK (High-speed clock)
	// LSB-Bit 1 / 0: enable counter interrupt
	// LSB-Bit 0 / 1: enable counter
	SysTick->CTLR = 0b00101;
}
void wacca_delay_us(uint32_t us)
{
	const uint32_t start = (uint32_t)(SysTick->CNT);
	const uint32_t ticks = us * ticks_per_us;
	while ((uint32_t)((uint32_t)(SysTick->CNT) - start) < ticks) { }
}
uint32_t wacca_now_us(void)
{
	uint64_t a, b;
	do { a = SysTick->CNT; b = SysTick->CNT; } while ((a >> 32) != (b >> 32));
	return (uint32_t)(b / ticks_per_us);
}
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
    return wacca_i2c_init();
}
static bool write_bus(void *ctx, uint8_t addr, const uint8_t *tx, size_t n)
{
    (void)ctx;
    return wacca_i2c_transfer(addr, tx, n, NULL, 0);
}
static bool read_bus(void *ctx, uint8_t addr, const uint8_t *tx, size_t n, uint8_t *rx, size_t rx_n)
{
    (void)ctx;
    return wacca_i2c_transfer(addr, tx, n, rx, rx_n);
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
