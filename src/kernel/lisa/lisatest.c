/* lisatest.c -- phase 1 test kernel for the Apple Lisa (PLAN.md phase 1).
 *
 * Not Minix yet: proves the boot block, the memory map, the serial console
 * and the clock interrupt. It prints the boot parameters and the context 0
 * MMU segments on SCC port B, counts vertical retrace interrupts for ten
 * seconds, then provokes a bus error to show the fault handler.
 *
 * K&R C for m68k-minix-gcc -mshort (docs/toolchain.md).
 */

#include "lisaaddr.h"

#define REG8(a)		(*(volatile unsigned char *)(a))

extern long boot_dev, boot_membase, boot_memend;
extern volatile long ticks, cops_events;	/* changed by interrupt handlers */
extern long mmu_read();
extern int spl0();

/* ---- SCC port B, polled ------------------------------------------------ */

#define SCC_RR0_TXEMPTY	0x04

/* Channel B set-up, one register number and value per pair, from the
 * Zilog Z8530 register definitions: reset channel B; x16 clock, 1 stop bit,
 * no parity; baud rate generator time constant for 9600 baud from the
 * 4 MHz clock, TC = PCLK / (2 * 16 * baud) - 2 = 11; receive and transmit
 * clocks from the generator; generator on, clocked from PCLK; 8-bit
 * receive enabled; 8-bit transmit enabled with DTR and RTS; no interrupts.
 */
static unsigned char scc_setup[] = {
	9, 0x40,		/* WR9: channel B reset */
	4, 0x44,		/* WR4: x16 clock, 1 stop bit */
	11, 0x50,		/* WR11: RxC and TxC from the BRG */
	12, 11,			/* WR12: time constant, low */
	13, 0,			/* WR13: time constant, high */
	14, 0x03,		/* WR14: BRG from PCLK, enabled */
	3, 0xC1,		/* WR3: 8 bits, receiver on */
	5, 0xEA,		/* WR5: DTR, 8 bits, transmitter on, RTS */
	1, 0x00,		/* WR1: no interrupts */
	15, 0x00,		/* WR15: no external/status interrupts */
};

static void scc_delay()
{
	volatile int i;

	for (i = 0; i < 50; i++)
		;
}

static void scc_init()
{
	int i;

	(void) REG8(SCC_B_CTRL);	/* point the register pointer at 0 */
	for (i = 0; i < sizeof(scc_setup); i += 2) {
		REG8(SCC_B_CTRL) = scc_setup[i];
		scc_delay();
		REG8(SCC_B_CTRL) = scc_setup[i + 1];
		scc_delay();
	}
}

static void putch(c)
int c;
{
	if (c == '\n')
		putch('\r');
	while ((REG8(SCC_B_CTRL) & SCC_RR0_TXEMPTY) == 0)
		;
	REG8(SCC_B_DATA) = c;
}

static void puts(s)
char *s;
{
	while (*s)
		putch(*s++);
}

static void puthex(v, digits)
long v;
int digits;
{
	while (digits-- > 0)
		putch("0123456789ABCDEF"[(v >> (4 * digits)) & 0xF]);
}

static void putdec(v)
long v;
{
	char buf[12];
	int i = 0;

	if (v < 0) {
		putch('-');
		v = -v;
	}
	do {
		buf[i++] = '0' + v % 10;
		v /= 10;
	} while (v != 0);
	while (i > 0)
		putch(buf[--i]);
}

/* ---- MMU ------------------------------------------------------------------ */

static char *acc_name(slr)
long slr;
{
	switch ((int) slr & MMU_ACC_MASK) {
	case MMU_ACC_ROSTK:	return "ro-stack";
	case MMU_ACC_RO:	return "ro";
	case MMU_ACC_RWSTK:	return "rw-stack";
	case MMU_ACC_RW:	return "rw";
	case MMU_ACC_IO:	return "io";
	case MMU_ACC_INVAL:	return "invalid";
	case MMU_ACC_SPIO:	return "special-io";
	}
	return "?";
}

/* Print every context 0 segment that is not invalid: segment number,
 * logical base, access type, limit byte, origin in pages and as a physical
 * address.
 */
