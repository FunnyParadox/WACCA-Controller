#ifndef WACCA_UPDATE_CLIENT_H
#define WACCA_UPDATE_CLIENT_H
#include "update.h"

struct wacca_update_transport
{
	void *ctx;
	bool (*exchange)(void *ctx, uint8_t command, const uint8_t *payload, size_t length, uint8_t reply[WACCA_UPDATE_REPLY_SIZE]);
	void (*progress)(void *ctx, uint32_t done, uint32_t total);
};
enum wacca_upload_result { WACCA_UPLOAD_OK, WACCA_UPLOAD_BAD_FILE, WACCA_UPLOAD_NO_BOOTLOADER, WACCA_UPLOAD_WRONG_DEVICE, WACCA_UPLOAD_TRANSFER_FAILED, WACCA_UPLOAD_DEVICE_ERROR };
// Check before sending even the application-to-updater reboot command.
bool wacca_update_file_role(const uint8_t *file, size_t length, enum wacca_role *role);
// Validates the entire file before the first transport operation, does not reset/run
enum wacca_upload_result wacca_upload(const uint8_t *file, size_t length, const struct wacca_update_transport *transport);

#endif
