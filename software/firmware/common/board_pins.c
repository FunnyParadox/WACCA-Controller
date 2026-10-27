#include "board_pins.h"

const struct wacca_pin wacca_touch_irq[WACCA_SECTORS] = {
	{ WACCA_GPIO_A, 0, 10 }, { WACCA_GPIO_A, 1, 11 },
	{ WACCA_GPIO_A, 2, 12 }, { WACCA_GPIO_A, 3, 13 },
	{ WACCA_GPIO_A, 4, 14 }, { WACCA_GPIO_A, 5, 15 },
	{ WACCA_GPIO_B, 3, 39 }, { WACCA_GPIO_B, 4, 40 },
	{ WACCA_GPIO_B, 5, 41 }, { WACCA_GPIO_B, 13, 26 },
	{ WACCA_GPIO_B, 14, 27 }, { WACCA_GPIO_B, 15, 28 }
};

const struct wacca_pin wacca_led_data[WACCA_SECTORS] = {
	{ WACCA_GPIO_A, 4, 14 }, { WACCA_GPIO_A, 5, 15 },
	{ WACCA_GPIO_A, 6, 16 }, { WACCA_GPIO_A, 7, 17 },
	{ WACCA_GPIO_B, 0, 18 }, { WACCA_GPIO_B, 1, 19 },
	{ WACCA_GPIO_B, 7, 43 }, { WACCA_GPIO_B, 6, 42 },
	{ WACCA_GPIO_B, 5, 41 }, { WACCA_GPIO_B, 4, 40 },
	{ WACCA_GPIO_B, 3, 39 }, { WACCA_GPIO_A, 15, 38 }
};

// Array index 0 is hardware I2C1 (sectors 1..6); index 1 is I2C2 (7..12).
const enum wacca_i2c_bus wacca_touch_bus[WACCA_SECTORS] = {
	WACCA_I2C_BUS1, WACCA_I2C_BUS1, WACCA_I2C_BUS1,
	WACCA_I2C_BUS1, WACCA_I2C_BUS1, WACCA_I2C_BUS1,
	WACCA_I2C_BUS2, WACCA_I2C_BUS2, WACCA_I2C_BUS2,
	WACCA_I2C_BUS2, WACCA_I2C_BUS2, WACCA_I2C_BUS2
};
const struct wacca_pin wacca_touch_scl[WACCA_I2C_BUS_COUNT] = {
	{ WACCA_GPIO_B, 6, 42 }, { WACCA_GPIO_B, 10, 21 }
};
const struct wacca_pin wacca_touch_sda[WACCA_I2C_BUS_COUNT] = {
	{ WACCA_GPIO_B, 7, 43 }, { WACCA_GPIO_B, 11, 22 }
};
const struct wacca_pin wacca_brightness = { WACCA_GPIO_A, 3, 13 };
