#include "board_pins.h"

const struct wacca_pin wacca_touch_irq[WACCA_SECTORS] = {
    { WACCA_GPIO_A, 0, 10 }, { WACCA_GPIO_A, 1, 11 },
    { WACCA_GPIO_A, 2, 12 }, { WACCA_GPIO_A, 3, 13 },
    { WACCA_GPIO_A, 4, 14 }, { WACCA_GPIO_A, 5, 15 },
    { WACCA_GPIO_B, 3, 39 }, { WACCA_GPIO_B, 4, 40 },
    { WACCA_GPIO_B, 5, 41 }, { WACCA_GPIO_B, 6, 42 },
    { WACCA_GPIO_B, 7, 43 }, { WACCA_GPIO_B, 8, 45 }
};

const struct wacca_pin wacca_led_data[WACCA_SECTORS] = {
    { WACCA_GPIO_A, 4, 14 }, { WACCA_GPIO_A, 5, 15 },
    { WACCA_GPIO_A, 6, 16 }, { WACCA_GPIO_A, 7, 17 },
    { WACCA_GPIO_B, 0, 18 }, { WACCA_GPIO_B, 1, 19 },
    { WACCA_GPIO_B, 7, 43 }, { WACCA_GPIO_B, 6, 42 },
    { WACCA_GPIO_B, 5, 41 }, { WACCA_GPIO_B, 4, 40 },
    { WACCA_GPIO_B, 3, 39 }, { WACCA_GPIO_A, 15, 38 }
};

const struct wacca_pin wacca_touch_scl = { WACCA_GPIO_B, 10, 21 };
const struct wacca_pin wacca_touch_sda = { WACCA_GPIO_B, 11, 22 };
const struct wacca_pin wacca_hub_reset = { WACCA_GPIO_B, 9, 46 };
const struct wacca_pin wacca_brightness = { WACCA_GPIO_A, 3, 13 };
