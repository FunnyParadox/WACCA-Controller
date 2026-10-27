#include "board_pins.h"
#include <assert.h>
#include <stdio.h>

static uint16_t pin_mask(const struct wacca_pin *pins, unsigned port)
{
	uint16_t mask = 0;
	for (size_t i = 0; i < WACCA_SECTORS; ++i)
	{
		assert(pins[i].bit < 16 && pins[i].package_pin >= 1 && pins[i].package_pin <= 48);
		if (pins[i].port == port)
		{
			const uint16_t bit = (uint16_t)(1U << pins[i].bit);
			assert((mask & bit) == 0);
			mask = (uint16_t)(mask | bit);
		}
	}
	return (mask);
}

static void check_pin_maps(void)
{
	assert(pin_mask(wacca_touch_irq, WACCA_GPIO_A) == WACCA_TOUCH_GPIOA_MASK);
	assert(pin_mask(wacca_touch_irq, WACCA_GPIO_B) == WACCA_TOUCH_GPIOB_MASK);
	assert(pin_mask(wacca_led_data, WACCA_GPIO_A) == WACCA_LED_GPIOA_MASK);
	assert(pin_mask(wacca_led_data, WACCA_GPIO_B) == WACCA_LED_GPIOB_MASK);
	assert((WACCA_LED_GPIOA_MASK & (uint16_t)(1U << wacca_brightness.bit)) == 0);
	assert((WACCA_LED_GPIOA_MASK & UINT16_C(0x7800)) == 0); // USB/debug
	assert(wacca_brightness.port == WACCA_GPIO_A && wacca_brightness.bit == 3);
	assert(wacca_brightness.package_pin == 13);
	const uint16_t bus_pins = UINT16_C(0x0CC0); // PB6/7 and PB10/11
	assert((WACCA_TOUCH_GPIOB_MASK & bus_pins) == 0);
	assert(wacca_touch_scl[WACCA_I2C_BUS1].bit == 6 && wacca_touch_scl[WACCA_I2C_BUS1].package_pin == 42);
	assert(wacca_touch_sda[WACCA_I2C_BUS1].bit == 7 && wacca_touch_sda[WACCA_I2C_BUS1].package_pin == 43);
	assert(wacca_touch_scl[WACCA_I2C_BUS2].bit == 10 && wacca_touch_scl[WACCA_I2C_BUS2].package_pin == 21);
	assert(wacca_touch_sda[WACCA_I2C_BUS2].bit == 11 && wacca_touch_sda[WACCA_I2C_BUS2].package_pin == 22);
	for (unsigned sector = 0; sector < WACCA_SECTORS; ++sector) {
		assert(wacca_touch_bus[sector] == (sector < 6 ? WACCA_I2C_BUS1 : WACCA_I2C_BUS2));
	}
	for (unsigned sector = 9; sector < WACCA_SECTORS; ++sector) {
		assert(wacca_touch_irq[sector].port == WACCA_GPIO_B);
		assert(wacca_touch_irq[sector].bit == sector + 4);
		assert(wacca_touch_irq[sector].package_pin == sector + 17);
	}
}

int main(void)
{
	check_pin_maps();
	puts("Board pin maps and independent sector buses passed");
	return (0);
}
