#include <minix/config.h>
#if (MACHINE == LISA)
/* lisacons.c -- the Minix console: Lisa screen and keyboard, and SCC
 * serial port B.
 *
 * Replaces stcon.c, stvdu.c, stkbd.c and rs232.c of Minix-ST.  The console
 * is mirrored: output goes to the screen (lisavdu.c) and to serial port B,
 * and input from the keyboard (lisakbd.c) and from port B both reach the
 * console, so either can be used.  Serial output is polled; serial input
 * arrives by the level 6 interrupt and is handed to the TTY task through
 * tty_driver_buf, as stkbd.c does for the ST keyboard.  There are no
 * RS232 lines (NR_RS_LINES is 0 on the Lisa), so the rs_* entry points
 * that tty.c and clock.c use are stubs, except rs_flush().
 *
 * SCC set-up follows the Zilog Z8530 register definitions; the SCC clock
 * is 4 MHz (lisaaddr.h).
 */

#include "../kernel.h"
#include <minix/com.h>
#include <signal.h>
#include <sgtty.h>
#include "../proc.h"
#include "../tty.h"
#include "lisaaddr.h"

#define REG8(a)		(*(volatile unsigned char *)(a))

#define THRESHOLD	20	/* chars to accumulate before a message (stkbd.c) */
#define RR0_RX_AVAIL	0x01
#define RR0_TX_EMPTY	0x04
#define DUMP_CHAR	0x14	/* control-T: process table dump (kbdput) */

PRIVATE int cons_ready = 0;	/* SCC has been set up */

PUBLIC void console();

/*===========================================================================*
 *				scc register access			     *
 *===========================================================================*/
PRIVATE void scc_delay()
{
  volatile int i;

  for (i = 0; i < 50; i++)
	;
}

PRIVATE void scc_wr(reg, val)
int reg, val;
{
  REG8(SCC_B_CTRL) = reg;
  scc_delay();
  REG8(SCC_B_CTRL) = val;
  scc_delay();
}

/*===========================================================================*
 *				lisa_cons_init				     *
 *===========================================================================*/
PUBLIC void lisa_cons_init()
{
/* Set up channel B for 9600 baud, 8 bits, 1 stop bit, no parity, with a
 * receive interrupt.  Called from initlisa() before interrupts are on, so
 * that kernel printf works from the start.
 */
  if (cons_ready) return;
  (void) REG8(SCC_B_CTRL);	/* register pointer to 0 */
  scc_wr(9, 0x40);		/* WR9: reset channel B */
  scc_wr(4, 0x44);		/* WR4: x16 clock, 1 stop bit, no parity */
  scc_wr(11, 0x50);		/* WR11: RxC and TxC from the baud rate gen. */
  scc_wr(12, (int) (SCC_PCLK / (2L * 16 * 9600) - 2));	/* WR12: TC low */
  scc_wr(13, 0);		/* WR13: TC high */
  scc_wr(14, 0x03);		/* WR14: BRG from PCLK, enabled */
  scc_wr(3, 0xC1);		/* WR3: 8 bits, receiver on */
  scc_wr(5, 0xEA);		/* WR5: DTR, 8 bits, transmitter on, RTS */
  scc_wr(15, 0x00);		/* WR15: no external/status interrupts */
  scc_wr(1, 0x10);		/* WR1: interrupt on every received char */
  scc_wr(9, 0x0A);		/* WR9: master interrupt enable, no vector */
  cons_ready = 1;
}

/*===========================================================================*
 *				scc_putc				     *
 *===========================================================================*/
PRIVATE void scc_putc(c)
int c;
{
  while ((REG8(SCC_B_CTRL) & RR0_TX_EMPTY) == 0)
	;
  REG8(SCC_B_DATA) = c;
}

/*===========================================================================*
 *				lisa_scc_int				     *
 *===========================================================================*/
PUBLIC void lisa_scc_int()
{
/* Level 6: take every received character from channel B and queue it for
 * the TTY task, like kbdint() in stkbd.c.
 */
  int s = lock();

  while (REG8(SCC_B_CTRL) & RR0_RX_AVAIL)
	kbdput(REG8(SCC_B_DATA) & 0xFF, CONSOLE);
  REG8(SCC_A_CTRL) = 0x38;	/* WR0: reset highest interrupt under service */
  if (tty_buf_count(tty_driver_buf) < THRESHOLD) {
	/* Don't send a message.  Just accumulate.  Let the clock do it. */
	flush_flag++;
  } else
	rs_flush();		/* send the TTY task a message */
  restore(s);
}

/*===========================================================================*
 *				kbdput					     *
 *===========================================================================*/
