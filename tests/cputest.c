/* cputest.c -- the stack array and indexed-read code from a debugging
 * version of the Lisa test kernel, as a Minix program, to compare 68000
 * emulators. Built with the same flags as the Lisa kernel (-mshort -O).
 * It runs correctly under Hatari. The wrong values seen in LisaEm at the
 * time turned out to be a boot block bug (docs/lisaem.md), not the CPU.
 */

static char out[80];
static int outn;

static void putch(c)
int c;
{
	out[outn++] = c;
}

static void rawdump(addr, len)
char *addr;
long len;
{
	char hex[16];
	int i;
	long n;

	for (i = 0; i < 10; i++)
		hex[i] = '0' + i;
	for (i = 10; i < 16; i++)
		hex[i] = 'A' + i - 10;
	for (n = 0; n < len; n++) {
		putch(hex[(addr[n] >> 4) & 15]);
		putch(hex[addr[n] & 15]);
	}
	putch('\n');
}

static char data[] = {
	0x01, 0x23, 0x45, 0x67, (char) 0x89, (char) 0xAB, (char) 0xCD, (char) 0xEF,
	(char) 0xFE, (char) 0xDC, (char) 0xBA, (char) 0x98, 0x76, 0x54, 0x32, 0x10,
};

int main()
{
	rawdump(data, (long) sizeof(data));
	write(1, "expect 0123456789ABCDEFFEDCBA9876543210\ngot    ", 47);
	write(1, out, outn);
	return 0;
}
