#ifndef WACCA_MERCURY_CONFIG_H
#define WACCA_MERCURY_CONFIG_H
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>

enum { MERCURY_PORT_CHARS = 16 };
struct mercury_config
{
	wchar_t touch_port[MERCURY_PORT_CHARS];
	wchar_t led_port[MERCURY_PORT_CHARS];
	DWORD touch_timeout_ms;
	uint8_t test, service, coin, volume_up, volume_down;
};

bool mercury_port_path(const wchar_t *name, wchar_t *path, size_t capacity);
bool mercury_config_load(HINSTANCE module, struct mercury_config *config);
void mercury_log(const char *format, ...);
#endif
