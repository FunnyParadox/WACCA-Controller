#include "config.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

void mercury_log(const char *format, ...)
{
	char text[512];
	va_list arguments;
	va_start(arguments, format);
	(void)vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	text[sizeof(text) - 1] = '\0';
	OutputDebugStringA("WACCA controller: ");
	OutputDebugStringA(text);
	fprintf(stderr, "WACCA controller: %s", text);
}

bool mercury_port_path(const wchar_t *name, wchar_t *path, size_t capacity)
{
	if (!name || !path || capacity < 12 || wcslen(name) < 4 || (name[0] != L'C' && name[0] != L'c') || (name[1] != L'O' && name[1] != L'o') || (name[2] != L'M' && name[2] != L'm')) return (false);
	unsigned number = 0;
	for (const wchar_t *p = name + 3; *p; ++p)
	{
		if (*p < L'0' || *p > L'9' || number > 4096U) return (false);
		number = number * 10U + (unsigned)(*p - L'0');
	}
	if (number == 0 || number > 4096U) return (false);
	const int written = swprintf(path, capacity, L"\\\\.\\COM%u", number);
	return written > 0 && (size_t)written < capacity;
}

static bool read_number(const wchar_t *path, const wchar_t *section, const wchar_t *key,
	const wchar_t *fallback, unsigned minimum, unsigned maximum, unsigned *value)
{
	wchar_t text[32], *end;
	const DWORD length = GetPrivateProfileStringW(section, key, fallback, text, 32, path);
	if (length == 0 || length >= 31) return (false);

	errno = 0;
	const unsigned long parsed = wcstoul(text, &end, 0);
	if (errno || *end || parsed < minimum || parsed > maximum) return (false);
	*value = (unsigned)parsed;
	return (true);
}

bool mercury_config_load(HINSTANCE module, struct mercury_config *config)
{
	// Find the config file
	wchar_t path[1024];
	const DWORD length = GetModuleFileNameW(module, path, 1024);
	if (length == 0 || length >= 1024) return (false);

	wchar_t *filename = wcsrchr(path, L'\\');
	if (!filename || (size_t)(filename + 1 - path) + wcslen(L"mercuryio.ini") >= 1024) return (false);

	wcscpy(filename + 1, L"mercuryio.ini");
	*config = (struct mercury_config){0};

	// Read the touch and LED ports
	const DWORD touch_length = GetPrivateProfileStringW(L"controller", L"touchPort", L"", config->touch_port, MERCURY_PORT_CHARS, path);
	const DWORD led_length = GetPrivateProfileStringW(L"controller", L"ledPort", L"", config->led_port, MERCURY_PORT_CHARS, path);
	{
		wchar_t validation[32];
		if (touch_length >= MERCURY_PORT_CHARS - 1 || led_length >= MERCURY_PORT_CHARS - 1 || (touch_length && !mercury_port_path(config->touch_port, validation, 32)) || (led_length && !mercury_port_path(config->led_port, validation, 32))) return (false);
		if (touch_length && led_length)
		{
			wchar_t other[32];
			if (!mercury_port_path(config->touch_port, other, 32) || wcscmp(other, validation) == 0) return (false);
		}
	}
	// Read the timeout for touch state polling (milliseconds)
	{
		unsigned timeout;
		if (!read_number(path, L"controller", L"touchTimeoutMs", L"100", 10, 1000, &timeout)) return (false);
		config->touch_timeout_ms = (DWORD)timeout;
	}
	// Read the keyboard mapping values
	struct { const wchar_t *key, *fallback; uint8_t *destination; } keys[] = 
	{
		{L"test", L"0x70", &config->test}, {L"service", L"0x71", &config->service},
		{L"coin", L"0x72", &config->coin}, {L"volup", L"0x26", &config->volume_up},
		{L"voldown", L"0x28", &config->volume_down}
	};
	unsigned key;
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
	{
		if (!read_number(path, L"keyboard", keys[i].key, keys[i].fallback, 0, 255, &key)) return (false);
		*keys[i].destination = (uint8_t)(key);
	}
	return (true);
}
