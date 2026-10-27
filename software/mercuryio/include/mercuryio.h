#ifndef WACCA_MERCURYIO_H
#define WACCA_MERCURYIO_H
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* SegaTools mercuryio API 1.0. The by-value object is the game's ABI. */
struct led_data
{
	DWORD unitCount;
	uint8_t rgba[480 * 4];
};
_Static_assert(sizeof(struct led_data) == 1924, "SegaTools LED object size");
_Static_assert(offsetof(struct led_data, rgba) == 4, "SegaTools LED color offset");
_Static_assert(sizeof(bool) == 1, "SegaTools touch callback bool size");

typedef void (*mercury_io_touch_callback_t)(const bool *state);
uint16_t mercury_io_get_api_version(void);
HRESULT mercury_io_init(void);
HRESULT mercury_io_poll(void);
void mercury_io_get_opbtns(uint8_t *opbtn);
void mercury_io_get_gamebtns(uint8_t *gamebtn);
HRESULT mercury_io_touch_init(void);
void mercury_io_touch_start(mercury_io_touch_callback_t callback);
void mercury_io_touch_set_leds(struct led_data data);
#endif
