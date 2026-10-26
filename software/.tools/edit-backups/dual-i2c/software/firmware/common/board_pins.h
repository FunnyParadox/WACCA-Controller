#ifndef WACCA_BOARD_PINS_H
#define WACCA_BOARD_PINS_H

#include <stdint.h>

enum {
    WACCA_SECTORS = 12,
    WACCA_TOUCH_PER_SECTOR = 20,
    WACCA_LED_MODULES_PER_SECTOR = 5,
    WACCA_PHYSICAL_LEDS_PER_MODULE = 80,
    WACCA_LOGICAL_LEDS_TOTAL = 480,
    WACCA_PHYSICAL_LEDS_PER_LOGICAL_LED = 10,
    WACCA_LOGICAL_LEDS_PER_SECTOR = WACCA_LOGICAL_LEDS_TOTAL / WACCA_SECTORS,
    WACCA_LOGICAL_LEDS_PER_MODULE = WACCA_PHYSICAL_LEDS_PER_MODULE / WACCA_PHYSICAL_LEDS_PER_LOGICAL_LED,
    WACCA_LOGICAL_LED_RGB_BYTES = WACCA_LOGICAL_LEDS_TOTAL * 3,
    WACCA_LOGICAL_LED_RGBA_BYTES = WACCA_LOGICAL_LEDS_TOTAL * 4,
    WACCA_PHYSICAL_LEDS_PER_SECTOR = WACCA_LED_MODULES_PER_SECTOR * WACCA_PHYSICAL_LEDS_PER_MODULE,
    WACCA_PHYSICAL_LEDS_TOTAL = WACCA_SECTORS * WACCA_PHYSICAL_LEDS_PER_SECTOR,
    WACCA_LED_RGB_BYTES_PER_MODULE = WACCA_PHYSICAL_LEDS_PER_MODULE * 3,
    WACCA_LED_RGB_BYTES_PER_SECTOR = WACCA_PHYSICAL_LEDS_PER_SECTOR * 3,
    WACCA_LED_RGB_BYTES_TOTAL = WACCA_PHYSICAL_LEDS_TOTAL * 3
};
// Receive/store 480 game colors; repeat 10x only during physical output.
_Static_assert(WACCA_LOGICAL_LEDS_TOTAL * WACCA_PHYSICAL_LEDS_PER_LOGICAL_LED == WACCA_PHYSICAL_LEDS_TOTAL, "Logical/physical LED geometry");
_Static_assert(WACCA_LOGICAL_LEDS_PER_SECTOR == 40 && WACCA_LOGICAL_LEDS_PER_MODULE == 8, "Logical sector/module geometry");

enum wacca_gpio_port { WACCA_GPIO_A, WACCA_GPIO_B };
struct wacca_pin { uint8_t port; uint8_t bit; uint8_t package_pin; };

extern const struct wacca_pin wacca_touch_irq[WACCA_SECTORS];
extern const struct wacca_pin wacca_led_data[WACCA_SECTORS];
extern const struct wacca_pin wacca_touch_scl;
extern const struct wacca_pin wacca_touch_sda;
extern const struct wacca_pin wacca_hub_reset;
extern const struct wacca_pin wacca_brightness;

enum { WACCA_BRIGHTNESS_ADC_CHANNEL = 3 };
#define WACCA_HSE_HZ UINT32_C(8000000)
#define WACCA_TOUCH_GPIOA_MASK UINT16_C(0x003F)
#define WACCA_TOUCH_GPIOB_MASK UINT16_C(0x01F8)
#define WACCA_LED_GPIOA_MASK UINT16_C(0x80F0)
#define WACCA_LED_GPIOB_MASK UINT16_C(0x00FB)

// PA11/PA12 are USB, PA13/PA14 remain reserved for the debug probe
// Array index 0 names connector/sector 1, not a game-coordinate direction
#endif
