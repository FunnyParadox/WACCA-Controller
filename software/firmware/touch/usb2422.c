#include "usb2422.h"
#include <string.h>

enum { HUB_CFG1 = 0x06, HUB_CFG2 = 0x07, HUB_NONREMOVABLE = 0x09, HUB_MAXPB = 0x0D, HUB_HCMCB = 0x0F, HUB_PWRT = 0x10, HUB_PRTSP = 0xFA, HUB_STCD = 0xFF, HUB_BLOCK = 32, HUB_USB_ATTACH = 0x01, HUB_SMBUS_POWER_DOWN = 0x04 };

bool wacca_usb2422_config(uint8_t *image, size_t length, unsigned total_mA, unsigned hub_mA)
{
	if (image == NULL || length != WACCA_HUB_CONFIG_SIZE || total_mA < 100 || total_mA > 500 || !hub_mA || hub_mA > 100 || hub_mA > total_mA || ((total_mA | hub_mA) & 1U) != 0) return (false);
	memset(image, 0, length);

	// Actual Microchip hub identity, retained from its documented defaults
	uint8_t header[5] = {0x24, 0x04, 0x22, 0x24, 0xA0};
	memcpy(image, header, sizeof(header));
	image[HUB_CFG1] = 0x0C; // Bus powered, normal EOP, no OC sensing, ganged power control
	image[HUB_CFG2] = 0x28; // Compound device, default 8 ms OC timer
	image[HUB_NONREMOVABLE] = 0x06;
	image[0x0C] = true; image[0x0E] = true; // Unused self-powered budgets
	image[HUB_MAXPB] = (uint8_t)(total_mA / 2U);
	image[HUB_HCMCB] = (uint8_t)(hub_mA / 2U);
	image[HUB_PWRT] = 50; // 100 ms power-on settling allowance
	image[HUB_PRTSP] = WACCA_HUB_PORT_SWAP; // Swap both downstream differential pairs

	return (true);
}

static enum wacca_hub_result fail(const struct wacca_hub_io *io, enum wacca_hub_result result)
{
	io->set_reset(io->ctx, true);
	return (result);
}

enum wacca_hub_result wacca_usb2422_start(const struct wacca_hub_io *io, const uint8_t *image, size_t length)
{
	uint8_t tx[HUB_BLOCK + 2];
	uint8_t rx[2];

	if (io == NULL || image == NULL || length != WACCA_HUB_CONFIG_SIZE || io->set_reset == NULL || io->prepare_bus == NULL || io->delay_us == NULL || io->write == NULL || io->write_read == NULL) return (WACCA_HUB_BAD_ARGUMENT);
	if (image[HUB_PRTSP] != WACCA_HUB_PORT_SWAP) return (WACCA_HUB_BAD_PORT_SWAP);

	io->set_reset(io->ctx, true);
	if (!io->prepare_bus(io->ctx)) return (WACCA_HUB_BUS_INIT_FAILED);

	io->delay_us(io->ctx, 2); // RESET_N minimum is 1 us
	io->set_reset(io->ctx, false);
	io->delay_us(io->ctx, 1000);

	// SMBus configuration starts from zeros, not from the default ROM descriptor
	// Write every register through FE before USB_ATTACH
	for (size_t offset = 0; offset < length; offset += 0)
	{
		const size_t remaining = length - offset;
		const size_t count = (remaining < HUB_BLOCK) ? remaining : HUB_BLOCK;
		const unsigned attempts = (!offset) ? WACCA_HUB_READY_ATTEMPTS : 1;
		bool written = false;

		tx[0] = (uint8_t)(offset);
		tx[1] = (uint8_t)(count); // Required SMBus byte count
		memcpy(tx + 2, image + offset, count);

		for (unsigned attempt = 0; attempt < attempts; ++attempt)
		{
			if (io->write(io->ctx, WACCA_HUB_ADDRESS, tx, count + 2))
			{
				written = true;
				break;
			}
			if (attempt + 1 < attempts) io->delay_us(io->ctx, 1000);
		}
		if (!written) return (fail(io, WACCA_HUB_WRITE_FAILED));
		offset += count;
	}
	tx[0] = HUB_PRTSP;

	if (!io->write_read(io->ctx, WACCA_HUB_ADDRESS, tx, 1, rx, sizeof(rx))) return (fail(io, WACCA_HUB_READ_FAILED));
	if (rx[0] != 1 || (rx[1] & 0x07U) != WACCA_HUB_PORT_SWAP) return (fail(io, WACCA_HUB_VERIFY_FAILED));

	tx[0] = HUB_STCD;
	tx[1] = 1;
	// Register FFh bit 2 powers down SMBus after ACK; bit 0 attaches USB.
	// Release the slow configuration target before using 400 kHz for touch.
	tx[2] = HUB_USB_ATTACH | HUB_SMBUS_POWER_DOWN;

	if (!io->write(io->ctx, WACCA_HUB_ADDRESS, tx, 3)) return (fail(io, WACCA_HUB_WRITE_FAILED));
	return (WACCA_HUB_OK);
}
