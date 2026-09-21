#include <minix/config.h>
#if (MACHINE == LISA)
/* lisakbd.c -- the Lisa keyboard, read from the COPS.
 *
 * The COPS microcontroller on the I/O board sends bytes through the COPS
 * VIA; each one raises level 2 (lisa_level2 in lisamain.c calls
 * lisa_cops_int).  A byte is either a key transition or part of a longer
 * report:
 *
 *   bit 7 set: key went down, clear: key went up; bits 0-6: key code
 *   0x80	next byte is a reset code: 0xFF keyboard COPS failure, 0xFE I/O
 *		COPS failure, 0xFD keyboard unplugged, 0xFC clock timer, 0xFB
 *		soft power switch, 0xE0-0xEF clock reading (5 more bytes of
 *		BCD follow), anything else the keyboard's ID after a reset
 *
 * The clock reading answers the read-clock command (lisa_read_rtc).  The
 * low nibble of 0xEn is the year, 0-15 from 1980 (the COPS keeps no more);
 * the five bytes hold, in BCD nibbles, the day of the year (1-366), hour,
 * minute, second and tenth of a second [U sys/kb.c, sys/rtc.c].  The time
 * of day is set from it with clock_set_boot_time() (clock.c).
 *   0x00	next two bytes are a mouse movement (x, then y)
 *   0x01-0x08	disk inserted or button pressed, parallel port plug, mouse
 *		button, mouse plug (up or down in bit 7); ignored here
 *
 * The protocol and the key code layout of the US keyboard are from the
 * UniPlus sources (include/sys/cops.h, include/sys/keyboard.h, sys/kb.c),
 * read for the facts; the tables below are written for Minix.  As in
 * UniPlus, Command is the control key, both Option keys send ESC, Enter
 * sends newline and the arrow keys send VT100 cursor sequences.  Unlike
 * UniPlus, Clear sends ESC and Shift-Clear DEL (Minix's interrupt
 * character): LisaEm types the host's Escape key as Clear, and an editor
 * user pressing Escape should not interrupt the editor.  A key held down
 * repeats after 0.4 s, 15 times a second (kb_timer, from the clock
 * interrupt), like the Atari ST keyboard driver stkbd.c.
 *
 * Characters go into tty_driver_buf with kbdput(), shared with the serial
 * console (lisacons.c), so input from either reaches the console.
 */

#include "../kernel.h"
#include <minix/com.h>
#include "../proc.h"
#include "../tty.h"
#include "lisaaddr.h"

#define REG8(a)		(*(volatile unsigned char *)(a))

#define THRESHOLD	20	/* chars to accumulate before a message */

#define KEY_DOWN	0x80
#define KC_LEFT		0x22	/* key codes of keys handled specially */
#define KC_RIGHT	0x23
#define KC_UP		0x27
#define KC_DOWN		0x2B
#define KC_LOCK		0x7D
#define KC_SHIFT	0x7E
#define KC_COMMAND	0x7F

#define RC_CLOCK	0xE0	/* reset codes 0xE0-0xEF: clock reading */

/* States of the byte stream. */
#define S_KEY		0	/* expecting a key code or a prefix */
#define S_RESET		1	/* byte after 0x80 */
#define S_MOUSE_X	2	/* bytes after 0x00 */
#define S_MOUSE_Y	3
#define S_CLOCK		4	/* the 5 bytes after a clock reset code */

PUBLIC int keypad = FALSE;	/* DECKPAM, set by lisavdu.c (unused) */
PUBLIC int app_mode = FALSE;	/* DECCKM: arrows send ESC O x, not ESC [ x */

PRIVATE int kb_state = S_KEY;
PRIVATE int kb_count;		/* clock bytes still to come */
PRIVATE int rtc_year;		/* clock reading being collected */
PRIVATE unsigned char rtc_bytes[5];
PRIVATE int kb_shift, kb_lock, kb_command;
PRIVATE int repeat_code;	/* key code being repeated, 0 for none */
PRIVATE int repeat_tics;	/* clock ticks to the next repeat */

#define REPEAT_DELAY	24	/* ticks (60 Hz) before the first repeat */
#define REPEAT_RATE	4	/* ticks between repeats */

/* ASCII for key codes 0x20-0x7F without and with Shift; 0 for keys that
 * give no character.  Rows 0x00-0x1F and 0x30-0x3F have no keys.
 */