PUBLIC void kbdput(c, line)
int c;
int line;
{
/* Store the character so the task can get at it later (from stkbd.c).
 * tty_driver_buf[0] is the current count, and tty_driver_buf[2] is the
 * maximum allowed to be stored.
 */
  register int k;

  if (c == DUMP_CHAR) {
	/* Debugging aid, from either console input: the Minix-ST F1 and F2
	 * dumps (stdmp.c), printed at interrupt level.
	 */
	p_dmp();
	map_dmp();
	return;
  }
  if ((k = tty_buf_count(tty_driver_buf)) >= tty_buf_max(tty_driver_buf))
	return;			/* too many characters buffered: discard */
  k <<= 1;			/* each entry uses two bytes */
  tty_driver_buf[k+4] = c;	/* store the char code */
  tty_driver_buf[k+5] = line;	/* which line it came from */
  tty_buf_count(tty_driver_buf)++;
}

/*===========================================================================*
 *				tty_init				     *
 *===========================================================================*/
PUBLIC void tty_init()
{
  struct tty_struct *tp;

  for (tp = &tty_struct[0]; tp < &tty_struct[NR_CONS]; tp++) {
	tp->tty_inhead = tp->tty_inqueue;
	tp->tty_intail = tp->tty_inqueue;
	tp->tty_mode = CRMOD | XTABS | ECHO;
	tp->tty_devstart = (int (*)()) console;
	tp->tty_makebreak = TWO_INTS;
	tp->tty_erase = ERASE_CHAR;
	tp->tty_kill  = KILL_CHAR;
	tp->tty_intr  = INTR_CHAR;
	tp->tty_quit  = QUIT_CHAR;
	tp->tty_xon   = XON_CHAR;
	tp->tty_xoff  = XOFF_CHAR;
	tp->tty_eof   = EOT_CHAR;
  }

  tty_buf_max(tty_driver_buf) = MAX_OVERRUN;	/* limit on input buffering */
  tty_buf_count(tty_driver_buf) = 0;
  lisa_cons_init();
}

/*===========================================================================*
 *				console					     *
 *===========================================================================*/
PUBLIC void console(tp)
register struct tty_struct *tp;	/* tells which terminal is to be used */
{
/* Copy the user's data to the terminal (from stvdu.c). */

  int count = 0;
  char *charptr = (char *)tp->tty_phys;

  vducursor(0);
  while (tp->tty_outleft > 0 && tp->tty_inhibited == RUNNING) {
	out_char(tp, *charptr++);	/* write 1 byte to terminal */
	count++;
	tp->tty_outleft--;
  }
  vducursor(1);
  flush(tp);
  tp->tty_phys += count;	/* advance physical data pointer */
  tp->tty_cum += count;		/* number of characters printed */

  /* If all data has been copied to the terminal, send the reply. */
  if (tp->tty_outleft == 0) finish(tp, tp->tty_cum);
}

/*===========================================================================*
 *				flush					     *
 *===========================================================================*/
PUBLIC void flush(tp)
register struct tty_struct *tp;
{
/* Characters are sent as out_char() gets them; drain anything left in the
 * RAM queue (from stvdu.c).
 */
  register char *rq;

  if (tp->tty_rwords == 0)
	return;
  rq = (char *)tp->tty_ramqueue;
  do {
	if (tp->tty_inhibited == TRUE)
		break;
	out_char(tp, *rq++);
	tp->tty_phys++;
	tp->tty_cum++;
  } while (--tp->tty_rwords != 0);
  vducursor(1);
}

/*===========================================================================*
 *				out_char				     *
 *===========================================================================*/
PUBLIC void out_char(tp, c)
register struct tty_struct *tp;
int c;
{
/* Send one character to the screen and to the serial port.  The screen
 * driver turns newline into CR LF itself when CRMOD is set; for the serial
 * line that is done here.  The caller shows the cursor again afterwards.
 */
  c &= 0xFF;
  vdu_out(c);
  if (c == '\n' && (tp->tty_mode & CRMOD))
	scc_putc('\r');
  scc_putc(c);
}

/*===========================================================================*
 *				putc					     *
 *===========================================================================*/
PUBLIC void putc(c)
int c;
{
/* Used by the kernel's printf() (printk).  Works before tty_init(). */
  if (!cons_ready) lisa_cons_init();
  out_char(&tty_struct[CONSOLE], c);
  vducursor(1);
}

/*===========================================================================*
 *				func_key, dump				     *
 *===========================================================================*/
PUBLIC int func_key(pfx)
int pfx;
{
  return 0;
}

PUBLIC void dump()
{
}

/*===========================================================================*
 *				RS232 entry points			     *
 *===========================================================================*/
PUBLIC void rs_flush()
{
/* Flush the tty_driver_buf by sending a message to TTY (from rs232.c). */
  int s = lock();

  flush_flag = 0;
  if ((tty_buf_count(tty_driver_buf) == 0) && (output_done == 0)) {
	restore(s);
	return;
  }
  interrupt(TTY);
  restore(s);
}

PUBLIC int tty_o_done()
{
/* Called by tty.c when output_done is set; no RS232 line ever sets it. */
  return(0);
}

PUBLIC void init_rs232()	{ }
PUBLIC void set_uart(line, mode, speeds) int line, mode, speeds; { }
PUBLIC void rs_sig(tp) struct tty_struct *tp; { }
PUBLIC void rs_out_char(tp, c) struct tty_struct *tp; int c; { }
#endif
