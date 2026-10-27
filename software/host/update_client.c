#include "update_client.h"

static bool response(const uint8_t *reply, uint8_t command, enum wacca_role role)
{
	return (reply[0] == command && reply[1] == WACCA_UPDATE_OK && reply[2] == (uint8_t)role &&
			wacca_get_u32(reply + 8) == WACCA_APP_OFFSET &&
			wacca_get_u32(reply + 12) == WACCA_APP_CAPACITY &&
			wacca_get_u32(reply + 16) == WACCA_UPDATE_CHUNK &&
			wacca_get_u32(reply + 20) == 1);
}
bool wacca_update_file_role(const uint8_t *file, size_t length, enum wacca_role *role)
{
	if (!file || !role || length < WACCA_IMAGE_HEADER_SIZE) return (false);
	const uint32_t role_number = wacca_get_u32(file + 12);
	if (role_number != WACCA_ROLE_TOUCH && role_number != WACCA_ROLE_LED) return (false);
	const enum wacca_role selected = (enum wacca_role)role_number;
	if (!wacca_image_header(file, WACCA_IMAGE_HEADER_SIZE, selected)) return (false);
	const uint32_t size = wacca_get_u32(file + 20);
	if (length - WACCA_IMAGE_HEADER_SIZE != size ||
		~wacca_crc32_update(UINT32_MAX, file + WACCA_IMAGE_HEADER_SIZE, size) != wacca_get_u32(file + 24))
		return (false);
	*role = selected;
	return (true);
}

enum wacca_upload_result wacca_upload(const uint8_t *file, size_t length, const struct wacca_update_transport *t)
{
	enum wacca_role role;
	if (!t || !t->exchange || !wacca_update_file_role(file, length, &role)) return (WACCA_UPLOAD_BAD_FILE);
	const uint32_t size = wacca_get_u32(file + 20);

	uint8_t reply[WACCA_UPDATE_REPLY_SIZE];
	if (!t->exchange(t->ctx, WACCA_UPDATE_INFO, NULL, 0, reply)) return (WACCA_UPLOAD_NO_BOOTLOADER);
	if (reply[2] != (uint8_t)role) return (WACCA_UPLOAD_WRONG_DEVICE);
	if (!response(reply, WACCA_UPDATE_INFO, role)) return (WACCA_UPLOAD_DEVICE_ERROR);
	if (!t->exchange(t->ctx, WACCA_UPDATE_BEGIN, file, WACCA_IMAGE_HEADER_SIZE, reply)) return (WACCA_UPLOAD_TRANSFER_FAILED);

	if (!response(reply, WACCA_UPDATE_BEGIN, role) || reply[3] != WACCA_UPDATE_WRITING || wacca_get_u32(reply + 4) != 0) return (WACCA_UPLOAD_DEVICE_ERROR);
	for (uint32_t offset = 0; offset < size;)
	{
		const uint32_t remaining = size - offset;
		const uint32_t n = remaining < WACCA_UPDATE_CHUNK ? remaining : WACCA_UPDATE_CHUNK;
		uint8_t payload[4 + WACCA_UPDATE_CHUNK];
		wacca_put_u32(payload, offset);
		for (uint32_t i = 0; i < n; ++i) payload[4 + i] = file[WACCA_IMAGE_HEADER_SIZE + offset + i];
		if (!t->exchange(t->ctx, WACCA_UPDATE_DATA, payload, n + 4U, reply)) return (WACCA_UPLOAD_TRANSFER_FAILED);

		offset += n;
		if (!response(reply, WACCA_UPDATE_DATA, role) || reply[3] != WACCA_UPDATE_WRITING || wacca_get_u32(reply + 4) != offset) return (WACCA_UPLOAD_DEVICE_ERROR);
		if (t->progress) t->progress(t->ctx, offset, size);
	}
	if (!t->exchange(t->ctx, WACCA_UPDATE_FINISH, NULL, 0, reply)) return (WACCA_UPLOAD_TRANSFER_FAILED);
	if (!response(reply, WACCA_UPDATE_FINISH, role) || reply[3] != WACCA_UPDATE_COMMITTED || wacca_get_u32(reply + 4) != size) return (WACCA_UPLOAD_DEVICE_ERROR);
	return (WACCA_UPLOAD_OK);
}
