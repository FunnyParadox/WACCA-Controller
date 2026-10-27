#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "update_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct serial { HANDLE handle; uint16_t sequence; struct wacca_decoder decoder; };
static HANDLE open_port(const char *name)
{
	if (strncmp(name, "COM", 3) || strlen(name) < 4 || strlen(name) > 7) return (INVALID_HANDLE_VALUE);
	for (const char *p = name + 3; *p; ++p) if (*p < '0' || *p > '9') return (INVALID_HANDLE_VALUE);
	const unsigned long number = strtoul(name + 3, NULL, 10);
	if (!number || number > 4096) return (INVALID_HANDLE_VALUE);

	char path[32];
	(void)snprintf(path, sizeof(path), "\\\\.\\COM%lu", number);
	HANDLE port = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (port == INVALID_HANDLE_VALUE) return port;

	DCB config = {0};
	config.DCBlength = sizeof(config);
	if (!GetCommState(port, &config)) goto failed;

	config.BaudRate = 115200;
	config.ByteSize = 8;
	config.Parity = NOPARITY;
	config.StopBits = ONESTOPBIT;
	config.fBinary = TRUE;
	config.fParity = FALSE;
	config.fOutxCtsFlow = FALSE;
	config.fOutxDsrFlow = FALSE;
	config.fDtrControl = DTR_CONTROL_ENABLE;
	config.fDsrSensitivity = FALSE;
	config.fTXContinueOnXoff = TRUE;
	config.fOutX = FALSE;
	config.fInX = FALSE;
	config.fErrorChar = FALSE;
	config.fNull = FALSE;
	config.fRtsControl = RTS_CONTROL_ENABLE;
	config.fAbortOnError = FALSE;

	COMMTIMEOUTS timeouts = { MAXDWORD, 0, 100, 0, 3000 };
	if (!SetCommState(port, &config) || !SetCommTimeouts(port, &timeouts) || !PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR)) goto failed;
	return (port);

	failed:
	{
		CloseHandle(port);
		return (INVALID_HANDLE_VALUE);
	}
}
static bool write_all(HANDLE port, const uint8_t *bytes, size_t length)
{
	while (length)
	{
		DWORD count = 0;
		if (!WriteFile(port, bytes, (DWORD)length, &count, NULL) || !count || count > length) return (false);
		bytes += count;
		length -= count;
	}
	return (true);
}
static bool request(struct serial *port, uint8_t command, const uint8_t *payload, size_t length,
	uint8_t *reply, size_t reply_length, ULONGLONG timeout)
{
	uint8_t wire[WACCA_MAX_ENCODED];
	const uint16_t sequence = ++port->sequence;
	const size_t n = wacca_message_encode(command, sequence, payload, length, wire, sizeof(wire));
	if (!n || !write_all(port->handle, wire, n)) return (false);

	const ULONGLONG began = GetTickCount64();
	while (GetTickCount64() - began < timeout)
	{
		uint8_t bytes[64];
		DWORD count = 0;
		if (!ReadFile(port->handle, bytes, sizeof(bytes), &count, NULL)) return (false);
		for (DWORD i = 0; i < count; ++i)
		{
			struct wacca_message message;
			if (!wacca_decoder_feed(&port->decoder, bytes[i], &message) || message.sequence != sequence) continue;
			if (message.type != WACCA_RESPONSE || message.length != reply_length || message.payload[0] != command) return (false);
			memcpy(reply, message.payload, reply_length);
			return (true);
		}
	}
	return (false);
}
static bool exchange(void *ctx, uint8_t command, const uint8_t *payload, size_t length, uint8_t reply[WACCA_UPDATE_REPLY_SIZE])
{
	if (!request(ctx, command, payload, length, reply, WACCA_UPDATE_REPLY_SIZE, 10000)) return (false);
	if (reply[1] != WACCA_UPDATE_OK)
		fprintf(stderr, "Controller rejected command 0x%02X (status %u)\n", command, reply[1]);
	return (true);
}
static bool prepare_updater(struct serial *port, const char *name, enum wacca_role role)
{
	uint8_t reply[WACCA_UPDATE_REPLY_SIZE];
	if (request(port, WACCA_UPDATE_INFO, NULL, 0, reply, sizeof(reply), 1500))
		return (reply[1] == WACCA_UPDATE_OK && reply[2] == (uint8_t)role);

	uint8_t info[36];
	if (!request(port, WACCA_GET_INFO, NULL, 0, info, sizeof(info), 1500) ||
		info[1] != (uint8_t)role || !(wacca_get_u32(info + 32) & WACCA_CAP_USB_UPDATE)) return (false);
	const uint8_t target = (uint8_t)role;
	uint8_t accepted[2];
	if (!request(port, WACCA_ENTER_UPDATE, &target, 1, accepted, sizeof(accepted), 1500) ||
		accepted[1] != target) return (false);

	CloseHandle(port->handle);
	port->handle = INVALID_HANDLE_VALUE;
	puts("Restarting selected MCU into its USB updater...");
	const ULONGLONG began = GetTickCount64();
	while (GetTickCount64() - began < 15000)
	{
		Sleep(250);
		port->handle = open_port(name);
		if (port->handle == INVALID_HANDLE_VALUE) continue;
		wacca_decoder_reset(&port->decoder);
		const uint8_t delimiter = 0;
		if (write_all(port->handle, &delimiter, 1) &&
			request(port, WACCA_UPDATE_INFO, NULL, 0, reply, sizeof(reply), 1000) &&
			reply[1] == WACCA_UPDATE_OK && reply[2] == target) return (true);
		CloseHandle(port->handle);
		port->handle = INVALID_HANDLE_VALUE;
	}
	return (false);
}
static void progress(void *ctx, uint32_t done, uint32_t total)
{
	(void)ctx;
	printf("\rWriting application: %lu / %lu bytes", (unsigned long)done, (unsigned long)total);
	fflush(stdout);
}
int main(int argc, char **argv)
{
	if (argc != 3)
	{
		fputs("Usage: wacca_flash COM<number> touch.wcfu|led.wcfu\nClose the game; the updater restarts the selected MCU automatically\nFor a blank chip/factory recovery, use flash.ps1 <touch|led> -Factory\n", stderr);
		return (2);
	}
	FILE *file = fopen(argv[2], "rb");
	if (!file) { perror("Cannot open update package"); return 1; }
	uint8_t *image = malloc(WACCA_APP_CAPACITY + WACCA_IMAGE_HEADER_SIZE + 1U);
	if (!image) { fclose(file); return 1; }
	const size_t length = fread(image, 1, WACCA_APP_CAPACITY + WACCA_IMAGE_HEADER_SIZE + 1U, file);
	const bool read_ok = !ferror(file) && length <= WACCA_APP_CAPACITY + WACCA_IMAGE_HEADER_SIZE;
	fclose(file);
	if (!read_ok) { free(image); fputs("Invalid or oversized update package\n", stderr); return 1; }
	enum wacca_role role;
	if (!wacca_update_file_role(image, length, &role))
	{
		free(image);
		fputs("Invalid update package (role, layout, length or CRC); nothing was sent\n", stderr);
		return (1);
	}
	struct serial port = { .handle = open_port(argv[1]) };
	if (port.handle == INVALID_HANDLE_VALUE)
	{
		free(image);
		fputs("Cannot open COM port, select its current number in Device Manager and close the game/probe\n", stderr);
		return 1;
	}
	const uint8_t delimiter = 0;
	struct wacca_update_transport transport = { &port, exchange, progress };
	enum wacca_upload_result result = WACCA_UPLOAD_TRANSFER_FAILED;
	if (write_all(port.handle, &delimiter, 1) && prepare_updater(&port, argv[1], role))
		result = wacca_upload(image, length, &transport);
	free(image);
	fputc('\n', stdout);
	if (result != WACCA_UPLOAD_OK)
	{
		fprintf(stderr, "Update stopped (reason %u), check file, MCU selection and current COM port\n", (unsigned)result);
		fputs("If the COM number changed, rerun with the new number; an interrupted update stays recoverable\nUse UPDATE + RESET only for factory USB recovery (flash.ps1 <touch|led> -Factory)\n", stderr);
		if (port.handle != INVALID_HANDLE_VALUE) CloseHandle(port.handle);
		return 1;
	}
	uint8_t reply[WACCA_UPDATE_REPLY_SIZE];
	const bool ran = exchange(&port, WACCA_UPDATE_RUN, NULL, 0, reply) && reply[1] == WACCA_UPDATE_OK;
	if (port.handle != INVALID_HANDLE_VALUE) CloseHandle(port.handle);
	puts(ran ? "Verified and committed, controller restarting..." : "Verified and committed, tap RESET to start the application");
	return (0);
}
