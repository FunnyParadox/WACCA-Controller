#ifndef WACCA_MERCURY_FRAME_H
#define WACCA_MERCURY_FRAME_H
#include "protocol.h"

enum { MERCURY_TOUCH_COUNT = WACCA_SECTORS * WACCA_TOUCH_PER_SECTOR };
struct mercury_touch_sample
{
	bool state[MERCURY_TOUCH_COUNT];
	uint16_t sequence;
	bool have_sequence;
};

bool mercury_led_apply(uint8_t rgb[WACCA_LOGICAL_LED_RGB_BYTES], uint32_t count, const uint8_t *rgba);
bool mercury_touch_apply(struct mercury_touch_sample *sample, const struct wacca_message *message);
bool mercury_info_matches(const struct wacca_message *message, uint16_t sequence, uint8_t role, uint32_t capability);
#endif
