#ifndef WACCA_UPDATE_H
#define WACCA_UPDATE_H
#include "protocol.h"
#include "usb_control.h"
#include "image_layout.h"

// WCFU: Wacca-Compatible Controller Firmware Update; four zero padding bytes.
#define WACCA_IMAGE_MAGIC "WCFU\0\0\0"
#define WACCA_UPDATE_REPLY_SIZE 24
#define WACCA_IMAGE_MIN_SIZE 512

enum wacca_update_command {
	WACCA_UPDATE_INFO = 0x30, WACCA_UPDATE_BEGIN, WACCA_UPDATE_DATA,
	WACCA_UPDATE_FINISH, WACCA_UPDATE_RUN
};
enum wacca_update_status {
	WACCA_UPDATE_OK, WACCA_UPDATE_BAD_COMMAND, WACCA_UPDATE_BAD_IMAGE,
	WACCA_UPDATE_WRONG_ROLE, WACCA_UPDATE_BAD_STATE, WACCA_UPDATE_BAD_OFFSET,
	WACCA_UPDATE_FLASH_ERROR, WACCA_UPDATE_BAD_CRC
};
enum wacca_update_state { WACCA_UPDATE_IDLE, WACCA_UPDATE_WRITING, WACCA_UPDATE_COMMITTED };
// Offsets are relative to flash, never arbitrary MCU addresses.
struct wacca_flash_io {
	void *ctx;
	bool (*read)(void *ctx, uint32_t offset, uint8_t *out, size_t length);
	bool (*erase)(void *ctx, uint32_t offset, size_t length);
	bool (*write)(void *ctx, uint32_t offset, const uint8_t *data, size_t length);
};
struct wacca_update {
	struct wacca_flash_io flash;
	enum wacca_role role;
	enum wacca_update_state state;
	uint8_t header[WACCA_IMAGE_HEADER_SIZE];
	uint32_t size, expected_crc, received;
	bool run_requested;
};
// Shared fixed-address NOLOAD RAM survives a software reset, never a flash write.
struct wacca_boot_request { uint32_t magic, role, inverse_magic, inverse_role; };
_Static_assert(sizeof(struct wacca_boot_request) == WACCA_BOOT_REQUEST_SIZE, "Boot request layout");
void wacca_boot_request_store(volatile struct wacca_boot_request *request, enum wacca_role role);
bool wacca_boot_request_take(volatile struct wacca_boot_request *request, enum wacca_role role, bool software_reset);
uint32_t wacca_get_u32(const uint8_t *p);
void wacca_put_u32(uint8_t *p, uint32_t value);
uint32_t wacca_crc32_update(uint32_t crc, const uint8_t *data, size_t length);
bool wacca_image_header(const uint8_t *header, size_t length, enum wacca_role role);
void wacca_update_init(struct wacca_update *update, enum wacca_role role, const struct wacca_flash_io *flash);
bool wacca_update_app_valid(const struct wacca_update *update);
// Returns a fixed-size reply payload; the caller adds the usual framed transport.
void wacca_update_handle(struct wacca_update *update, const struct wacca_message *message,
	uint8_t reply[WACCA_UPDATE_REPLY_SIZE]);
#endif
