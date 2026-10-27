#include "diagnostics.h"
#include "protocol.h"
#include "update.h"
#include "mcu.h"

static struct wacca_decoder decoder;
static uint8_t reply[64];
static size_t reply_length;
static uint32_t last_generation, reset_after;
static bool update_requested, resetting;

static void respond(const struct wacca_message *m, enum wacca_role role, uint32_t stage, uint32_t pending, uint32_t hardware)
{
	uint8_t payload[40] = {0};
	uint8_t type = WACCA_RESPONSE;
	size_t n;

	if ((m->type == WACCA_GET_INFO || m->type == WACCA_GET_STATUS) && m->length == 0)
	{
		struct wacca_usb_stats stats;
		wacca_usb_get_stats(&stats);
		payload[0] = m->type;
		payload[1] = (uint8_t)role;
		payload[2] = 0; payload[3] = 1; // Firmware 1.0, little-endian.
		wacca_put_u32(payload + 4, SystemCoreClock);
		wacca_put_u32(payload + 8, stage);
		wacca_put_u32(payload + 12, hardware);
		wacca_put_u32(payload + 16, pending);
		wacca_put_u32(payload + 20, stats.resets);
		wacca_put_u32(payload + 24, stats.errors);
		wacca_put_u32(payload + 28, decoder.rejected);
		wacca_put_u32(payload + 32, WACCA_CAP_USB_UPDATE); // Scan/output engines remain pending.
		n = 36;
	}
	else if (m->type == WACCA_ENTER_UPDATE && m->length == 1 && m->payload[0] == (uint8_t)role)
	{
		payload[0] = WACCA_ENTER_UPDATE;
		payload[1] = (uint8_t)role;
		update_requested = true;
		n = 2;
	}
	else if (m->type == WACCA_PING && m->length <= 39)
	{
		payload[0] = WACCA_PING;
		for (size_t i = 0; i < m->length; ++i)
			payload[i + 1] = m->payload[i];
		n = (size_t)m->length + 1U;
	}
	else
	{
		type = WACCA_ERROR;
		payload[0] = m->type;
		payload[1] = 1; // Unsupported type, length or target role.
		n = 2;
	}
	reply_length = wacca_message_encode(type, m->sequence, payload, n, reply, sizeof(reply));
}
void wacca_diagnostics_poll(enum wacca_role role, uint32_t stage, uint32_t pending, uint32_t hardware)
{
	const uint32_t current = wacca_usb_generation();

	if (current != last_generation)
	{
		last_generation = current;
		wacca_decoder_reset(&decoder);
		reply_length = 0;
		update_requested = false;
		resetting = false;
	}
	if (!wacca_usb_ready()) return;

	if (reply_length)
	{
		if (!wacca_usb_send(reply, reply_length)) return;
		reply_length = 0;
		if (update_requested)
		{
			reset_after = wacca_now_us();
			resetting = true;
		}
	}
	if (resetting)
	{
		// Acknowledge first; never reset while the host is still reading that reply.
		if ((uint32_t)(wacca_now_us() - reset_after) >= 10000 && wacca_usb_tx_idle())
			wacca_enter_updater(role);
		return;
	}
	// One byte at a time avoids consuming commands beyond a pending response.
	// Limit each pass to 64 bytes so other real-time tasks keep getting service.
	for (unsigned budget = 0; budget < 64; ++budget)
	{
		uint8_t byte;
		struct wacca_message message;
		if (wacca_usb_read(&byte, 1) == 0) break;
		if (wacca_decoder_feed(&decoder, byte, &message))
		{
			respond(&message, role, stage, pending, hardware);
			break;
		}
	}
}
