#include "mercuryio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	assert(argc == 2);
	HMODULE dll = LoadLibraryExA(argv[1], NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	assert(dll);
	const char *exports[] = {
		"mercury_io_get_api_version", "mercury_io_init", "mercury_io_poll",
		"mercury_io_get_opbtns", "mercury_io_get_gamebtns", "mercury_io_touch_init",
		"mercury_io_touch_start", "mercury_io_touch_set_leds"
	};
	for (size_t i = 0; i < sizeof(exports) / sizeof(exports[0]); ++i) assert(GetProcAddress(dll, exports[i]));
	uint16_t (*version)(void);
	FARPROC symbol = GetProcAddress(dll, exports[0]);
	_Static_assert(sizeof(version) == sizeof(symbol), "Windows function pointer size");
	memcpy(&version, &symbol, sizeof(version));
	assert(version() == 0x0100);
	void (*set_leds)(struct led_data);
	symbol = GetProcAddress(dll, "mercury_io_touch_set_leds");
	memcpy(&set_leds, &symbol, sizeof(set_leds));
	// Exercise the 1924-byte by-value ABI without opening hardware or starting workers.
	struct led_data invalid = { .unitCount = 481 };
	set_leds(invalid);
	assert(FreeLibrary(dll));
	puts("x64 DLL loaded; eight API exports, version and by-value LED ABI verified");
	return 0;
}
