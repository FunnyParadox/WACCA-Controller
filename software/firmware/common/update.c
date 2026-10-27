#include "update.h"
#include <string.h>

uint32_t wacca_get_u32(const uint8_t *p)
{
	return ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
void wacca_put_u32(uint8_t *p, uint32_t value)
{
	for (unsigned i = 0; i < 4; ++i)
		p[i] = (uint8_t)(value >> (8U * i));
}
uint32_t wacca_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
	for (size_t i = 0; i < length; ++i)
	{
		crc ^= data[i];
		for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((0U - (crc & 1U)) & UINT32_C(0xEDB88320));
	}
	return (crc);
}
bool wacca_image_header(const uint8_t *h, size_t length, enum wacca_role role)
{
	if (!h || length != WACCA_IMAGE_HEADER_SIZE) return (false);

	const uint32_t size = wacca_get_u32(h + 20);
	return (memcmp(h, WACCA_IMAGE_MAGIC, 8) == 0 && wacca_get_u32(h + 8) == 1 &&
	wacca_get_u32(h + 12) == (uint32_t)role && wacca_get_u32(h + 16) == WACCA_APP_OFFSET &&
	size >= WACCA_IMAGE_MIN_SIZE && size <= WACCA_APP_CAPACITY &&
	wacca_get_u32(h + 28) == WACCA_IMAGE_LAYOUT_ID);
}
void wacca_update_init(struct wacca_update *u, enum wacca_role role, const struct wacca_flash_io *flash)
{
	memset(u, 0, sizeof(*u));
	u->role = role;
	u->flash = *flash;
}
static bool image_crc(const struct wacca_update *u, uint32_t size, uint32_t expected)
{
	uint8_t bytes[64];
	uint32_t crc = UINT32_MAX;
	for (uint32_t offset = 0; offset < size;)
	{
		const uint32_t remaining = size - offset;
		const size_t n = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
		if (!u->flash.read(u->flash.ctx, WACCA_APP_OFFSET + offset, bytes, n)) return false;
		crc = wacca_crc32_update(crc, bytes, n);
		offset += (uint32_t)(n);
	}
	return (~crc == expected);
}
bool wacca_update_app_valid(const struct wacca_update *u)
{
	uint8_t metadata[WACCA_IMAGE_HEADER_SIZE + 4];
	return (u->flash.read(u->flash.ctx, WACCA_METADATA_OFFSET, metadata, sizeof(metadata)) &&
	wacca_get_u32(metadata + WACCA_IMAGE_HEADER_SIZE) == WACCA_IMAGE_COMMIT &&
	wacca_image_header(metadata, WACCA_IMAGE_HEADER_SIZE, u->role) &&
	image_crc(u, wacca_get_u32(metadata + 20), wacca_get_u32(metadata + 24)));
}
static enum wacca_update_status begin(struct wacca_update *u, const struct wacca_message *m)
{
	if (!wacca_image_header(m->payload, m->length, u->role))
	{
		if (m->payload && m->length == WACCA_IMAGE_HEADER_SIZE && wacca_get_u32(m->payload + 12) != (uint32_t)u->role) return (WACCA_UPDATE_WRONG_ROLE);
		return (WACCA_UPDATE_BAD_IMAGE);
	}
	// Invalidate the old image before touching any application page
	u->state = WACCA_UPDATE_IDLE;
	u->received = 0;
	u->run_requested = false;
	if (!u->flash.erase(u->flash.ctx, WACCA_METADATA_OFFSET, WACCA_FLASH_PAGE)) return (WACCA_UPDATE_FLASH_ERROR);

	uint8_t tag[4];
	if (!u->flash.read(u->flash.ctx, WACCA_METADATA_OFFSET + WACCA_IMAGE_HEADER_SIZE, tag, sizeof(tag)) || wacca_get_u32(tag) == WACCA_IMAGE_COMMIT) return (WACCA_UPDATE_FLASH_ERROR);

	memcpy(u->header, m->payload, sizeof(u->header));
	u->size = wacca_get_u32(u->header + 20);
	u->expected_crc = wacca_get_u32(u->header + 24);
	u->state = WACCA_UPDATE_WRITING;

	return (WACCA_UPDATE_OK);
}
static enum wacca_update_status data(struct wacca_update *u, const struct wacca_message *m)
{
	if (u->state != WACCA_UPDATE_WRITING) return (WACCA_UPDATE_BAD_STATE);
	if (m->length <= 4 || m->length > WACCA_UPDATE_CHUNK + 4) return (WACCA_UPDATE_BAD_COMMAND);

