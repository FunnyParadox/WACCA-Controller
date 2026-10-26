#include "ch32v20x.h"
#include "mcu.h"

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