PRIVATE char kb_normal[96] = {
/* 0x20: keypad */
  033,  '-',  0,    0,    '7',  '8',  '9',  0,
  '4',  '5',  '6',  0,    '.',  '2',  '3',  '\n',
/* 0x30 */
  0,    0,    0,    0,    0,    0,    0,    0,
  0,    0,    0,    0,    0,    0,    0,    0,
/* 0x40 */
  '-',  '=',  '\\', 0,    'p',  '\b', '\n', 0,
  '\r', '0',  0,    0,    '/',  '1',  033,  0,
/* 0x50 */
  '9',  '0',  'u',  'i',  'j',  'k',  '[',  ']',
  'm',  'l',  ';',  '\'', ' ',  ',',  '.',  'o',
/* 0x60 */
  'e',  '6',  '7',  '8',  '5',  'r',  't',  'y',
  '`',  'f',  'g',  'h',  'v',  'c',  'b',  'n',
/* 0x70 */
  'a',  '2',  '3',  '4',  '1',  'q',  's',  'w',
  '\t', 'z',  'x',  'd',  033,  0,    0,    0,
};

PRIVATE char kb_shifted[96] = {
/* 0x20: keypad */
  0x7F, '-',  '+',  '*',  '7',  '8',  '9',  '/',
  '4',  '5',  '6',  ',',  '.',  '2',  '3',  '\n',
/* 0x30 */
  0,    0,    0,    0,    0,    0,    0,    0,
  0,    0,    0,    0,    0,    0,    0,    0,
/* 0x40 */
  '_',  '+',  '|',  0,    'P',  '\b', '\n', 0,
  '\r', '0',  0,    0,    '?',  '1',  033,  0,
/* 0x50 */
  '(',  ')',  'U',  'I',  'J',  'K',  '{',  '}',
  'M',  'L',  ':',  '"',  ' ',  '<',  '>',  'O',
/* 0x60 */
  'E',  '^',  '&',  '*',  '%',  'R',  'T',  'Y',
  '~',  'F',  'G',  'H',  'V',  'C',  'B',  'N',
/* 0x70 */
  'A',  '@',  '#',  '$',  '!',  'Q',  'S',  'W',
  '\t', 'Z',  'X',  'D',  033,  0,    0,    0,
};

FORWARD void cops_byte();
FORWARD void key_down();
FORWARD void rtc_reading();
FORWARD void flush_input();

/*===========================================================================*
 *				lisa_kbd_init				     *
 *===========================================================================*/
PUBLIC void lisa_kbd_init()
{
/* Enable the COPS VIA's data interrupt (CA1) only.  The rest of the VIA
 * (handshake, speaker, timer) is left as the boot ROM set it.
 */
  REG8(VIA_COPS_REG(VIA_IER)) = 0x7F;
  REG8(VIA_COPS_REG(VIA_IFR)) = 0x7F;
  REG8(VIA_COPS_REG(VIA_IER)) = 0x80 | VIA_IRQ_CA1;
}

/*===========================================================================*
 *				lisa_cops_int				     *
 *===========================================================================*/
PUBLIC void lisa_cops_int()
{
/* Level 2: take one byte from the COPS, if it has one; the COPS raises the
 * interrupt again for the next.  Reading in a loop while the flag shows
 * CA1 hung LisaEm: once the mouse has moved over its window, it reports
 * CA1 whenever its key queue is empty, and each read of port A makes up
 * another mouse report (lisa/io_board/cops.c via1_ira, via6522.c IFR1).
 * UniPlus sys/kb.c also reads one byte per interrupt.
 */
  int s = lock();

  if (REG8(VIA_COPS_REG(VIA_IFR)) & VIA_IRQ_CA1)
	cops_byte(REG8(VIA_COPS_REG(VIA_ORA)) & 0xFF);
  flush_input();
  restore(s);
}

/*===========================================================================*
 *				kb_timer				     *
 *===========================================================================*/
PUBLIC void kb_timer()
{
/* Called by the clock interrupt every tick: repeat a held key. */
  int s = lock();

  if (repeat_code != 0 && --repeat_tics == 0) {
	key_down(repeat_code);
	repeat_tics = REPEAT_RATE;
	flush_input();
  }
  restore(s);
}

PRIVATE void flush_input()
{
  if (tty_buf_count(tty_driver_buf) < THRESHOLD) {
	/* Don't send a message.  Just accumulate.  Let the clock do it. */
	flush_flag++;
  } else
	rs_flush();		/* send the TTY task a message */
}

/*===========================================================================*
 *				cops_byte				     *
 *===========================================================================*/