	const uint32_t offset = wacca_get_u32(m->payload);
	const uint32_t n = ((uint32_t)(m->length) - 4U);
	if (offset != u->received || n > u->size - u->received || (n != WACCA_UPDATE_CHUNK && n != u->size - u->received)) return (WACCA_UPDATE_BAD_OFFSET);

	// A page is erased immediately before its first chunk, chunks never cross pages
	if (offset % WACCA_FLASH_PAGE == 0 && !u->flash.erase(u->flash.ctx, WACCA_APP_OFFSET + offset, WACCA_FLASH_PAGE))
	{
		u->state = WACCA_UPDATE_IDLE;
		return (WACCA_UPDATE_FLASH_ERROR);
	}
	if (!u->flash.write(u->flash.ctx, WACCA_APP_OFFSET + offset, m->payload + 4, n))
	{
		u->state = WACCA_UPDATE_IDLE;
		return (WACCA_UPDATE_FLASH_ERROR);
	}
	u->received += n;
	return (WACCA_UPDATE_OK);
}
static enum wacca_update_status finish(struct wacca_update *u)
{
	if (u->state != WACCA_UPDATE_WRITING || u->received != u->size) return (WACCA_UPDATE_BAD_STATE);
	if (!image_crc(u, u->size, u->expected_crc)) return (WACCA_UPDATE_BAD_CRC);

	// Commit only after all bytes have been read back and their CRC verified
	uint8_t tag[4];
	wacca_put_u32(tag, WACCA_IMAGE_COMMIT);
	if (!u->flash.write(u->flash.ctx, WACCA_METADATA_OFFSET, u->header, sizeof(u->header)) || !u->flash.write(u->flash.ctx, WACCA_METADATA_OFFSET + sizeof(u->header), tag, sizeof(tag)) || !wacca_update_app_valid(u))
	{
		u->state = WACCA_UPDATE_IDLE;
		return (WACCA_UPDATE_FLASH_ERROR);
	}
	u->state = WACCA_UPDATE_COMMITTED;
	return (WACCA_UPDATE_OK);
}
void wacca_update_handle(struct wacca_update *u, const struct wacca_message *m, uint8_t reply[WACCA_UPDATE_REPLY_SIZE])
{
	enum wacca_update_status status = WACCA_UPDATE_BAD_COMMAND;
	if (m->length && !m->payload) status = WACCA_UPDATE_BAD_COMMAND;
	else if (m->type == WACCA_UPDATE_INFO && m->length == 0) status = WACCA_UPDATE_OK;
	else if (m->type == WACCA_UPDATE_BEGIN) status = begin(u, m);
	else if (m->type == WACCA_UPDATE_DATA) status = data(u, m);
	else if (m->type == WACCA_UPDATE_FINISH && m->length == 0) status = finish(u);
	else if (m->type == WACCA_UPDATE_RUN && m->length == 0)
	{
		status = u->state != WACCA_UPDATE_WRITING && (wacca_update_app_valid(u) ? WACCA_UPDATE_OK : WACCA_UPDATE_BAD_STATE);
		u->run_requested = (status == WACCA_UPDATE_OK);
	}
	reply[0] = (uint8_t)m->type;
	reply[1] = (uint8_t)status;
	reply[2] = (uint8_t)u->role;
	reply[3] = (uint8_t)u->state;
	wacca_put_u32(reply + 4, u->received);
	wacca_put_u32(reply + 8, WACCA_APP_OFFSET);
	wacca_put_u32(reply + 12, WACCA_APP_CAPACITY);
	wacca_put_u32(reply + 16, WACCA_UPDATE_CHUNK);
	wacca_put_u32(reply + 20, 1); // Bootloader protocol version
}

void wacca_boot_request_store(volatile struct wacca_boot_request *request, enum wacca_role role)
{
	const uint32_t magic = UINT32_C(0x55434357); // "WCCU" in little-endian RAM
	request->magic = 0;
	request->role = (uint32_t)role;
	request->inverse_role = ~(uint32_t)role;
	request->inverse_magic = ~magic;
	request->magic = magic; // Write the validity tag last
}
bool wacca_boot_request_take(volatile struct wacca_boot_request *request, enum wacca_role role, bool software_reset)
{
	const uint32_t magic = request->magic, saved_role = request->role;
	const bool valid = (software_reset) && (role == WACCA_ROLE_TOUCH || role == WACCA_ROLE_LED) && (magic == UINT32_C(0x55434357)) && (request->inverse_magic == ~magic) && (saved_role == (uint32_t)(role)) && (request->inverse_role == ~saved_role);
	request->magic = 0;
	request->role = 0;
	request->inverse_magic = 0;
	request->inverse_role = 0;
	return (valid); // Consume once, including stale requests from another role
}
