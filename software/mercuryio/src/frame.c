#include "frame.h"

bool mercury_led_apply(uint8_t rgb[WACCA_LOGICAL_LED_RGB_BYTES], uint32_t count, const uint8_t *rgba)
{
	if (!rgb || !rgba || count == 0 || count > WACCA_LOGICAL_LEDS_TOTAL) return false;
	for (size_t i = 0; i < count; ++i)
	{
		rgb[3 * i] = rgba[4 * i];
		rgb[3 * i + 1] = rgba[4 * i + 1];
		rgb[3 * i + 2] = rgba[4 * i + 2];
	}
	return true;
}

bool mercury_touch_apply(struct mercury_touch_sample *sample, const struct wacca_message *message)
{
	if (!sample || !message || !message->payload || message->type != WACCA_TOUCH_STATE || message->length != WACCA_TOUCH_STATE_BYTES) return (false);
	if (sample->have_sequence)
	{
		const uint16_t distance = (uint16_t)(message->sequence - sample->sequence);
		if (!distance || distance >= UINT16_C(0x8000)) return (false);
	}
	for (unsigned i = 0; i < MERCURY_TOUCH_COUNT; ++i) sample->state[i] = (message->payload[i / 8] & (1U << (i % 8))) != 0;
	sample->sequence = message->sequence;
	sample->have_sequence = true;
	return (true);
}

bool mercury_info_matches(const struct wacca_message *message, uint16_t sequence, uint8_t role, uint32_t capability)
{
	if (!message || !message->payload || message->type != WACCA_RESPONSE || message->sequence != sequence || message->length != 36 || message->payload[0] != WACCA_GET_INFO || message->payload[1] != role) return (false);
	const uint8_t *p = message->payload + 32;
	const uint32_t flags = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	return ((flags & capability) == capability);
}
