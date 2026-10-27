#include "diagnostics.h"
#include "protocol.h"
#include "update.h"
#include "mcu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t SystemCoreClock = 144000000;
static uint8_t incoming[WACCA_MAX_ENCODED], outgoing[64];
static size_t received, cursor, transmitted;
static uint32_t generation, now;
static unsigned resets;
static bool ready, writable, idle;
static enum wacca_role reset_role;

bool wacca_usb_ready(void) { return (ready); }
bool wacca_usb_tx_idle(void) { return (idle); }
uint32_t wacca_usb_generation(void) { return (generation); }
uint32_t wacca_now_us(void) { return (now); }
void wacca_enter_updater(enum wacca_role role) { ++resets; reset_role = role; }
void wacca_usb_get_stats(struct wacca_usb_stats *stats) { memset(stats, 0, sizeof(*stats)); }
size_t wacca_usb_read(uint8_t *bytes, size_t capacity)
{
	assert(capacity == 1);
	if (cursor == received) return (0);
	bytes[0] = incoming[cursor++];
	return (1);
}
bool wacca_usb_send(const uint8_t *bytes, size_t length)
{
	if (!writable) return (false);
	assert(length <= sizeof(outgoing));
	memcpy(outgoing, bytes, length);
	transmitted = length;
	return (true);
}
static void poll(enum wacca_role role) { wacca_diagnostics_poll(role, 3, 0x123, 2); }
static void fresh(enum wacca_role role)
{
	++generation;
	received = cursor = transmitted = resets = 0;
	now = 0;
	ready = writable = idle = true;
	poll(role);
}
static void command(enum wacca_role role, uint8_t type, const uint8_t *payload, size_t length)
{
	cursor = transmitted = 0;
	received = wacca_message_encode(type, 17, payload, length, incoming, sizeof(incoming));
	assert(received);
	poll(role); // Decode request.
	poll(role); // Queue reply.
}
static struct wacca_message response(void)
{
	static struct wacca_decoder decoder;
	wacca_decoder_reset(&decoder);
	struct wacca_message message = {0};
	for (size_t i = 0; i < transmitted; ++i)
		if (wacca_decoder_feed(&decoder, outgoing[i], &message)) return (message);
	assert(false);
	return (message);
}
int main(void)
{
	for (enum wacca_role role = WACCA_ROLE_TOUCH; role <= WACCA_ROLE_LED; ++role)
	{
		fresh(role);
		command(role, WACCA_GET_INFO, NULL, 0);
		struct wacca_message reply = response();
		assert(reply.type == WACCA_RESPONSE && reply.length == 36 && reply.payload[1] == (uint8_t)role);
		assert(reply.payload[2] == 0 && reply.payload[3] == 1);
		assert(wacca_get_u32(reply.payload + 32) == WACCA_CAP_USB_UPDATE);
		assert(wacca_get_u32(reply.payload + 12) == 2);

		const uint8_t wrong = role == WACCA_ROLE_TOUCH ? WACCA_ROLE_LED : WACCA_ROLE_TOUCH;
		command(role, WACCA_ENTER_UPDATE, &wrong, 1);
		assert(response().type == WACCA_ERROR);
		now += 100000;
		poll(role);
		assert(!resets);
		command(role, WACCA_ENTER_UPDATE, NULL, 0);
		assert(response().type == WACCA_ERROR && !resets);

		fresh(role);
		const uint8_t target = (uint8_t)role;
		writable = false;
		command(role, WACCA_ENTER_UPDATE, &target, 1);
		now = UINT32_MAX - 5000;
		poll(role);
		assert(!transmitted && !resets); // Backpressure must delay the reset.
		writable = true;
		poll(role);
		reply = response();
		assert(reply.type == WACCA_RESPONSE && reply.length == 2 && reply.payload[0] == WACCA_ENTER_UPDATE && reply.payload[1] == target);
		now += 11000;
		idle = false;
		poll(role);
		assert(!resets); // Wait for the actual USB IN completion, including wraparound.
		idle = true;
		poll(role);
		assert(resets == 1 && reset_role == role);

		fresh(role);
		command(role, WACCA_ENTER_UPDATE, &target, 1);
		++generation;
		now += 20000;
		poll(role);
		assert(!resets); // A bus reset cancels the pending handoff.
	}
	puts("USB application handoff: both roles, capability, invalid requests, backpressure, completion and bus-reset cancellation passed");
	return (0);
}
