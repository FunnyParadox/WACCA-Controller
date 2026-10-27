#include "mercuryio.h"
#include "config.h"
#include "frame.h"
#include "serial.h"
#include "usb_control.h"
#include <process.h>
#include <string.h>

static HINSTANCE module_instance;
static INIT_ONCE initialization = INIT_ONCE_STATIC_INIT;
static struct mercury_config config;
static SRWLOCK start_lock = SRWLOCK_INIT;
static SRWLOCK led_lock = SRWLOCK_INIT;
static HANDLE led_wake;
static bool led_started, touch_started;
static mercury_io_touch_callback_t touch_callback;
static volatile LONG button_snapshot;
static uint8_t led_rgb[WACCA_LOGICAL_LED_RGB_BYTES];
static uint64_t led_generation;

// The API has no stop callback
// Once workers exist, keep their code loaded until process exit
// DllMain never starts threads, performs I/O or waits on workers
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	(void)reserved;
	if (reason == DLL_PROCESS_ATTACH) module_instance = instance;
	return (TRUE);
}

static BOOL CALLBACK initialize(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
	(void)once; (void)parameter; (void)context;
	struct mercury_config loaded;
	if (!mercury_config_load(module_instance, &loaded))
	{
		mercury_log("Invalid mercuryio.ini: check COM ports, timeout and keyboard key codesn");
		SetLastError(ERROR_INVALID_DATA);
		return (FALSE);
	}
	HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!event) return (FALSE);

	config = loaded;
	led_wake = event;
	if (!config.led_port[0]) mercury_log("LED port is not configured; LED forwarding is disabled\n");
	return (TRUE);
}

static HRESULT ensure_initialized(void)
{
	if (InitOnceExecuteOnce(&initialization, initialize, NULL, NULL)) return (S_OK);
	const DWORD error = GetLastError();
	return (HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE));
}

static bool pin_module(void)
{
	if (!module_instance) return (true); // The integration test links this source into an EXE
	HMODULE pinned;
	return (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&module_instance, &pinned) != 0);
}

static void connection_error(const char *role, DWORD error, DWORD *previous)
{
	if (error != *previous)
	{
		mercury_log("%s connection unavailable (Windows error %lu), check the port and MCU capabilities\n", role, (unsigned long)error);
		*previous = error;
	}
}

static unsigned __stdcall touch_worker(void *context)
{
	(void)context;
	const bool released[MERCURY_TOUCH_COUNT] = { false };
	touch_callback(released);
	DWORD previous_error = 0;

	while (1)
	{
		struct mercury_serial serial;
		if (!mercury_serial_open(&serial, config.touch_port, WACCA_ROLE_TOUCH, WACCA_CAP_TOUCH_STATE))
		{
			connection_error("Touch", GetLastError(), &previous_error);
			Sleep(1000);
			continue;
		}
		previous_error = 0;
		mercury_log("Touch MCU connected.\n");
		struct mercury_touch_sample sample = {0};
		ULONGLONG last_fresh = GetTickCount64();

		while (1)
		{
			uint8_t bytes[512];
			DWORD count;
			if (!mercury_serial_read(&serial, bytes, sizeof(bytes), &count)) break;
			bool fresh = false;
			for (DWORD i = 0; i < count; ++i)
			{
				struct wacca_message message;
				if (wacca_decoder_feed(&serial.decoder, bytes[i], &message) && mercury_touch_apply(&sample, &message)) fresh = true;
			}
			// Coalesce reports already queued in this read; deliver the newest one
			if (fresh)
			{
				last_fresh = GetTickCount64();
				touch_callback(sample.state);
			}
			if (GetTickCount64() - last_fresh >= config.touch_timeout_ms)
			{
				SetLastError(ERROR_TIMEOUT);
				break;
			}
		}
		touch_callback(released); // A cable fault must not leave held game inputs
		mercury_serial_close(&serial);
		connection_error("Touch", GetLastError(), &previous_error);
		Sleep(1000);
	}
	return 0;
}

