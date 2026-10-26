#include "protocol.h"
#include "board_pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct wacca_decoder decoder;
static uint8_t payload[WACCA_MAX_PAYLOAD],encoded[WACCA_MAX_ENCODED+2];
static bool decode(const uint8_t *bytes,size_t n,struct wacca_message *m)
{
	unsigned messages=0;
	for (size_t i=0; i<n; ++i) { if (wacca_decoder_feed(&decoder,bytes[i],m)) { ++messages; } }
	return messages == 1;
}
int main(void)
{
	struct wacca_message m;
	/* Independent golden CRC from Python binascii.crc_hqx(raw,0xFFFF):
	 * raw header+payload 01 03 34 12 03 00 41 00 42 -> 0x9E62. */
	const uint8_t golden[]={6,1,3,0x34,0x12,3,2,0x41,4,0x42,0x62,0x9E,0};
	assert(decode(golden,sizeof(golden),&m) && m.type == WACCA_PING && m.sequence == 0x1234 && m.length == 3);
	const uint8_t golden_payload[]={0x41,0,0x42};
	assert(wacca_message_encode(WACCA_PING,0x1234,golden_payload,3,encoded,sizeof(encoded)) == sizeof(golden));
	assert(memcmp(golden,encoded,sizeof(golden)) == 0);
	const size_t sizes[]={0,1,7,64,245,246,247,253,254,255,500,1024,WACCA_LED_RGB_BYTES_PER_MODULE,WACCA_LED_RGB_BYTES_PER_SECTOR,WACCA_LOGICAL_LED_RGB_BYTES};
	for (unsigned pattern=0; pattern<3; ++pattern) {
		for (size_t i=0; i<sizeof(payload); ++i) { payload[i]=pattern == 0 ? 0 : pattern == 1 ? 255 : (uint8_t)i; }
		for (unsigned s=0; s<sizeof(sizes)/sizeof(sizes[0]); ++s) {
			const size_t n=sizes[s]; memset(encoded,0xA5,sizeof(encoded));
			const size_t out=wacca_message_encode(WACCA_LED_FRAME,0x1234,payload,n,encoded+1,sizeof(encoded)-2);
			assert(out>0 && out<=WACCA_MAX_ENCODED && encoded[0] == 0xA5 && encoded[out+1] == 0xA5);
			assert(encoded[out] == 0);
			wacca_decoder_reset(&decoder); assert(decode(encoded+1,out,&m));
			assert(m.type == WACCA_LED_FRAME && m.sequence == 0x1234 && m.length == n && memcmp(m.payload,payload,n) == 0);
			for (size_t i=1; i<out; ++i) { assert(encoded[i] != 0); }
			// Every bit corruption in a small frame must fail CRC/COBS.
			if (n == 7) {
				for (size_t i=1; i<out; ++i) {
					for (unsigned bit=0; bit<8; ++bit) {
						encoded[i] ^= (uint8_t)(1U<<bit); wacca_decoder_reset(&decoder);
						assert(!decode(encoded+1,out,&m)); encoded[i] ^= (uint8_t)(1U<<bit);
					}
				}
			}
		}
	}
	// Receive all 480 logical colors in one message; expand only at output.
	assert(WACCA_LOGICAL_LED_RGB_BYTES == 1440);
	assert(WACCA_LOGICAL_LED_RGBA_BYTES == 1920);
	assert(WACCA_MAX_PAYLOAD == WACCA_LOGICAL_LED_RGB_BYTES);
	assert(WACCA_LOGICAL_LED_RGBA_BYTES > WACCA_MAX_PAYLOAD);
	assert(WACCA_MAX_RAW == 1448);
	assert(WACCA_MAX_ENCODED == 1455);
	assert(wacca_message_encode(WACCA_LED_FRAME,1,payload,WACCA_LOGICAL_LED_RGBA_BYTES,encoded,sizeof(encoded)) == 0);
	assert(WACCA_LOGICAL_LEDS_TOTAL * WACCA_PHYSICAL_LEDS_PER_LOGICAL_LED == WACCA_PHYSICAL_LEDS_TOTAL);
	assert(WACCA_LED_RGB_BYTES_PER_SECTOR <= WACCA_MAX_PAYLOAD);
	assert(WACCA_LED_RGB_BYTES_TOTAL > WACCA_MAX_PAYLOAD);
	assert(wacca_message_encode(WACCA_LED_FRAME,1,payload,WACCA_LED_RGB_BYTES_TOTAL,encoded,sizeof(encoded)) == 0);
	assert(wacca_message_encode(1,1,NULL,1,encoded,sizeof(encoded)) == 0);
	assert(wacca_message_encode(1,1,payload,WACCA_MAX_PAYLOAD+1,encoded,sizeof(encoded)) == 0);
	assert(wacca_message_encode(1,1,payload,7,encoded,5) == 0);
	wacca_decoder_reset(&decoder);
	for (unsigned i=0; i<WACCA_MAX_ENCODED+50; ++i) { assert(!wacca_decoder_feed(&decoder,1,&m)); }
	assert(decoder.discard); assert(!wacca_decoder_feed(&decoder,0,&m)); assert(!decoder.discard);
	const size_t n=wacca_message_encode(WACCA_PING,0xFFFF,payload,7,encoded,sizeof(encoded));
	assert(decode(encoded,n,&m) && m.sequence == 0xFFFF); // recovered after overflow
	puts("COBS framing, CRC corruption and resynchronization passed"); return 0;
}

