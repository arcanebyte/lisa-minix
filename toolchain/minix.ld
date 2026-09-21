/*
 * Linker script for Minix-ST executables built with gcc: one image
 * linked at address 0, text (with read-only data) first, then data, then
 * bss, as tools/elf2mnx.py and src/mm/exec.c expect. Link with -q so that
 * elf2mnx can find the relocations. The first object must be the start-up
 * code (crtso.o for commands, head.o for kernel, mm and fs), whose first
 * instruction is the entry point.
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)

SECTIONS
{
	. = 0;
	.text : {
		*(.text .text.*)
		*(.rodata .rodata.*)
		. = ALIGN(2);
	}
	.data : {
		*(.data .data.*)
		. = ALIGN(2);
	}
	.bss : {
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(2);
	}
	/* C names carry no leading underscore in m68k ELF (ACK added one). */
	etext = ADDR(.data);
	edata = ADDR(.bss);
	end = ADDR(.bss) + SIZEOF(.bss);

	/DISCARD/ : {
		*(.comment)
		*(.note .note.*)
		*(.eh_frame .eh_frame_hdr)
	}
}
