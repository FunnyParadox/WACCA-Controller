#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct wacca_decoder decoder;
static uint32_t get32(const uint8_t *p) { return ((uint32_t)(p[0]) | ((uint32_t)(p[1]) << 8) | ((uint32_t)(p[2]) << 16) | ((uint32_t)(p[3]) << 24)); }
static bool port_name(const char *name,char *path,size_t capacity)
{
	if (strncmp(name, "COM", 3) != 0 || name[3] == '\0' || strlen(name)>7) return (false);
	for (const char *p = name + 3; *p; ++p) if (*p < '0' || *p > '9') return (false);

	char *end;
	const unsigned long number = strtoul(name + 3, &end, 10);
	if (*end || !number || number > 4096) return (false);

	const int count = snprintf(path, capacity, "\\\\.\\COM%lu", number);
	return (count > 0 && (size_t)(count) < capacity);
}

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE open_port(const char *path)
{
	HANDLE port = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (port == INVALID_HANDLE_VALUE) return (port);

	DCB state = {0};
	state.DCBlength = sizeof(state);
	if (!GetCommState(port,&state)) goto error;

	state.BaudRate = 115200;
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
	COMMTIMEOUTS timeout = { MAXDWORD, 0, 100, 0, 1000 };
	if (!SetCommState(port, &state) || !SetCommTimeouts(port, &timeout) || !PurgeComm(port,PURGE_RXCLEAR | PURGE_TXCLEAR)) goto error;
	return (port);

	error:
	{
		const DWORD error = GetLastError();
		CloseHandle(port);
		SetLastError(error);
		return (INVALID_HANDLE_VALUE);
	}
}

static inline int error(const char *message, int code)
{
	fputs(message, stderr);
	return (code);
}

int main(int argc,char **argv)
{
	if (argc != 2) return(error("Usage: wacca_probe COM<number>\nSelect the prototype's port in Windows Device Manager\n", 2));

	char path[32];
	if (!port_name(argv[1],path,sizeof(path))) return (error("Invalid port name, Use COM<number> format\n", 2));

	HANDLE port = open_port(path);
	if (port == INVALID_HANDLE_VALUE) return (error("Cannot open/configure port\n", 1));

	uint8_t request[32]; const uint16_t sequence=1;
	const size_t n=wacca_message_encode(WACCA_GET_INFO,sequence,NULL,0,request,sizeof(request));
	/* A delimiter first discards any incomplete message from a previous session. */
	const uint8_t delimiter=0; DWORD written=0;
	if (!WriteFile(port, &delimiter, 1, &written, NULL) || written != 1 || !WriteFile(port, request, (DWORD)(n), &written,NULL) || written != n)
	{
		char buffer[128]; snprintf(buffer,sizeof(buffer),"USB write failed (Windows Error Code: %lu).\n",(unsigned long)GetLastError());
		CloseHandle(port);
		return (error(buffer, 1));
	}
	const ULONGLONG start = GetTickCount64();
	while ((GetTickCount64() - start) < 3000)
	{
		uint8_t bytes[64];
		DWORD count = 0;

		if (!ReadFile(port, bytes, sizeof(bytes), &count, NULL))
		{
			char buffer[128]; snprintf(buffer,sizeof(buffer),"USB read failed (Windows Error Code: %lu).\n",(unsigned long)GetLastError());
			fputs(buffer,stderr); break;
		}
		for (DWORD i = 0; i < count; ++i)
		{
			struct wacca_message reply;
			if (!wacca_decoder_feed(&decoder, bytes[i], &reply) || reply.sequence != sequence) continue;

			if (reply.type != WACCA_RESPONSE || reply.length != 36 || reply.payload[0] != WACCA_GET_INFO)
			{
				CloseHandle(port);
				return (error("Unexpected controller reply\n", 1));
			}
			const uint8_t role = reply.payload[1];

			if (role != 1 && role != 2)
			{
				CloseHandle(port);
				return (error("Unknown controller role\n", 1));
			}
			printf("%s: WACCA-Compatible Controller %s MCU, firmware %u.%u\n", argv[1], role == 1 ? "touch" : "LED", (unsigned)(reply.payload[3]), (unsigned)(reply.payload[2]));
			printf("CPU: %lu Hz; boot stage: %lu; hub result: %lu\n", (unsigned long)get32(reply.payload + 4),(unsigned long)get32(reply.payload + 8),(unsigned long)get32(reply.payload + 12));
			printf("Pending sector IRQs: 0x%03lX; USB resets: %lu; USB errors: %lu; rejected messages: %lu\n", (unsigned long)get32(reply.payload + 16), (unsigned long)get32(reply.payload + 20), (unsigned long)get32(reply.payload + 24), (unsigned long)get32(reply.payload + 28));
			(unsigned long)get32(reply.payload + 32) ? printf("Diagnostics only\n") : printf("Capabilities: 0x%08lX\n", (unsigned long)get32(reply.payload + 32));
			CloseHandle(port);
			return (0);
		}
	}
	CloseHandle(port);
	return (error("No valid reply within 3 seconds. Check the selected port, firmware and hub bootstrap.\n", 1));
}

#else

_Static_assert(0, "Firmware probe utility is only supported on Windows");
int main(int argc,char **argv) { return (1); }

#endif

