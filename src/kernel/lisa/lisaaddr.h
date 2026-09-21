/* Apple Lisa 2 hardware addresses used by Minix.
 *
 * Sources, per entry: [HM] Lisa Hardware Manual (Apple, 1983);
 * [U] UniPlus V.1.5+ sources in ~/github/uniplus/v1.5 (read, not copied);
 * [E] LisaEm sources in ~/github/lisaem. Entries marked only [U] or [E]
 * are still to be checked against [HM] (PLAN.md, risk 7).
 *
 * All device register access goes through volatile pointers
 * (docs/toolchain.md, "Hardware registers").
 */

#ifndef LISAADDR_H
#define LISAADDR_H

/* I/O spaces. [U include/sys/mmu.h] */
#define LISA_STDIO	0xFC0000L	/* standard I/O space */
#define LISA_SPECIO	0xFE0000L	/* special I/O space (boot ROM) */

/* MMU control. Writing any value to the address performs the action.
 * [U include/sys/mmu.h, sys/mch.s] */
#define MMU_SEG1_OFF	0xFCE008L
#define MMU_SEG1_ON	0xFCE00AL
#define MMU_SEG2_OFF	0xFCE00CL
#define MMU_SEG2_ON	0xFCE00EL
#define MMU_SETUP_ON	0xFCE010L
#define MMU_SETUP_OFF	0xFCE012L

/* In setup mode, segment n's limit register is at n * 0x20000 + 0x8000 and
 * its origin register at + 0x8008; both are 12 bits. The code that runs
 * while setup mode is on must be at an address whose bit 14 is set: below
 * 0x4000 in a segment the hardware decodes special I/O, at 0x8000-0xBFFF
 * the MMU registers. [U sys/mch.s; E lisa/cpu_board/mmu.c
 * init_start_mode_segment] */
#define MMU_SEGSIZE	0x20000L
#define MMU_NSEGS	128
#define MMU_SLR		0x8000L		/* limit and access type */
#define MMU_SOR		0x8008L		/* origin, in 512-byte pages */

/* Segment access types, in the limit register's top 4 of 12 bits.
 * [U include/sys/mmu.h] */
#define MMU_ACC_MASK	0xF00
#define MMU_ACC_ROSTK	0x400		/* read-only stack */
#define MMU_ACC_RO	0x500		/* read-only */
#define MMU_ACC_RWSTK	0x600		/* read-write stack */
#define MMU_ACC_RW	0x700		/* read-write */
#define MMU_ACC_IO	0x800		/* I/O space */
#define MMU_ACC_INVAL	0xC00		/* invalid */
#define MMU_ACC_SPIO	0xF00		/* special I/O */

/* Vertical retrace interrupt: level 1 autovector, about 60 Hz. The
 * handler acknowledges it by turning it off and on. [U sys/l1.c,
 * include/sys/mmu.h] */
#define VRT_OFF		0xFCE018L
#define VRT_ON		0xFCE01AL
#define STATUS_REG	0xFCF800L	/* bit 2: vertical retrace */

/* Video address latch: physical address of the 32 KB video page divided
 * by 32 KB. The display is 720 x 364 pixels, 90 bytes per line, a set bit
 * black. [U include/sys/mmu.h, sys/bm.c; E screen dump] */
#define VIDEO_LATCH	0xFCE800L
#define VIDEO_PAGE	0x8000L

/* COPS VIA interrupt flag bit for "COPS has a byte" (CA1).
 * [U include/sys/cops.h] */
#define VIA_IRQ_CA1	0x02

/* Sending the COPS a command: the command byte goes into port A without
 * handshake (register 15); when port B's CRDY bit (0x40) goes low the COPS
 * is ready, and port A is briefly made an output.  Command 0x02 reads the
 * real-time clock. [U sys/l2.c l2copscmd, include/sys/cops.h,
 * include/sys/local.h; E lisa/io_board/cops.c] */
#define VIA_DDRA	3
#define VIA_ORA_NH	15
#define COPS_CRDY	0x40
#define COPS_READ_CLOCK	0x02

/* 6522 VIAs. The parallel port VIA's registers are 8 bytes apart, the
 * COPS VIA's 2 bytes apart, both starting at base + 1.
 * [U include/sys/pport.h struct device_d, include/sys/cops.h struct
 * device_e] */
#define VIA_PARALLEL	0xFCD900L	/* built-in parallel port (ProFile) */
#define VIA_COPS	0xFCDD80L	/* keyboard COPS */
#define VIA_PAR_REG(n)	(VIA_PARALLEL + 1 + 8 * (n))
#define VIA_COPS_REG(n)	(VIA_COPS + 1 + 2 * (n))
#define VIA_ORB		0
#define VIA_ORA		1
#define VIA_IFR		13
#define VIA_IER		14

/* Z8530 SCC. Control and data registers per channel. The SCC clock is
 * 4 MHz. [E lisa/io_board/z8530.c; U sys/conf.c sc_line] */
#define SCC_B_CTRL	0xFCD241L
#define SCC_A_CTRL	0xFCD243L
#define SCC_B_DATA	0xFCD245L
#define SCC_A_DATA	0xFCD247L
#define SCC_PCLK	4000000L

/* Boot ROM entry points and globals. [U stand/wbblk.s, pbblk.s,
 * include/sys/mmu.h] */
#define ROM_MONITOR	0xFE0084L	/* enter the ROM monitor */
#define ROM_PROREAD	0xFE0090L	/* read a block, built-in ProFile */
#define ROM_BOOTDEV	0x1B3L		/* byte: boot device (0-2 built-in) */
#define ROM_MEMBASE	0x2A4L		/* long: physical start of RAM */
#define ROM_MEMEND	0x2A8L		/* long: logical end of RAM */

#endif /* LISAADDR_H */
