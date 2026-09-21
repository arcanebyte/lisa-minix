/*
 * Linker script for the Minix kernel on the Lisa: toolchain/minix.ld plus
 * the .setup section, which holds the code and data used while the MMU is
 * in setup mode (src/kernel/lisa/lisampx.s). In setup mode the MMU decodes
 * only addresses whose bit 14 is set as RAM, so .setup is placed at the end
 * of the text, moved up to the next such address if necessary, and must
 * not cross out of that 16 KB range. It stays inside the text output
 * section so tools/elf2mnx.py and tools/build.py see one text segment.
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
		. = (. & 0x4000) ? . : ((. | 0x3FFF) + 1);
		setup_start = .;
		*(.setup)
		. = ALIGN(2);
		setup_end = .;
	}
	ASSERT((setup_start & 0x4000) != 0 &&
	       ((setup_end - 1) & ~0x3FFF) == (setup_start & ~0x3FFF),
	       "setup-mode code must lie where address bit 14 is set")
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
