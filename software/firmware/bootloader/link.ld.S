#include "image_layout.h"
ENTRY(_start)
__stack_size = 2048;
MEMORY {
	#if defined(WACCA_BOOTLOADER)
	FLASH (rx) : ORIGIN = 0, LENGTH = WACCA_BOOT_SIZE
	#else
	FLASH (rx) : ORIGIN = WACCA_APP_OFFSET, LENGTH = WACCA_APP_CAPACITY
	#endif
	RAM (xrw) : ORIGIN = WACCA_RAM_BASE, LENGTH = WACCA_RAM_SIZE - WACCA_BOOT_REQUEST_SIZE
	REQUEST (rw) : ORIGIN = WACCA_RAM_BASE + WACCA_RAM_SIZE - WACCA_BOOT_REQUEST_SIZE, LENGTH = WACCA_BOOT_REQUEST_SIZE
}
SECTIONS {
	.init : { KEEP(*(.init)) } > FLASH
	.vector ALIGN(256) : { KEEP(*(.vector)) . = ALIGN(4); } > FLASH
	.text : {
		*(.text .text.*) *(.rodata .rodata.*) *(.gnu.linkonce.t.*)
		. = ALIGN(4);
	} > FLASH
	.data : {
		. = ALIGN(4);
		_data_vma = .;
		*(.ramfunc .ramfunc.*)
		*(.data .data.*)
		. = ALIGN(8);
		__global_pointer$ = . + 0x800;
		*(.sdata .sdata.* .sdata2.* .srodata .srodata.*)
		. = ALIGN(4);
		_edata = .;
	} > RAM AT > FLASH
	_data_lma = LOADADDR(.data);
	.bss (NOLOAD) : {
		. = ALIGN(4); _sbss = .;
		*(.sbss .sbss.* .bss .bss.* COMMON)
		. = ALIGN(4); _ebss = .;
	} > RAM
	_end = _ebss;
	PROVIDE(end = _ebss);
	.stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size (NOLOAD) : {
		_heap_end = .; _susrstack = .;
		. += __stack_size; _eusrstack = .;
	} > RAM
	.boot_request (NOLOAD) : { KEEP(*(.boot_request)) } > REQUEST
	ASSERT(SIZEOF(.boot_request) == WACCA_BOOT_REQUEST_SIZE, "Invalid boot request size")
	ASSERT(_ebss <= _susrstack, "RAM overlaps stack")
	ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(FLASH) + LENGTH(FLASH), "Firmware exceeds its flash partition")
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