static unsigned __stdcall led_worker(void *context)
{
	(void)context;
	DWORD previous_error = 0;
	while (1)
	{
		struct mercury_serial serial;
		if (!mercury_serial_open(&serial, config.led_port, WACCA_ROLE_LED, WACCA_CAP_LED_FRAME))
		{
			connection_error("LED", GetLastError(), &previous_error);
			Sleep(1000);
			continue;
		}
		previous_error = 0;
		mercury_log("LED MCU connected\n");

		uint64_t delivered = 0;
		while (1)
		{
			uint8_t rgb[WACCA_LOGICAL_LED_RGB_BYTES];
			AcquireSRWLockShared(&led_lock);

			const uint64_t generation = led_generation;
			if (generation != delivered) memcpy(rgb, led_rgb, sizeof(rgb));
			ReleaseSRWLockShared(&led_lock);
			if (generation == delivered)
			{
				WaitForSingleObject(led_wake, INFINITE);
				continue;
			}
			if (!mercury_serial_led_frame(&serial, rgb)) break;
			delivered = generation;
		}
		mercury_serial_close(&serial);
		connection_error("LED", GetLastError(), &previous_error);
		Sleep(1000);
	}
	return (0);
}

static HRESULT start_led_worker(void)
{
	if (!config.led_port[0]) return (S_OK);
	AcquireSRWLockExclusive(&start_lock);

	HRESULT result = S_OK;
	if (!led_started)
	{
		const uintptr_t thread = pin_module() ? _beginthreadex(NULL, 0, led_worker, NULL, 0, NULL) : 0;
		if (thread)
		{
			led_started = true;
			CloseHandle((HANDLE)thread);
		}
		else result = E_FAIL;
	}
	ReleaseSRWLockExclusive(&start_lock);
	return (result);
}

uint16_t mercury_io_get_api_version(void) { return UINT16_C(0x0100); }

HRESULT mercury_io_init(void)
{
	const HRESULT result = ensure_initialized();
	return FAILED(result) ? result : start_led_worker();
}

static bool key_down(uint8_t key) { return key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0; }

HRESULT mercury_io_poll(void)
{
	const HRESULT result = ensure_initialized();
	if (FAILED(result)) return (result);

	LONG buttons = 0;
	if (key_down(config.test)) buttons |= 1;
	if (key_down(config.service)) buttons |= 2;
	if (key_down(config.coin)) buttons |= 4;
	if (key_down(config.volume_up)) buttons |= 1 << 8;
	if (key_down(config.volume_down)) buttons |= 2 << 8;
	InterlockedExchange(&button_snapshot, buttons);
	return (S_OK);
}

void mercury_io_get_opbtns(uint8_t *buttons) { if (buttons) *buttons = (uint8_t)InterlockedCompareExchange(&button_snapshot, 0, 0); }
void mercury_io_get_gamebtns(uint8_t *buttons) { if (buttons) *buttons = (uint8_t)(InterlockedCompareExchange(&button_snapshot, 0, 0) >> 8); }

HRESULT mercury_io_touch_init(void)
{
	const HRESULT result = ensure_initialized();
	if (FAILED(result)) return (result);

	if (!config.touch_port[0])
	{
		mercury_log("Set touchPort in mercuryio.ini before enabling the controller\n");
		return HRESULT_FROM_WIN32(ERROR_NOT_READY);
	}
	return (S_OK);
}

void mercury_io_touch_start(mercury_io_touch_callback_t callback)
{
	if (!callback || FAILED(mercury_io_touch_init())) return;

	AcquireSRWLockExclusive(&start_lock);
	if (!touch_started)
	{
		touch_callback = callback;
		const uintptr_t thread = pin_module() ? _beginthreadex(NULL, 0, touch_worker, NULL, 0, NULL) : 0;
		if (thread)
		{
			touch_started = true;
			CloseHandle((HANDLE)thread);
		}
		else mercury_log("Could not start the touch worker\n");
	}
	ReleaseSRWLockExclusive(&start_lock);
}

void mercury_io_touch_set_leds(struct led_data data)
{
	if (data.unitCount == 0 || data.unitCount > WACCA_LOGICAL_LEDS_TOTAL || FAILED(mercury_io_init())) return;
	if (!config.led_port[0]) return;

	AcquireSRWLockExclusive(&led_lock);
	if (mercury_led_apply(led_rgb, data.unitCount, data.rgba)) ++led_generation;
	ReleaseSRWLockExclusive(&led_lock);
	SetEvent(led_wake);
}
