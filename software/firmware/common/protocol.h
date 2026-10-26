#ifndef WACCA_PROTOCOL_H
#define WACCA_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board_pins.h"
#define WACCA_PROTOCOL_VERSION 1
// The game callback is RGBA, but USB carries only 480 RGB triplets (1440 bytes).
// Expand colors 10x at physical LED output, never in the USB receive path.
#define WACCA_MAX_PAYLOAD ((size_t)WACCA_LOGICAL_LED_RGB_BYTES)
#define WACCA_MAX_RAW (WACCA_MAX_PAYLOAD+8U)
#define WACCA_MAX_ENCODED (WACCA_MAX_RAW+WACCA_MAX_RAW/254U+2U)
enum wacca_message_type { WACCA_GET_INFO=1, WACCA_GET_STATUS=2, WACCA_PING=3,
	WACCA_TOUCH_STATE=0x10, WACCA_LED_FRAME=0x20, WACCA_RESPONSE=0x80, WACCA_ERROR=0xFF };
struct wacca_message { uint8_t type; uint16_t sequence, length; const uint8_t *payload; };
struct wacca_decoder { uint8_t buffer[WACCA_MAX_ENCODED]; size_t used; bool discard; uint32_t rejected; };
void wacca_decoder_reset(struct wacca_decoder *d);
/* Message storage remains valid until the next feed call. A zero delimiter
 * resynchronizes after malformed/oversized packets; no allocation or blocking. */
bool wacca_decoder_feed(struct wacca_decoder *d, uint8_t byte, struct wacca_message *m);
size_t wacca_message_encode(uint8_t type, uint16_t sequence, const uint8_t *payload, size_t length, uint8_t *out, size_t capacity);
#endif

