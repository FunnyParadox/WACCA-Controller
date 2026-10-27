/* The production worker and entry-point code runs here with simulated CDC I/O.
 * No game, serial device, profile file or DLL installation is used by this test. */
#include "mercuryio.h"
#include "config.h"
#include "serial.h"
#include "usb_control.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static SRWLOCK mock_lock = SRWLOCK_INIT;
static uint8_t queued[WACCA_MAX_ENCODED], delivered[WACCA_LOGICAL_LED_RGB_BYTES];
static size_t queued_length;
static bool observed[MERCURY_TOUCH_COUNT];
static HANDLE led_arrived, led_ack, touch_arrived;
static volatile LONG led_count, callback_count, touch_opens, disconnect_touch;

bool mercury_config_load(HINSTANCE module, struct mercury_config *config)
{
	(void)module;
	*config = (struct mercury_config){ .touch_timeout_ms = 500 };
	wcscpy(config->touch_port, L"COM1");
	wcscpy(config->led_port, L"COM2");
	return true;
}
void mercury_log(const char *format, ...)
{
	(void)format;
}
bool mercury_serial_open(struct mercury_serial *serial, const wchar_t *port, uint8_t role, uint32_t capability)
{
	assert((role == WACCA_ROLE_TOUCH && capability == WACCA_CAP_TOUCH_STATE && wcscmp(port, L"COM1") == 0) ||
		(role == WACCA_ROLE_LED && capability == WACCA_CAP_LED_FRAME && wcscmp(port, L"COM2") == 0));
	*serial = (struct mercury_serial){ .handle = (HANDLE)(uintptr_t)role };
	if (role == WACCA_ROLE_TOUCH) InterlockedIncrement(&touch_opens);
	return true;
}
void mercury_serial_close(struct mercury_serial *serial) { serial->handle = INVALID_HANDLE_VALUE; }
bool mercury_serial_read(struct mercury_serial *serial, uint8_t *bytes, DWORD capacity, DWORD *count)
{
	assert((uintptr_t)serial->handle == WACCA_ROLE_TOUCH);
	if (InterlockedCompareExchange(&disconnect_touch, 0, 0)) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return false; }
	AcquireSRWLockExclusive(&mock_lock);
	const size_t limit = capacity < 7 ? capacity : 7; // Split COBS frames across reads.
	const size_t length = queued_length < limit ? queued_length : limit;
	memcpy(bytes, queued, length);
	memmove(queued, queued + length, queued_length - length);
	queued_length -= length;
	ReleaseSRWLockExclusive(&mock_lock);
	*count = (DWORD)length;
	if (!length) Sleep(1);
	return true;
}
bool mercury_serial_led_frame(struct mercury_serial *serial, const uint8_t rgb[WACCA_LOGICAL_LED_RGB_BYTES])
{
	assert((uintptr_t)serial->handle == WACCA_ROLE_LED);
	AcquireSRWLockExclusive(&mock_lock);
	memcpy(delivered, rgb, sizeof(delivered));
	ReleaseSRWLockExclusive(&mock_lock);
	InterlockedIncrement(&led_count);
	SetEvent(led_arrived);
	assert(WaitForSingleObject(led_ack, 5000) == WAIT_OBJECT_0);
	return true;
}

static void callback(const bool *state)
{
	AcquireSRWLockExclusive(&mock_lock);
	memcpy(observed, state, sizeof(observed));
	ReleaseSRWLockExclusive(&mock_lock);
	InterlockedIncrement(&callback_count);
	SetEvent(touch_arrived);
}
static void wait_touch(bool pressed)
{
	const ULONGLONG began = GetTickCount64();
	for (;;)
	{
		AcquireSRWLockShared(&mock_lock);
		const bool matches = observed[239] == pressed;
		ReleaseSRWLockShared(&mock_lock);
		if (matches) return;
		assert(GetTickCount64() - began < 3000);
		WaitForSingleObject(touch_arrived, 10);
	}
}
static void send_touch(uint16_t sequence)
{
	uint8_t state[WACCA_TOUCH_STATE_BYTES] = {0};
	state[29] = 0x80;
	AcquireSRWLockExclusive(&mock_lock);
	assert(queued_length == 0);
	queued_length = wacca_message_encode(WACCA_TOUCH_STATE, sequence, state, sizeof(state), queued, sizeof(queued));
	assert(queued_length > 0);
	ReleaseSRWLockExclusive(&mock_lock);
}

