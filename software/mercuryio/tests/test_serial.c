/* Exercise the real CDC transport against simulated Win32 serial calls. */
#include "serial.h"
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct wacca_decoder mock_decoder;
static uint8_t incoming[4096], last_rgb[WACCA_LOGICAL_LED_RGB_BYTES];
static size_t incoming_length;
static uint8_t device_role = 1;
static uint32_t device_capability = WACCA_CAP_TOUCH_STATE;
static unsigned closes, frames;
static bool fail_write;

static void append_message(uint8_t type, uint16_t sequence, const uint8_t *payload, size_t length)
{
	const size_t count = wacca_message_encode(type, sequence, payload, length,
		incoming + incoming_length, sizeof(incoming) - incoming_length);
	assert(count);
	incoming_length += count;
}
static HANDLE WINAPI fake_open(LPCWSTR path, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
	DWORD creation, DWORD attributes, HANDLE template_file)
{
	(void)access; (void)sharing; (void)security; (void)attributes; (void)template_file;
	assert(wcscmp(path, L"\\\\.\\COM1") == 0 && creation == OPEN_EXISTING);
	mock_decoder = (struct wacca_decoder){0};
	incoming_length = 0;
	return (HANDLE)(uintptr_t)123;
}
static BOOL WINAPI fake_close(HANDLE handle) { assert((uintptr_t)handle == 123); ++closes; return TRUE; }
static BOOL WINAPI fake_get_state(HANDLE handle, LPDCB state) { (void)handle; assert(state->DCBlength == sizeof(*state)); return TRUE; }
static BOOL WINAPI fake_set_state(HANDLE handle, LPDCB state)
{
	(void)handle;
	assert(state->fDtrControl == DTR_CONTROL_ENABLE && state->fOutX == FALSE && state->fInX == FALSE);
	return TRUE;
}
static BOOL WINAPI fake_timeouts(HANDLE handle, LPCOMMTIMEOUTS timeouts)
{
	(void)handle;
	assert(timeouts->ReadIntervalTimeout == MAXDWORD && timeouts->ReadTotalTimeoutMultiplier == MAXDWORD);
	assert(timeouts->ReadTotalTimeoutConstant == 5 && timeouts->WriteTotalTimeoutConstant == 100);
	return TRUE;
}
static BOOL WINAPI fake_purge(HANDLE handle, DWORD flags) { (void)handle; assert(flags == (PURGE_RXCLEAR | PURGE_TXCLEAR)); return TRUE; }
static BOOL WINAPI fake_read(HANDLE handle, LPVOID output, DWORD capacity, LPDWORD count, LPOVERLAPPED overlapped)
{
	(void)handle; (void)overlapped;
	const size_t length = incoming_length < capacity ? incoming_length : capacity;
	memcpy(output, incoming, length);
	memmove(incoming, incoming + length, incoming_length - length);
	incoming_length -= length;
	*count = (DWORD)length;
	if (!length) Sleep(1);
	return TRUE;
}
static BOOL WINAPI fake_write(HANDLE handle, LPCVOID input, DWORD length, LPDWORD count, LPOVERLAPPED overlapped)
{
	(void)handle; (void)overlapped;
	if (fail_write) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return FALSE; }
	const uint8_t *bytes = input;
	*count = length < 13 ? length : 13; // Force partial writes of a large RGB frame.
	for (DWORD i = 0; i < *count; ++i)
	{
		struct wacca_message message;
		if (!wacca_decoder_feed(&mock_decoder, bytes[i], &message)) continue;
		if (message.type == WACCA_GET_INFO)
		{
			uint8_t info[36] = {WACCA_GET_INFO, device_role};
			info[32] = (uint8_t)device_capability;
			append_message(WACCA_RESPONSE, message.sequence, info, sizeof(info));
			if (device_role == 1)
			{
				uint8_t touch[WACCA_TOUCH_STATE_BYTES] = {0};
				touch[29] = 0x80;
				append_message(WACCA_TOUCH_STATE, 77, touch, sizeof(touch));
			}
		}
		else
		{
			assert(message.type == WACCA_LED_FRAME && message.length == 1440);
			memcpy(last_rgb, message.payload, sizeof(last_rgb));
			++frames;
			const uint8_t acknowledged = WACCA_LED_FRAME;
			append_message(WACCA_RESPONSE, (uint16_t)(message.sequence - 1), &acknowledged, 1); // Ignore an old ACK.
			append_message(WACCA_RESPONSE, message.sequence, &acknowledged, 1);
		}
	}
	return TRUE;
}

