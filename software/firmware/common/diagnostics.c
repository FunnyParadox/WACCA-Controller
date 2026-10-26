#include "diagnostics.h"
#include "protocol.h"
#include "mcu.h"

static struct wacca_decoder decoder;
static uint8_t reply[64];
static size_t reply_length;
static uint32_t last_generation;

static void put16(uint8_t *p, uint16_t x) { for (unsigned i = 0; i < 2; ++i) { p[i] = (uint8_t)(x >> (i << 1)); } }
static void put32(uint8_t *p, uint32_t x) { for (unsigned i = 0; i < 4; ++i) { p[i] = (uint8_t)(x >> (i << 3)); } }
static void respond(const struct wacca_message *m, enum wacca_role role, uint32_t stage, uint32_t pending, uint32_t hub)
{
	uint8_t payload[40] = {0};
	uint8_t type = WACCA_RESPONSE;
	size_t n;

	if ((m->type == WACCA_GET_INFO || m->type == WACCA_GET_STATUS) && m->length == 0)
	{
		struct wacca_usb_stats stats; wacca_usb_get_stats(&stats);
		payload[0] = m->type;
		payload[1] = (uint8_t)(role);
		put16(payload + 2, 0x0100); // firmware 1.0
		put32(payload + 4, SystemCoreClock); put32(payload + 8, stage); put32(payload + 12, hub);
		put32(payload + 16, pending); put32(payload + 20, stats.resets); put32(payload + 24, stats.errors);
		put32(payload + 28, decoder.rejected); put32(payload+32, 0); // No scan/output capabilities yet
		n = 36;
	}
	else if (m->type == WACCA_PING && m->length <= 39)
	{
		payload[0] = WACCA_PING;
		for (size_t i = 0; i < m->length; ++i)
			payload[i + 1] = m->payload[i];
		n = (size_t)(m->length) + 1U;
	}
	else
	{
		type = WACCA_ERROR;
		payload[0] = m->type;
		payload[1] = 1; // unsupported type/length
		n = 2;
	}
	reply_length = wacca_message_encode(type, m->sequence, payload, n, reply, sizeof(reply));
}
void wacca_diagnostics_poll(enum wacca_role role, uint32_t stage, uint32_t pending, uint32_t hub)
{
	const uint32_t current = wacca_usb_generation();

	if (current != last_generation)
	{
		last_generation=current; 
		wacca_decoder_reset(&decoder);
		reply_length = 0;
	}
	if (!wacca_usb_ready()) return;

	if (reply_length)
	{
		if (!wacca_usb_send(reply, reply_length)) return;
		reply_length = 0;
	}
	// One byte at a time avoids consuming commands beyond a pending response
	// Limit each pass to 64 bytes so other real-time tasks keep getting service
	for (unsigned budget = 0; budget < 64; ++budget)
	{
		uint8_t byte;
		struct wacca_message message;
		if (wacca_usb_read(&byte,1) == 0) break;
		if (wacca_decoder_feed(&decoder,byte,&message))
		{
			respond(&message,role,stage,pending,hub);
			break;
		}
	}
}