static void dump_segments()
{
	int seg, invalid = 0;
	long base, slr, sor;

	puts("context 0 segments (seg logical access slr sor physical):\n");
	for (seg = 0; seg < MMU_NSEGS; seg++) {
		base = (long) seg * MMU_SEGSIZE;
		slr = mmu_read(base + MMU_SLR);
		sor = mmu_read(base + MMU_SOR);
		if ((slr & MMU_ACC_MASK) == MMU_ACC_INVAL) {
			invalid++;
			continue;
		}
		puts("  ");
		puthex((long) seg, 2);
		puts(" ");
		puthex(base, 6);
		puts(" ");
		puts(acc_name(slr));
		puts(" ");
		puthex(slr, 3);
		puts(" ");
		puthex(sor, 3);
		puts(" ");
		puthex(sor << 9, 6);
		puts("\n");
	}
	puts("  ");
	putdec((long) invalid);
	puts(" invalid segments\n");
}

/* ---- faults ------------------------------------------------------------- */

static char *regname[] = {
	"d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
	"a0", "a1", "a2", "a3", "a4", "a5", "a6",
};

static void dump_regs(r)
long *r;
{
	int i;

	for (i = 0; i < 15; i++) {
		puts(i % 4 == 0 ? "\n  " : "  ");
		puts(regname[i]);
		puts("=");
		puthex(r[i], 8);
	}
	puts("\n");
}

/* Frame on the stack: vector word, 15 registers, then the 68000 group 0
 * frame: function code word, access address, instruction register, SR, PC.
 */
void group0_fault(f)
char *f;
{
	short vec, fc, ir, sr;
	long addr, pc;
	char *g = f + 2 + 15 * 4;

	vec = *(short *) f;
	fc = *(short *) g;
	addr = *(long *) (g + 2);
	ir = *(short *) (g + 6);
	sr = *(short *) (g + 8);
	pc = *(long *) (g + 10);
	puts(vec == 2 ? "\nbus error" : "\naddress error");
	puts(": access address ");
	puthex(addr, 8);
	puts(" function code ");
	puthex((long) fc, 4);
	puts(" (");
	puts(fc & 0x10 ? "read" : "write");
	puts(") instruction ");
	puthex((long) ir, 4);
	puts(" sr ");
	puthex((long) sr, 4);
	puts(" pc ");
	puthex(pc, 8);
	dump_regs((long *) (f + 2));
	puts("halted\n");
	for (;;)
		;
}

/* Frame on the stack: vector word, 15 registers, SR, PC. */
void other_fault(f)
char *f;
{
	short vec, sr;
	long pc;

	vec = *(short *) f;
	sr = *(short *) (f + 2 + 15 * 4);
	pc = *(long *) (f + 2 + 15 * 4 + 2);
	puts("\nunexpected exception: vector ");
	putdec((long) vec);
	puts(" sr ");
	puthex((long) sr, 4);
	puts(" pc ");
	puthex(pc, 8);
	dump_regs((long *) (f + 2));
	puts("halted\n");
	for (;;)
		;
}

/* ---- main ---------------------------------------------------------------- */

void main()
{
	long t, last, second;
	volatile long probe;

	scc_init();
	/* LisaEm connects serial port B to its host backend at the first SCC
	 * access; give a program on the host time to open it.
	 */
	for (probe = 0; probe < 200000L; probe++)
		;
	puts("\n\nMinix/Lisa phase 1 test kernel\n");
	puts("boot device ");
	puthex(boot_dev, 2);
	puts("  physical RAM base ");
	puthex(boot_membase, 8);
	puts("  logical end of RAM ");
	puthex(boot_memend, 8);
	puts(" (");
	putdec(boot_memend >> 10);
	puts(" KB)\n");
	dump_segments();

	/* Mask the VIAs, which share level 1 and 2, and turn on the vertical
	 * retrace interrupt.
	 */
	REG8(VIA_PAR_REG(VIA_IER)) = 0x7F;
	REG8(VIA_COPS_REG(VIA_IER)) = 0x7F;
	REG8(VRT_ON) = 0;
	(void) spl0();

	puts("counting vertical retrace interrupts\n");
	last = 0;
	for (second = 1; second <= 10; second++) {
		while ((t = ticks) < last + 60)
			;
		last = t;
		puts("tick ");
		putdec(t);
		puts("  COPS interrupts ");
		putdec(cops_events);
		puts("\n");
	}

	puts("reading unmapped address 800000 to test the bus error handler\n");
	probe = *(long *) 0x800000L;
	puts("no bus error: read ");
	puthex(probe, 8);
	puts("\nhalted\n");
}

