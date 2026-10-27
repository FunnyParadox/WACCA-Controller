#include "serial.h"
#include "config.h"
#include <string.h>

// Resolve real Windows entry points rather than using a serial API import that the game's cabinet-emulation hooks could redirect to virtual COM ports
struct mercury_win32_serial_api
{
	HANDLE (WINAPI *open)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
	BOOL (WINAPI *close)(HANDLE);
	BOOL (WINAPI *get_state)(HANDLE, LPDCB);
	BOOL (WINAPI *set_state)(HANDLE, LPDCB);
	BOOL (WINAPI *timeouts)(HANDLE, LPCOMMTIMEOUTS);
	BOOL (WINAPI *purge)(HANDLE, DWORD);
	BOOL (WINAPI *read)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
	BOOL (WINAPI *write)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
};
static struct mercury_win32_serial_api native_serial;
static INIT_ONCE native_initialization = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK load_native_api(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
	(void)once; (void)parameter; (void)context;
	HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
	if (!kernel) return (FALSE);

	#define RESOLVE(field, name) do { \
		FARPROC address = GetProcAddress(kernel, name); \
		if (!address) { SetLastError(ERROR_PROC_NOT_FOUND); return (FALSE); } \
		_Static_assert(sizeof(native_serial.field) == sizeof(address), "Windows procedure pointer size"); \
		memcpy(&native_serial.field, &address, sizeof(address)); \
	} while (0)

	RESOLVE(open, "CreateFileW");
	RESOLVE(close, "CloseHandle");
	RESOLVE(get_state, "GetCommState");
	RESOLVE(set_state, "SetCommState");
	RESOLVE(timeouts, "SetCommTimeouts");
	RESOLVE(purge, "PurgeComm");
	RESOLVE(read, "ReadFile");
	RESOLVE(write, "WriteFile");

	#undef RESOLVE
	return (TRUE);
}

void mercury_serial_close(struct mercury_serial *serial)
{
	if (serial->handle && serial->handle != INVALID_HANDLE_VALUE) native_serial.close(serial->handle);
	serial->handle = INVALID_HANDLE_VALUE;
}

bool mercury_serial_read(struct mercury_serial *serial, uint8_t *bytes, DWORD capacity, DWORD *count)
{
	if (serial->pending_count)
	{
		*count = serial->pending_count < capacity ? serial->pending_count : capacity;
		memcpy(bytes, serial->pending, *count);
		serial->pending_count -= *count;
		memmove(serial->pending, serial->pending + *count, serial->pending_count);
		return (true);
	}
	return (native_serial.read(serial->handle, bytes, capacity, count, NULL) != 0);
}

static bool write_bytes(struct mercury_serial *serial, const uint8_t *bytes, size_t length)
{
	const ULONGLONG began = GetTickCount64();
	while (length)
	{
		DWORD count = 0;
		if (!native_serial.write(serial->handle, bytes, (DWORD)length, &count, NULL)) return (false);
		if (!count || count > length) { SetLastError(ERROR_WRITE_FAULT); return (false); }
		bytes += count;
		length -= count;
		if (length && GetTickCount64() - began >= 100) { SetLastError(ERROR_TIMEOUT); return (false); }
	}
	return (true);
}

static bool send_message(struct mercury_serial *serial, uint8_t type, const uint8_t *payload, size_t length)
{
	uint8_t wire[WACCA_MAX_ENCODED];
	const size_t count = wacca_message_encode(type, ++serial->sequence, payload, length, wire, sizeof(wire));
	if (!count) { SetLastError(ERROR_INVALID_DATA); return (false); }
	return (write_bytes(serial, wire, count));
}

static void retain_tail(struct mercury_serial *serial, const uint8_t *bytes, DWORD count)
{
	// A USB read can contain a command reply followed by the first touch report.
	memcpy(serial->pending, bytes, count);
	serial->pending_count = count;
}

