#include "ch32v20x.h"
#include "flash.h"
#include <string.h>

// Standard 4 KiB erase / half-word programming, as in WCH's FLASH_Program example
// The bootloader runs at 48 MHz; the flash access clock therefore stays <= 60 MHz
// Busy polling executes in RAM, copied by startup alongside .data
#define RAM_FUNCTION __attribute__((section(".ramfunc"), noinline))
static RAM_FUNCTION bool wait_flash(void)
{
	for (unsigned count = 0; count < 2000000; ++count) if (!(FLASH->STATR & FLASH_STATR_BSY)) return ((FLASH->STATR & (FLASH_STATR_PGERR | FLASH_STATR_WRPRTERR)) == 0);
	return (false);
}
static bool range(uint32_t offset, size_t length, bool writing)
{
	return offset <= WACCA_FLASH_SIZE && length <= WACCA_FLASH_SIZE - offset && (!writing || offset >= WACCA_APP_OFFSET);
}
static bool read_flash(void *ctx, uint32_t offset, uint8_t *out, size_t length)
{
	(void)ctx;
	if (!out || !range(offset, length, false)) return (false);
	memcpy(out, (const void *)(WACCA_FLASH_BASE + offset), length);
	return (true);
}
static RAM_FUNCTION bool erase_page(uint32_t address)
{
	FLASH->STATR = FLASH_STATR_EOP | FLASH_STATR_PGERR | FLASH_STATR_WRPRTERR;
	if (!wait_flash()) return (false);
	FLASH->CTLR |= FLASH_CTLR_PER;
	FLASH->ADDR = address;
	FLASH->CTLR |= FLASH_CTLR_STRT;
	const bool ok = wait_flash();
	FLASH->CTLR &= ~FLASH_CTLR_PER;
	return (ok);
}
static RAM_FUNCTION bool program_halfword(uint32_t address, uint16_t value)
{
	FLASH->STATR = FLASH_STATR_EOP | FLASH_STATR_PGERR | FLASH_STATR_WRPRTERR;
	if (!wait_flash()) return (false);
	FLASH->CTLR |= FLASH_CTLR_PG;
	*(volatile uint16_t *)address = value;
	const bool ok = wait_flash();
	FLASH->CTLR &= ~FLASH_CTLR_PG;
	return (ok && *(volatile uint16_t *)address == value);
}
static uint32_t lock_interrupts(void)
{
	uint32_t saved;
	__asm__ volatile ("csrr %0, 0x800" : "=r"(saved));
	__disable_irq();
	__asm__ volatile ("" ::: "memory");
	return saved;
}
static void restore_interrupts(uint32_t saved)
{
	__asm__ volatile ("fence.i" ::: "memory");
	__asm__ volatile ("csrw 0x800, %0" :: "r"(saved) : "memory");
}
static void unlock_flash(void)
{
	FLASH->KEYR = UINT32_C(0x45670123);
	FLASH->KEYR = UINT32_C(0xCDEF89AB);
}
static bool erase_flash(void *ctx, uint32_t offset, size_t length)
{
	(void)ctx;
	if (!range(offset, length, true) || length != WACCA_FLASH_PAGE || offset % WACCA_FLASH_PAGE) return (false);
	const uint32_t saved = lock_interrupts();
	unlock_flash();

	const bool ok = erase_page(WACCA_FLASH_BASE + offset);
	FLASH->CTLR |= FLASH_CTLR_LOCK;
	restore_interrupts(saved);
	return (ok);
}
static bool write_flash(void *ctx, uint32_t offset, const uint8_t *data, size_t length)
{
	(void)ctx;
	if (!data || !length || length > WACCA_FLASH_SIZE || !range(offset, length + (length & 1U), true) || (offset & 1U)) return (false);
	const uint32_t saved = lock_interrupts();
	unlock_flash();

	bool ok = true;
	for (size_t i = 0; ok && i < length; i += 2)
	{
		const uint16_t upper = i + 1 < length ? data[i + 1] : 0xFFU;
		ok = program_halfword(WACCA_FLASH_BASE + offset + (uint32_t)(i), (uint16_t)((uint16_t)data[i] | (uint16_t)(upper << 8)));
	}
	FLASH->CTLR |= FLASH_CTLR_LOCK;
	restore_interrupts(saved);
	return (ok);
}
const struct wacca_flash_io *wacca_boot_flash(void)
{
	static const struct wacca_flash_io io = { NULL, read_flash, erase_flash, write_flash };
	return (&io);
}