static HMODULE WINAPI fake_kernel(LPCWSTR name)
{
	assert(wcscmp(name, L"kernel32.dll") == 0);
	return (HMODULE)(uintptr_t)1;
}
static FARPROC WINAPI fake_lookup(HMODULE module, LPCSTR name);
#define GetModuleHandleW fake_kernel
#define GetProcAddress fake_lookup
#include "../src/serial.c"

static FARPROC WINAPI fake_lookup(HMODULE module, LPCSTR name)
{
	assert((uintptr_t)module == 1);
	const struct mercury_win32_serial_api fakes = {
		fake_open, fake_close, fake_get_state, fake_set_state,
		fake_timeouts, fake_purge, fake_read, fake_write
	};
	const struct { const char *name; size_t offset; } functions[] = {
		{"CreateFileW", offsetof(struct mercury_win32_serial_api, open)},
		{"CloseHandle", offsetof(struct mercury_win32_serial_api, close)},
		{"GetCommState", offsetof(struct mercury_win32_serial_api, get_state)},
		{"SetCommState", offsetof(struct mercury_win32_serial_api, set_state)},
		{"SetCommTimeouts", offsetof(struct mercury_win32_serial_api, timeouts)},
		{"PurgeComm", offsetof(struct mercury_win32_serial_api, purge)},
		{"ReadFile", offsetof(struct mercury_win32_serial_api, read)},
		{"WriteFile", offsetof(struct mercury_win32_serial_api, write)}
	};
	for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i)
	{
		if (strcmp(name, functions[i].name)) continue;
		FARPROC result;
		memcpy(&result, (const uint8_t *)&fakes + functions[i].offset, sizeof(result));
		return result;
	}
	assert(false);
	return NULL;
}

int main(void)
{
	struct mercury_serial serial;
	assert(mercury_serial_open(&serial, L"COM1", 1, WACCA_CAP_TOUCH_STATE));
	// GET_INFO and TOUCH_STATE arrived in one read. Neither message may be lost.
	uint8_t bytes[256];
	DWORD count;
	assert(mercury_serial_read(&serial, bytes, sizeof(bytes), &count) && count > 0);
	struct mercury_touch_sample sample = {0};
	unsigned reports = 0;
	for (DWORD i = 0; i < count; ++i)
	{
		struct wacca_message message;
		if (wacca_decoder_feed(&serial.decoder, bytes[i], &message) && mercury_touch_apply(&sample, &message)) ++reports;
	}
	assert(reports == 1 && sample.sequence == 77 && sample.state[239]);
	mercury_serial_close(&serial);
	assert(closes == 1);

	device_capability = 0;
	assert(!mercury_serial_open(&serial, L"COM1", 1, WACCA_CAP_TOUCH_STATE));
	assert(GetLastError() == ERROR_NOT_SUPPORTED && closes == 2);
	device_role = 2;
	device_capability = WACCA_CAP_LED_FRAME;
	assert(!mercury_serial_open(&serial, L"COM1", 1, WACCA_CAP_TOUCH_STATE));
	assert(GetLastError() == ERROR_BAD_UNIT && closes == 3);
	assert(mercury_serial_open(&serial, L"COM1", 2, WACCA_CAP_LED_FRAME));
	uint8_t rgb[WACCA_LOGICAL_LED_RGB_BYTES];
	for (size_t i = 0; i < sizeof(rgb); ++i) rgb[i] = (uint8_t)i;
	assert(mercury_serial_led_frame(&serial, rgb));
	assert(frames == 1 && memcmp(last_rgb, rgb, sizeof(rgb)) == 0);
	fail_write = true;
	assert(!mercury_serial_led_frame(&serial, rgb));
	assert(GetLastError() == ERROR_DEVICE_NOT_CONNECTED && frames == 1);
	mercury_serial_close(&serial);
	puts("CDC transport: timeouts, partial writes, handshake roles/capabilities, retained reports and ACK matching passed");
	return 0;
}
