/*
 * Linker script for the phase 1 Lisa test kernel: one image linked at
 * logical address 0, exception vectors first. The .setup section holds the
 * code that runs with the MMU in setup mode; it must lie where address bit
 * 14 is set (src/kernel/lisa/lisaaddr.h), so it is moved up to the next
 * such address. Everything up to the end of .data is loaded from the disk;
 * .bss and the kernel stack follow.
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(start)

SECTIONS
{
	. = 0;
	.text : {
		KEEP(*(.vectors))
		*(.text .text.*)
		*(.rodata .rodata.*)
		. = ALIGN(2);
	}

	. = (. & 0x4000) ? . : ((. & ~0x7FFF) + 0x4000);
	.setup : {
		*(.setup)
		. = ALIGN(2);
	}
	ASSERT((ADDR(.setup) & 0x4000) != 0 &&
	       ((ADDR(.setup) + SIZEOF(.setup) - 1) & ~0x3FFF) ==
	       (ADDR(.setup) & ~0x3FFF),
	       "setup-mode code must lie where address bit 14 is set")

	.data : {
		*(.data .data.*)
		. = ALIGN(2);
	}
	.bss : {
		bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(2);
		bss_end = .;
	}
	kstack_top = bss_end + 0x1000;

	/DISCARD/ : {
		*(.comment)
		*(.note .note.*)
		*(.eh_frame .eh_frame_hdr)
	}
}
