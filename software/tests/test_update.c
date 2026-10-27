#include "update_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct simulation {
	uint8_t bytes[WACCA_FLASH_SIZE];
	struct wacca_update device;
	unsigned operations, exchanges, cut_operation, drop_exchange;
	bool cut, corrupt_before_finish;
};
static struct simulation sim;
static uint8_t file[WACCA_IMAGE_HEADER_SIZE + WACCA_APP_CAPACITY];
static bool read_memory(void *ctx, uint32_t offset, uint8_t *out, size_t n)
{
	struct simulation *s = ctx;
	assert(offset <= WACCA_FLASH_SIZE && n <= WACCA_FLASH_SIZE - offset);
	memcpy(out, s->bytes + offset, n);
	return true;
}
static bool erase_memory(void *ctx, uint32_t offset, size_t n)
{
	struct simulation *s = ctx;
	assert(offset >= WACCA_APP_OFFSET && offset <= WACCA_FLASH_SIZE && n <= WACCA_FLASH_SIZE - offset);
	assert(n == WACCA_FLASH_PAGE && offset % WACCA_FLASH_PAGE == 0);
	++s->operations;
	if (s->cut_operation && s->operations == s->cut_operation)
	{
		memset(s->bytes + offset, 0xFF, n / 2);
		s->cut = true;
		return false;
	}
	memset(s->bytes + offset, 0xFF, n);
	return true;
}
static bool write_memory(void *ctx, uint32_t offset, const uint8_t *bytes, size_t n)
{
	struct simulation *s = ctx;
	assert(offset >= WACCA_APP_OFFSET && offset <= WACCA_FLASH_SIZE && n <= WACCA_FLASH_SIZE - offset);
	assert(!(offset & 1U));
	++s->operations;
	const bool cut = s->cut_operation && s->operations == s->cut_operation;
	if (cut) n /= 2;
	for (size_t i = 0; i < n; ++i)
	{
		assert((s->bytes[offset + i] & bytes[i]) == bytes[i]);
		s->bytes[offset + i] &= bytes[i];
	}
	if (cut) s->cut = true;
	return !cut;
}
static void restart(enum wacca_role role)
{
	const struct wacca_flash_io io = { &sim, read_memory, erase_memory, write_memory };
	wacca_update_init(&sim.device, role, &io);
}
static void reset(enum wacca_role role)
{
	memset(&sim, 0, sizeof(sim));
	memset(sim.bytes, 0xFF, sizeof(sim.bytes));
	memset(sim.bytes, 0xA5, WACCA_BOOT_SIZE);
	restart(role);
}
static size_t make_file(enum wacca_role role, uint32_t size)
{
	memcpy(file, WACCA_IMAGE_MAGIC, 8);
	wacca_put_u32(file + 8, 1);
	wacca_put_u32(file + 12, (uint32_t)role);
	wacca_put_u32(file + 16, WACCA_APP_OFFSET);
	wacca_put_u32(file + 20, size);
	wacca_put_u32(file + 28, WACCA_IMAGE_LAYOUT_ID);
	for (uint32_t i = 0; i < size; ++i) file[WACCA_IMAGE_HEADER_SIZE + i] = (uint8_t)(i * 17U + 3U);
	wacca_put_u32(file + 24, ~wacca_crc32_update(UINT32_MAX, file + WACCA_IMAGE_HEADER_SIZE, size));
	return WACCA_IMAGE_HEADER_SIZE + size;
}
static bool decode(struct wacca_decoder *decoder, const uint8_t *wire, size_t n, struct wacca_message *message)
{
	bool complete = false;
	for (size_t i = 0; i < n; ++i)
		if (wacca_decoder_feed(decoder, wire[i], message)) complete = true;
	return complete;
}
static bool exchange(void *ctx, uint8_t command, const uint8_t *payload, size_t n, uint8_t reply[WACCA_UPDATE_REPLY_SIZE])
{
	struct simulation *s = ctx;
	if (++s->exchanges == s->drop_exchange) return false;
	uint8_t wire[WACCA_MAX_ENCODED];
	struct wacca_decoder decoder = {0};
	struct wacca_message message;
	const uint16_t sequence = (uint16_t)s->exchanges;
	size_t length = wacca_message_encode(command, sequence, payload, n, wire, sizeof(wire));
	assert(decode(&decoder, wire, length, &message));
	if (command == WACCA_UPDATE_FINISH && s->corrupt_before_finish) s->bytes[WACCA_APP_OFFSET] ^= 1;
	wacca_update_handle(&s->device, &message, reply);
	if (s->cut) return false;
	length = wacca_message_encode(WACCA_RESPONSE, sequence, reply, WACCA_UPDATE_REPLY_SIZE, wire, sizeof(wire));
	struct wacca_decoder response_decoder = {0};
	assert(decode(&response_decoder, wire, length, &message));
	assert(message.type == WACCA_RESPONSE && message.sequence == sequence && message.length == WACCA_UPDATE_REPLY_SIZE);
	memcpy(reply, message.payload, WACCA_UPDATE_REPLY_SIZE);
	return true;
}
static enum wacca_upload_result upload(size_t n)
{
	const struct wacca_update_transport transport = { &sim, exchange, NULL };
	return wacca_upload(file, n, &transport);
}
static void boot_unchanged(void)
{
	for (unsigned i = 0; i < WACCA_BOOT_SIZE; ++i) assert(sim.bytes[i] == 0xA5);
}
static uint8_t command(uint8_t type, const uint8_t *payload, uint16_t n)
{
	const struct wacca_message message = { type, 1, n, payload };
	uint8_t reply[WACCA_UPDATE_REPLY_SIZE];
	wacca_update_handle(&sim.device, &message, reply);
	return reply[1];
}
static void successful_updates(void)
{
	for (unsigned role = WACCA_ROLE_TOUCH; role <= WACCA_ROLE_LED; ++role)
	{
		reset((enum wacca_role)role);
		assert(!wacca_update_app_valid(&sim.device));
		size_t n = make_file((enum wacca_role)role, 513);
		assert(upload(n) == WACCA_UPLOAD_OK);
		assert(wacca_update_app_valid(&sim.device));
		assert(memcmp(sim.bytes + WACCA_APP_OFFSET, file + WACCA_IMAGE_HEADER_SIZE, 513) == 0);
		assert(command(WACCA_UPDATE_RUN, NULL, 0) == WACCA_UPDATE_OK && sim.device.run_requested);
		restart((enum wacca_role)role);
		assert(wacca_update_app_valid(&sim.device));
		n = make_file((enum wacca_role)role, WACCA_APP_CAPACITY);
		assert(upload(n) == WACCA_UPLOAD_OK && wacca_update_app_valid(&sim.device));
		boot_unchanged();
	}
}
static void bad_files_and_devices(void)
{
	reset(WACCA_ROLE_LED);
	const size_t n = make_file(WACCA_ROLE_TOUCH, 1024);
	assert(upload(n) == WACCA_UPLOAD_WRONG_DEVICE && sim.operations == 0);
	reset(WACCA_ROLE_TOUCH);
	static const uint8_t rejected_magic[][8] = {
		{ 'W', 'A', 'C', 'U', 'P', 'D', '1', 0 }, // Previous package format.
		{ 'X', 'C', 'F', 'U', 0, 0, 0, 0 },
		{ 'W', 'C', 'F', 'U', 0, 0, 0, 1 } // Padding must also be zero.
	};
	for (size_t i = 0; i < sizeof(rejected_magic) / sizeof(rejected_magic[0]); ++i)
	{
		memcpy(file, rejected_magic[i], sizeof(rejected_magic[i]));
		assert(upload(n) == WACCA_UPLOAD_BAD_FILE && sim.exchanges == 0);
		assert(command(WACCA_UPDATE_BEGIN, file, WACCA_IMAGE_HEADER_SIZE) == WACCA_UPDATE_BAD_IMAGE && sim.operations == 0);
	}
	(void)make_file(WACCA_ROLE_TOUCH, 1024);
	file[WACCA_IMAGE_HEADER_SIZE + 3] ^= 1;
	assert(upload(n) == WACCA_UPLOAD_BAD_FILE && sim.exchanges == 0);
	(void)make_file(WACCA_ROLE_TOUCH, 1024);
	assert(upload(n - 1) == WACCA_UPLOAD_BAD_FILE && sim.exchanges == 0);
	wacca_put_u32(file + 16, 0); // Cannot address the resident loader.
	assert(upload(n) == WACCA_UPLOAD_BAD_FILE && sim.exchanges == 0);
	assert(command(WACCA_UPDATE_BEGIN, file, WACCA_IMAGE_HEADER_SIZE) == WACCA_UPDATE_BAD_IMAGE && sim.operations == 0);
	wacca_put_u32(file + 16, WACCA_APP_OFFSET);
	wacca_put_u32(file + 20, UINT32_MAX);
	assert(command(WACCA_UPDATE_BEGIN, file, WACCA_IMAGE_HEADER_SIZE) == WACCA_UPDATE_BAD_IMAGE && sim.operations == 0);
	boot_unchanged();
}
static void invalid_order_and_crc(void)
{
	reset(WACCA_ROLE_TOUCH);
	const size_t n = make_file(WACCA_ROLE_TOUCH, 513);
	assert(command(WACCA_UPDATE_FINISH, NULL, 0) == WACCA_UPDATE_BAD_STATE);
	assert(command(WACCA_UPDATE_RUN, NULL, 0) == WACCA_UPDATE_BAD_STATE);
	assert(command(WACCA_UPDATE_BEGIN, file, WACCA_IMAGE_HEADER_SIZE) == WACCA_UPDATE_OK);
	assert(command(WACCA_UPDATE_RUN, NULL, 0) == WACCA_UPDATE_BAD_STATE);
	uint8_t payload[WACCA_UPDATE_CHUNK + 4] = {0};
	wacca_put_u32(payload, UINT32_MAX);
	assert(command(WACCA_UPDATE_DATA, payload, sizeof(payload)) == WACCA_UPDATE_BAD_OFFSET);
	wacca_put_u32(payload, 256);
	assert(command(WACCA_UPDATE_DATA, payload, sizeof(payload)) == WACCA_UPDATE_BAD_OFFSET);
	assert(command(WACCA_UPDATE_FINISH, NULL, 0) == WACCA_UPDATE_BAD_STATE);
	sim.corrupt_before_finish = true;
	assert(upload(n) == WACCA_UPLOAD_DEVICE_ERROR);
	assert(!wacca_update_app_valid(&sim.device));
	assert(command(WACCA_UPDATE_RUN, NULL, 0) == WACCA_UPDATE_BAD_STATE);
	boot_unchanged();
}
static void power_failure_and_retry(void)
{
	const size_t n = make_file(WACCA_ROLE_TOUCH, 4353);
	reset(WACCA_ROLE_TOUCH);
	assert(upload(n) == WACCA_UPLOAD_OK);
	const unsigned operations = sim.operations;
	const unsigned exchanges = sim.exchanges;
	for (unsigned cut = 1; cut <= operations; ++cut)
	{
		reset(WACCA_ROLE_TOUCH);
		sim.cut_operation = cut;
		assert(upload(n) != WACCA_UPLOAD_OK && sim.cut);
		restart(WACCA_ROLE_TOUCH);
		assert(!wacca_update_app_valid(&sim.device));
		boot_unchanged();
		sim.cut = false;
		sim.cut_operation = 0;
		assert(upload(n) == WACCA_UPLOAD_OK && wacca_update_app_valid(&sim.device));
	}
	for (unsigned drop = 1; drop <= exchanges; ++drop)
	{
		reset(WACCA_ROLE_TOUCH);
		sim.drop_exchange = drop;
		assert(upload(n) != WACCA_UPLOAD_OK);
		restart(WACCA_ROLE_TOUCH);
		assert(!wacca_update_app_valid(&sim.device));
		sim.drop_exchange = 0;
		assert(upload(n) == WACCA_UPLOAD_OK);
		boot_unchanged();
	}
	// Updating an existing valid application invalidates its manifest first.
	reset(WACCA_ROLE_TOUCH);
	assert(upload(n) == WACCA_UPLOAD_OK);
	assert(command(WACCA_UPDATE_BEGIN, file, WACCA_IMAGE_HEADER_SIZE) == WACCA_UPDATE_OK);
	restart(WACCA_ROLE_TOUCH);
	assert(!wacca_update_app_valid(&sim.device));
	assert(upload(n) == WACCA_UPLOAD_OK);
	sim.bytes[WACCA_APP_OFFSET + 15] ^= 1;
	restart(WACCA_ROLE_TOUCH);
	assert(!wacca_update_app_valid(&sim.device));
}
static void boot_requests(void)
{
	volatile struct wacca_boot_request request = {0};
	assert(!wacca_boot_request_take(&request, WACCA_ROLE_TOUCH, true));
	for (enum wacca_role role = WACCA_ROLE_TOUCH; role <= WACCA_ROLE_LED; ++role)
	{
		wacca_boot_request_store(&request, role);
		assert(wacca_boot_request_take(&request, role, true));
		assert(!wacca_boot_request_take(&request, role, true));
		wacca_boot_request_store(&request, role);
		assert(!wacca_boot_request_take(&request, role, false)); // Cold/external reset.
		assert(!wacca_boot_request_take(&request, role, true));
		wacca_boot_request_store(&request, role);
		request.inverse_magic ^= 1;
		assert(!wacca_boot_request_take(&request, role, true));
		wacca_boot_request_store(&request, role);
		request.inverse_role ^= 1;
		assert(!wacca_boot_request_take(&request, role, true));
		wacca_boot_request_store(&request, role);
		assert(!wacca_boot_request_take(&request, role == WACCA_ROLE_TOUCH ? WACCA_ROLE_LED : WACCA_ROLE_TOUCH, true));
		assert(!wacca_boot_request_take(&request, role, true));
	}
}
int main(void)
{
	assert(~wacca_crc32_update(UINT32_MAX, (const uint8_t *)"123456789", 9) == UINT32_C(0xCBF43926));
	boot_requests();
	successful_updates();
	bad_files_and_devices();
	invalid_order_and_crc();
	power_failure_and_retry();
	puts("USB update: both roles, bounds, wrong images, CRC, interrupted operations and retry passed");
	return 0;
}
