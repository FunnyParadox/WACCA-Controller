#ifndef WACCA_MERCURY_SERIAL_H
#define WACCA_MERCURY_SERIAL_H
#include <windows.h>
#include "frame.h"

struct mercury_serial
{
	HANDLE handle;
	struct wacca_decoder decoder;
	uint16_t sequence;
	uint8_t pending[256];
	DWORD pending_count;
};

bool mercury_serial_open(struct mercury_serial *serial, const wchar_t *port, uint8_t role, uint32_t capability);
void mercury_serial_close(struct mercury_serial *serial);
bool mercury_serial_read(struct mercury_serial *serial, uint8_t *bytes, DWORD capacity, DWORD *count);
bool mercury_serial_led_frame(struct mercury_serial *serial, const uint8_t rgb[WACCA_LOGICAL_LED_RGB_BYTES]);
#endif