PRIVATE void cops_byte(b)
int b;
{
  int code = b & 0x7F;
  int down = b & KEY_DOWN;

  switch (kb_state) {
  case S_RESET:
	kb_state = S_KEY;
	if (b >= RC_CLOCK && b <= RC_CLOCK + 0x0F) {
		kb_state = S_CLOCK;
		kb_count = 5;
		rtc_year = b & 0x0F;
	}
	/* Other reset codes (failures, unplug, soft power, keyboard ID)
	 * are ignored.  A reset releases all keys.
	 */
	kb_shift = kb_command = 0;
	repeat_code = 0;
	return;
  case S_MOUSE_X:
	kb_state = S_MOUSE_Y;
	return;
  case S_MOUSE_Y:
	kb_state = S_KEY;
	return;
  case S_CLOCK:
	rtc_bytes[5 - kb_count] = b;
	if (--kb_count == 0) {
		kb_state = S_KEY;
		rtc_reading();
	}
	return;
  }

  switch (code) {
  case 0:
	kb_state = down ? S_RESET : S_MOUSE_X;
	return;
  case KC_SHIFT:
	kb_shift = down;
	return;
  case KC_LOCK:
	kb_lock = down;
	return;
  case KC_COMMAND:
	kb_command = down;
	return;
  }
  if (down) {
	key_down(code);
	if (code >= 0x20) {		/* a real key: repeat it while held */
		repeat_code = code;
		repeat_tics = REPEAT_DELAY;
	}
  } else if (code == repeat_code)
	repeat_code = 0;
}

/*===========================================================================*
 *				lisa_read_rtc				     *
 *===========================================================================*/
PUBLIC void lisa_read_rtc()
{
/* Ask the COPS for a clock reading.  Protocol as UniPlus l2copscmd(): put
 * the command in port A, wait for CRDY to go low (the COPS is ready), then
 * drive port A for a moment.  If CRDY is already low the moment was
 * missed; try again.
 */
  int s, tries, wait;
  volatile int delay;

  for (tries = 0; tries < 100; tries++) {
	s = lock();
	REG8(VIA_COPS_REG(VIA_ORA_NH)) = COPS_READ_CLOCK;
	if ((REG8(VIA_COPS_REG(VIA_ORB)) & COPS_CRDY) != 0) {
		for (wait = 0; wait < 1000; wait++)
			if ((REG8(VIA_COPS_REG(VIA_ORB)) & COPS_CRDY) == 0)
				break;
		if (wait < 1000) {
			REG8(VIA_COPS_REG(VIA_DDRA)) = 0xFF;
			for (delay = 0; delay < 16; delay++)
				;
			REG8(VIA_COPS_REG(VIA_DDRA)) = 0;
			restore(s);
			return;
		}
	}
	restore(s);
	for (delay = 0; delay < 1000; delay++)	/* let interrupts in */
		;
  }
  printf("Lisa clock: the COPS did not take the read command\n");
}

/*===========================================================================*
 *				rtc_reading				     *
 *===========================================================================*/
PRIVATE void rtc_reading()
{
/* A complete clock reading is in rtc_year and rtc_bytes: set the time. */
  register unsigned char *p = rtc_bytes;
  int year, day, hour, min, sec, y;
  long t;

#define HI(b)	(((b) >> 4) & 0x0F)
#define LO(b)	((b) & 0x0F)
  year = 1980 + rtc_year;
  day = HI(p[0]) * 100 + LO(p[0]) * 10 + HI(p[1]);
  hour = LO(p[1]) * 10 + HI(p[2]);
  min = LO(p[2]) * 10 + HI(p[3]);
  sec = LO(p[3]) * 10 + HI(p[4]);
  if (day < 1 || day > 366 || hour > 23 || min > 59 || sec > 59) {
	printf("Lisa clock: bad reading %d day %d %d:%d:%d\n",
		year, day, hour, min, sec);
	return;
  }
  t = 0;
  for (y = 1970; y < year; y++)
	t += (y % 4 == 0) ? 366 : 365;
  t += day - 1;
  t = ((t * 24 + hour) * 60 + min) * 60 + sec;
  clock_set_boot_time((time_t) t);
}

/*===========================================================================*
 *				key_down				     *
 *===========================================================================*/
PRIVATE void key_down(code)
int code;
{
  int c;

  if (code < 0x20)
	return;			/* disk, plug and mouse button events */
  if (!kb_shift) {
	switch (code) {
	case KC_UP:	c = 'A';	break;
	case KC_DOWN:	c = 'B';	break;
	case KC_RIGHT:	c = 'C';	break;
	case KC_LEFT:	c = 'D';	break;
	default:	c = 0;
	}
	if (c != 0) {
		kbdput('\033', CONSOLE);
		kbdput(app_mode ? 'O' : '[', CONSOLE);
		kbdput(c, CONSOLE);
		return;
	}
  }
  c = (kb_shift ? kb_shifted : kb_normal)[code - 0x20] & 0xFF;
  if (c == 0)
	return;
  if (kb_lock && c >= 'a' && c <= 'z')
	c += 'A' - 'a';
  if (kb_command)
	c &= 0x1F;
  kbdput(c, CONSOLE);
}
#endif