static bool wait_reply(struct mercury_serial *serial, bool info, uint8_t role, uint32_t capability)
{
	const ULONGLONG began = GetTickCount64();
	while (GetTickCount64() - began < 500)
	{
		uint8_t bytes[256];
		DWORD count;
		if (!mercury_serial_read(serial, bytes, sizeof(bytes), &count)) return (false);
		for (DWORD i = 0; i < count; ++i)
		{
			struct wacca_message message;
			if (!wacca_decoder_feed(&serial->decoder, bytes[i], &message) || message.sequence != serial->sequence) continue;
			if (message.type == WACCA_ERROR) { SetLastError(ERROR_NOT_SUPPORTED); return (false); }
			if (info)
			{
				if (mercury_info_matches(&message, serial->sequence, role, capability))
				{
					retain_tail(serial, bytes + i + 1, count - i - 1);
					return (true);
				}
				if (message.type == WACCA_RESPONSE && message.length == 36 && message.payload[0] == WACCA_GET_INFO)
				{
					SetLastError(message.payload[1] != role ? ERROR_BAD_UNIT : ERROR_NOT_SUPPORTED);
					return (false);
				}
			}
			else if (message.type == WACCA_RESPONSE && message.length == 1 && message.payload[0] == WACCA_LED_FRAME)
			{
				retain_tail(serial, bytes + i + 1, count - i - 1);
				return (true);
			}
		}
	}
	SetLastError(ERROR_TIMEOUT);
	return (false);
}

bool mercury_serial_open(struct mercury_serial *serial, const wchar_t *port, uint8_t role, uint32_t capability)
{
	wchar_t path[32];
	*serial = (struct mercury_serial){ .handle = INVALID_HANDLE_VALUE };
	if (!InitOnceExecuteOnce(&native_initialization, load_native_api, NULL, NULL)) return (false);
	if (!mercury_port_path(port, path, 32)) { SetLastError(ERROR_INVALID_PARAMETER); return (false); }
	serial->handle = native_serial.open(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (serial->handle == INVALID_HANDLE_VALUE) return (false);
	DCB state = {0};
	state.DCBlength = sizeof(state);
	if (!native_serial.get_state(serial->handle, &state)) goto failed;

	state.BaudRate = 115200; // CDC metadata; no UART limits the USB transfer rate
	state.ByteSize = 8;
	state.Parity = NOPARITY;
	state.StopBits = ONESTOPBIT;
	state.fBinary = TRUE;
	state.fParity = FALSE;
	state.fOutxCtsFlow = FALSE;
	state.fOutxDsrFlow = FALSE;
	state.fDtrControl = DTR_CONTROL_ENABLE;
	state.fDsrSensitivity = FALSE;
	state.fTXContinueOnXoff = TRUE;
	state.fOutX = FALSE;
	state.fInX = FALSE;
	state.fErrorChar = FALSE;
	state.fNull = FALSE;
	state.fRtsControl = RTS_CONTROL_ENABLE;
	state.fAbortOnError = FALSE;

	// Return as soon as USB bytes arrive, rather than accumulating a 5 ms batch
	const uint8_t delimiter = 0;
	COMMTIMEOUTS timeouts = {MAXDWORD, MAXDWORD, 5, 0, 100};
	if (!native_serial.set_state(serial->handle, &state)) goto failed;
	if (!native_serial.timeouts(serial->handle, &timeouts)) goto failed;
	if (!native_serial.purge(serial->handle, PURGE_RXCLEAR | PURGE_TXCLEAR)) goto failed;
	if (!write_bytes(serial, &delimiter, 1)) goto failed;
	if (!send_message(serial, WACCA_GET_INFO, NULL, 0)) goto failed;
	if (!wait_reply(serial, true, role, capability)) goto failed;
	return (true);

	failed:
	{
		const DWORD error = GetLastError();
		mercury_serial_close(serial);
		SetLastError(error);
		return (false);
	}
}

bool mercury_serial_led_frame(struct mercury_serial *serial, const uint8_t rgb[WACCA_LOGICAL_LED_RGB_BYTES])
{
	return (send_message(serial, WACCA_LED_FRAME, rgb, WACCA_LOGICAL_LED_RGB_BYTES) && wait_reply(serial, false, 0, 0));
}
