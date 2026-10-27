#include "frame.h"
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(void)
{
	uint8_t rgba[WACCA_LOGICAL_LED_RGBA_BYTES];
	uint8_t rgb[WACCA_LOGICAL_LED_RGB_BYTES], saved[sizeof(rgb)];
	for (unsigned i = 0; i < WACCA_LOGICAL_LEDS_TOTAL; ++i)
	{
		rgba[4 * i] = (uint8_t)i;
		rgba[4 * i + 1] = (uint8_t)(i >> 1);
		rgba[4 * i + 2] = (uint8_t)(255U - (i & 255U));
		rgba[4 * i + 3] = 0;
	}
	assert(mercury_led_apply(rgb, 480, rgba));
	memcpy(saved, rgb, sizeof(rgb));
	for (unsigned i = 0; i < WACCA_LOGICAL_LEDS_TOTAL; ++i) rgba[4 * i + 3] = 255;
	assert(mercury_led_apply(rgb, 480, rgba));
	assert(memcmp(saved, rgb, sizeof(rgb)) == 0); // A never changes physical color.
	for (unsigned i = 0; i < WACCA_LOGICAL_LEDS_TOTAL; ++i)
		assert(memcmp(rgb + 3 * i, rgba + 4 * i, 3) == 0);
	assert(!mercury_led_apply(rgb, 481, rgba) && memcmp(saved, rgb, sizeof(rgb)) == 0);
	assert(!mercury_led_apply(rgb, UINT32_MAX, rgba));
	assert(!mercury_led_apply(rgb, 0, rgba));
	assert(!mercury_led_apply(NULL, 1, rgba) && !mercury_led_apply(rgb, 1, NULL));
	rgba[0] = 123;
	assert(mercury_led_apply(rgb, 1, rgba));
	assert(rgb[0] == 123 && memcmp(rgb + 3, saved + 3, sizeof(rgb) - 3) == 0);

	uint8_t wire[WACCA_MAX_ENCODED];
	const size_t length = wacca_message_encode(WACCA_LED_FRAME, 7, rgb, sizeof(rgb), wire, sizeof(wire));
	assert(length > 0 && length <= 1455);
	struct wacca_decoder decoder = {0};
	struct wacca_message message;
	unsigned decoded = 0;
	for (size_t i = 0; i < length; ++i)
	{
		if (!wacca_decoder_feed(&decoder, wire[i], &message)) continue;
		assert(message.length == 1440 && message.type == WACCA_LED_FRAME);
		assert(memcmp(message.payload, rgb, sizeof(rgb)) == 0);
		++decoded;
	}
	assert(decoded == 1);

	uint8_t packed[WACCA_TOUCH_STATE_BYTES] = {0};
	struct mercury_touch_sample sample = {0};
	message = (struct wacca_message){WACCA_TOUCH_STATE, 0, sizeof(packed), packed};
	for (unsigned bit = 0; bit < MERCURY_TOUCH_COUNT; ++bit)
	{
		memset(packed, 0, sizeof(packed));
		packed[bit / 8] = (uint8_t)(1U << (bit % 8));
		message.sequence = (uint16_t)bit;
		assert(mercury_touch_apply(&sample, &message));
		for (unsigned i = 0; i < MERCURY_TOUCH_COUNT; ++i) assert(sample.state[i] == (i == bit));
	}
	assert(!mercury_touch_apply(&sample, &message)); // Duplicate sample.
	--message.sequence;
	assert(!mercury_touch_apply(&sample, &message)); // Older sample.
	sample.have_sequence = false;
	message.sequence = UINT16_MAX;
	assert(mercury_touch_apply(&sample, &message));
	message.sequence = 0;
	assert(mercury_touch_apply(&sample, &message)); // Sequence wraps normally.
	message.length = 29;
	assert(!mercury_touch_apply(&sample, &message));
	message.length = 31;
	assert(!mercury_touch_apply(&sample, &message));

	uint8_t info[36] = {WACCA_GET_INFO, 1};
	message = (struct wacca_message){WACCA_RESPONSE, 42, sizeof(info), info};
	assert(!mercury_info_matches(&message, 42, 1, WACCA_CAP_TOUCH_STATE));
	info[32] = WACCA_CAP_TOUCH_STATE;
	assert(mercury_info_matches(&message, 42, 1, WACCA_CAP_TOUCH_STATE));
	assert(!mercury_info_matches(&message, 41, 1, WACCA_CAP_TOUCH_STATE));
	assert(!mercury_info_matches(&message, 42, 2, WACCA_CAP_LED_FRAME));
	message.length = 35;
	assert(!mercury_info_matches(&message, 42, 1, WACCA_CAP_TOUCH_STATE));

	wchar_t port[32];
	assert(mercury_port_path(L"COM1", port, 32) && wcscmp(port, L"\\\\.\\COM1") == 0);
	assert(mercury_port_path(L"com004096", port, 32) && wcscmp(port, L"\\\\.\\COM4096") == 0);
	const wchar_t *invalid[] = {L"", L"COM", L"COM0", L"COM4097", L"COM-1", L"COM1foo", L"COM4294967297", L"COM1.txt", L"C:\\file", L"\\\\.\\PhysicalDrive0"};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) assert(!mercury_port_path(invalid[i], port, 32));
	puts("RGB-only framing, counts, touch ordering/sequences, capabilities and COM validation passed");
	return 0;
}