int main(void)
{
	led_arrived = CreateEventW(NULL, FALSE, FALSE, NULL);
	led_ack = CreateEventW(NULL, FALSE, FALSE, NULL);
	touch_arrived = CreateEventW(NULL, FALSE, FALSE, NULL);
	assert(led_arrived && led_ack && touch_arrived);
	assert(mercury_io_init() == S_OK && mercury_io_init() == S_OK);
	assert(mercury_io_touch_init() == S_OK);
	mercury_io_touch_start(callback);
	mercury_io_touch_start(callback);
	assert(WaitForSingleObject(touch_arrived, 3000) == WAIT_OBJECT_0);
	const ULONGLONG opened = GetTickCount64();
	while (InterlockedCompareExchange(&touch_opens, 0, 0) == 0)
	{
		assert(GetTickCount64() - opened < 3000);
		Sleep(1);
	}
	assert(InterlockedCompareExchange(&touch_opens, 0, 0) == 1);
	send_touch(1);
	wait_touch(true);
	Sleep(20);
	const LONG callbacks = InterlockedCompareExchange(&callback_count, 0, 0);
	send_touch(1); // A duplicate must neither invoke the callback nor extend freshness.
	Sleep(20);
	assert(InterlockedCompareExchange(&callback_count, 0, 0) == callbacks);
	wait_touch(false); // No fresh reports: inputs are released automatically.
	const ULONGLONG reconnect = GetTickCount64();
	while (InterlockedCompareExchange(&touch_opens, 0, 0) < 2)
	{
		assert(GetTickCount64() - reconnect < 4000);
		Sleep(1);
	}
	send_touch(0); // A new device session starts a new sequence history.
	wait_touch(true);
	InterlockedExchange(&disconnect_touch, 1);
	wait_touch(false);

	struct led_data frame = { .unitCount = 480 };
	for (unsigned i = 0; i < 480; ++i)
	{
		frame.rgba[4 * i] = 1;
		frame.rgba[4 * i + 1] = 2;
		frame.rgba[4 * i + 2] = 3;
		frame.rgba[4 * i + 3] = 255;
	}
	mercury_io_touch_set_leds(frame);
	assert(WaitForSingleObject(led_arrived, 3000) == WAIT_OBJECT_0);
	// The USB worker is blocked waiting for an ACK. Game callbacks must still finish.
	for (unsigned version = 4; version <= 100; ++version)
	{
		frame.rgba[0] = (uint8_t)version;
		mercury_io_touch_set_leds(frame);
	}
	assert(InterlockedCompareExchange(&led_count, 0, 0) == 1);
	SetEvent(led_ack);
	assert(WaitForSingleObject(led_arrived, 3000) == WAIT_OBJECT_0);
	AcquireSRWLockShared(&mock_lock);
	assert(delivered[0] == 100 && delivered[1] == 2 && delivered[2] == 3);
	for (unsigned i = 1; i < 480; ++i) assert(delivered[3 * i] == 1 && delivered[3 * i + 1] == 2 && delivered[3 * i + 2] == 3);
	ReleaseSRWLockShared(&mock_lock);
	assert(InterlockedCompareExchange(&led_count, 0, 0) == 2); // Obsolete intermediate frames were dropped.
	// A partial game update preserves the previous tail in the next full RGB transfer.
	frame.unitCount = 1;
	frame.rgba[0] = 42;
	mercury_io_touch_set_leds(frame);
	frame.unitCount = 481;
	mercury_io_touch_set_leds(frame);
	SetEvent(led_ack);
	assert(WaitForSingleObject(led_arrived, 3000) == WAIT_OBJECT_0);
	AcquireSRWLockShared(&mock_lock);
	assert(delivered[0] == 42 && delivered[3] == 1);
	ReleaseSRWLockShared(&mock_lock);
	// Leave the final simulated ACK blocked until process exit; workers own no game memory.
	puts("Workers: fresh touch callbacks, stale release, idempotent startup, RGB-only/latest-frame LED delivery passed");
	return 0;
}
