#include <minix/config.h>
#if (MACHINE == LISA)
/* lisavdu.c -- the Lisa's built-in screen as a Minix console terminal.
 *
 * Derived from stvdu.c (Minix-ST), which understands a subset of the ANSI
 * (VT100) escape sequences; the colour, sound and ST-specific parts are
 * gone.  The Lisa display is 720 x 364 pixels, one bit per pixel, a set
 * bit black, 90 bytes per scan line, in a 32 KB page of main memory that
 * the video latch selects.  Characters are 8 x 9 cells: the ST's 8 x 8
 * font (stfnt.c) and one blank scan line, giving 90 columns and 40 rows,
 * the same text size as UniPlus.
 *
 * Screen placement follows UniPlus sys/bm.c: the page is the top 32 KB of
 * the RAM the boot ROM reports, and the latch holds the physical page
 * address divided by 32 KB.  Logical addresses in the boot context are
 * physical ones less the ROM's MEMBASE.
 *
 * Entry points:
 *   vdu_init:	take over the screen (called before interrupts are on)
 *   vdu_out:	display one character, collecting escape sequences
 *   vducursor:	show or hide the cursor
 */

#include "../kernel.h"
#include <sgtty.h>
#include "../tty.h"
#include "lisaaddr.h"

#define REG8(a)		(*(volatile unsigned char *)(a))

#define NCOL		90	/* characters on a row */
#define NROW		40	/* character rows */
#define BYT_LIN		90	/* bytes in a video line */
#define LINC		9	/* video lines in a character */
#define FONT_LINES	8	/* of which drawn from the font */
#define BYTR		(BYT_LIN * LINC)	/* bytes in a row of characters */

extern long lisa_membase, lisa_memend;	/* lisampx.s */
extern int keypad, app_mode;		/* lisakbd.c */

PRIVATE struct vduinfo {
	char	*vram;		/* base of video ram */
	char	*curs;		/* cursor position in video RAM */
	char	attr;		/* 1: reverse video */
	int	ccol;		/* current char column */
	int	crow;		/* current char row */
	char	savattr;	/* saved attribute byte */
	int	savccol;	/* saved char column */
	int	savcrow;	/* saved char row */
	char	vbuf[20];	/* partial escape sequence */
	char	*next;		/* next char in vbuf[] */
} vduinfo;

PRIVATE int vdu_ready = 0;
PRIVATE int cursor_shown = 0;

FORWARD void vductrl();
FORWARD void vduansi();
FORWARD void vduesc();
FORWARD int vduparam();
FORWARD void cpyline();
FORWARD void cpychar();
FORWARD void clrarea();
FORWARD void clrline();
FORWARD void clrchar();
FORWARD void moveto();
FORWARD void paint();

/*===========================================================================*
 *				vdu_out					     *
 *===========================================================================*/
PUBLIC void vdu_out(c)
register int c;			/* character to be output */
{
/* Send a character to the screen, collecting escape sequences. */
  register struct vduinfo *v = &vduinfo;

  if (!vdu_ready)
	return;
  vducursor(0);
  c &= 0xFF;
  if (c == 0x7F)
	return;
  if ((c & 0140) == 0) {	/* control character */
	vductrl(c);
	return;
  }
  if (c & 0x80)			/* no characters above 0x7F in the font */
	c &= 0x1F;
  if (v->next == 0) {		/* normal character */
	paint(c);
	moveto(v->crow, v->ccol + 1);
	return;
  }
  if (v->next == v->vbuf && c == '[') {	/* start of ANSI sequence */
	*v->next++ = (char)c;
	return;
  }
  if (c >= 060 && (v->next == v->vbuf || v->vbuf[0] != '[')) {
	vduesc(c);		/* end of non-ANSI escape sequence */
	v->next = 0;
	return;
  }
  if (c >= 0100) {		/* end of ANSI sequence */
	vduansi(c);
	v->next = 0;
	return;
  }
  *v->next = (char)c;		/* part of escape sequence */
  if (v->next < &v->vbuf[sizeof(v->vbuf)])
	v->next++;
}

/*===========================================================================*
 *				vductrl					     *
 *===========================================================================*/
PRIVATE void vductrl(c)
int c;
{
  register struct vduinfo *v = &vduinfo;
  register int i;
  register struct tty_struct *tp = &tty_struct[CONSOLE];

  switch (c) {
  case 007: /* BEL: no sound yet */
	return;
  case 010: /* BS */
	moveto(v->crow, v->ccol - 1);
	return;
  case 011: /* HT */
	do
		if ((tp->tty_mode & XTABS) == XTABS)
			vdu_out(' ');
		else
			moveto(v->crow, v->ccol + 1);
	while (v->ccol & TAB_MASK);
	return;
  case 012: /* LF */
	if (tp->tty_mode & CRMOD)
		moveto(v->crow, 0);
	/* fall through */
  case 013: /* VT */
  case 014: /* FF */
	if (v->crow == NROW - 1) {
		for (i = 0; i < NROW - 1; i++)
			cpyline(i + 1, i);
		clrline(i);
	} else
		moveto(v->crow + 1, v->ccol);
	return;
  case 015: /* CR */
	moveto(v->crow, 0);
	return;
  case 030: /* CAN */
  case 032: /* SUB */
	v->next = 0;
	return;
  case 033: /* ESC */
	v->next = v->vbuf;
	return;
  default:
	return;
  }
}

/*===========================================================================*
 *				vduansi					     *
 *===========================================================================*/
PRIVATE void vduansi(c)
int c;
{
/* Execute an ANSI escape sequence. */
  register struct vduinfo *v = &vduinfo;
  register int i;
  register int j;

  if (v->next >= &v->vbuf[sizeof(v->vbuf)])
	return;
  *v->next = 0;
  v->next = &v->vbuf[1];
  j = vduparam();
  if ((i = j) <= 0)
	i = 1;
  switch (c) {
  case 'A': /* CUU: cursor up */
	if ((i = v->crow - i) < 0)
		i = 0;
	moveto(i, v->ccol);
	return;
  case 'B': /* CUD: cursor down */
	if ((i += v->crow) >= NROW)
		i = NROW - 1;
	moveto(i, v->ccol);
	return;
  case 'C': /* CUF: cursor forward */
	if ((i += v->ccol) >= NCOL)
		i = NCOL - 1;
	moveto(v->crow, i);
	return;
  case 'D': /* CUB: cursor backward */
	if ((i = v->ccol - i) < 0)
		i = 0;
	moveto(v->crow, i);
	return;
  case 'H': /* CUP: cursor position */
  case 'f': /* HVP: horizontal and vertical position */
	j = vduparam();
	if (j <= 0)
		j = 1;
	if (i > NROW)
		i = NROW;
	if (j > NCOL)
		j = NCOL;
	moveto(i - 1, j - 1);
	return;
  case 'J': /* ED: erase in display */
	if (j <= 0)
		clrarea(v->crow, v->ccol, NROW - 1, NCOL - 1);
	else if (j == 1)
		clrarea(0, 0, v->crow, v->ccol);
	else if (j == 2)
		clrarea(0, 0, NROW - 1, NCOL - 1);
	return;
  case 'K': /* EL: erase in line */
	if (j <= 0)
		clrarea(v->crow, v->ccol, v->crow, NCOL - 1);
	else if (j == 1)
		clrarea(v->crow, 0, v->crow, v->ccol);
	else if (j == 2)
		clrarea(v->crow, 0, v->crow, NCOL - 1);
	return;
  case 'm': /* SGR: set graphic rendition */
	do {
		if (j <= 0)
			v->attr = 0;
		else if (j == 4 || j == 7)
			v->attr = 1;
	} while ((j = vduparam()) >= 0);
	return;
  case 'L': /* IL: insert line */
	if (i > NROW - v->crow)
		i = NROW - v->crow;
	for (j = NROW - 1; j >= v->crow + i; j--)
		cpyline(j - i, j);
	while (--i >= 0)
		clrline(j--);
	return;
  case 'M': /* DL: delete line */
	if (i > NROW - v->crow)
		i = NROW - v->crow;
	for (j = v->crow; j < NROW - i; j++)
		cpyline(j + i, j);
	while (--i >= 0)
		clrline(j++);
	return;
  case '@': /* ICH: insert char */
	j = NCOL - v->ccol;
	if (i > j)
		i = j;
	j -= i;
	while (--j >= 0)
		cpychar(v->crow, v->ccol + j, v->crow, v->ccol + j + i);
	clrarea(v->crow, v->ccol, v->crow, v->ccol + i - 1);
	return;
  case 'P': /* DCH: delete char */
	j = NCOL - v->ccol;
	if (i > j)
		i = j;
	j -= i;
	while (--j >= 0)
		cpychar(v->crow, NCOL - 1 - j, v->crow, NCOL - 1 - j - i);
	clrarea(v->crow, NCOL - i, v->crow, NCOL - 1);
	return;
  case 'l': /* RM: reset mode */
  case 'h': /* SM: set mode */
	if (v->next[0] == '?' && v->next[1] == '1')	/* DECCKM */
		app_mode = c == 'l' ? TRUE : FALSE;
	return;
  default:
	return;
  }
}

/*===========================================================================*
 *				vduesc					     *
 *===========================================================================*/
PRIVATE void vduesc(c)
int c;
{
/* Execute a non-ANSI escape sequence. */
  register struct vduinfo *v = &vduinfo;
  register int i;

  if (v->next >= &v->vbuf[sizeof(v->vbuf)-1])
	return;
  *v->next = (char)c;
  switch (v->vbuf[0]) {
  case '8': /* DECRC: restore cursor */
	v->ccol = v->savccol;
	v->crow = v->savcrow;
	v->attr = v->savattr;
	moveto(v->crow, v->ccol);
	return;
  case '7': /* DECSC: save cursor */
	v->savccol = v->ccol;
	v->savcrow = v->crow;
	v->savattr = v->attr;
	return;
  case '=': /* DECKPAM: keypad application mode */
	keypad = TRUE;
	return;
  case '>': /* DECKPNM: keypad numeric mode */
	keypad = FALSE;
	return;
  case 'E': /* NEL: next line */
	vductrl(015);
	/* fall through */
  case 'D': /* IND: index */
	vductrl(012);
	return;
  case 'M': /* RI: reverse index */
	if (v->crow == 0) {
		for (i = NROW - 1; i > 0; i--)
			cpyline(i - 1, i);
		clrline(i);
	} else
		moveto(v->crow - 1, v->ccol);
	return;
  case 'c': /* RIS: reset to initial state */
	v->attr = 0;
	moveto(0, 0);
	clrarea(0, 0, NROW - 1, NCOL - 1);
	keypad = FALSE;
	app_mode = FALSE;
	return;
  default:
	return;
  }
}

/*===========================================================================*
 *				vduparam				     *
 *===========================================================================*/
PRIVATE int vduparam()
{
/* Compute the next parameter of an ANSI sequence; -1 if none. */
  register struct vduinfo *v = &vduinfo;
  register int c;
  register int i;

  i = -1;
  c = *v->next++;
  if (c >= '0' && c <= '9') {
	i = 0;
	do {
		i *= 10;
		i += (c - '0');
		c = *v->next++;
	} while (c >= '0' && c <= '9');
  }
  if (c != ';')
	v->next--;
  return(i);
}

/*===========================================================================*
 *				manipulate video ram			     *
 *===========================================================================*/
PRIVATE void cpyline(r1, r2)
int r1, r2;
{
/* Copy character row r1 to r2. */
  register long *src;
  register long *dst;
  register char *s, *d;
  register int i;

  src = (long *) &vduinfo.vram[r1 * BYTR];
  dst = (long *) &vduinfo.vram[r2 * BYTR];
  i = BYTR >> 2;
  while (--i >= 0)
	*dst++ = *src++;
  s = (char *) src;
  d = (char *) dst;
  i = BYTR & 3;
  while (--i >= 0)
	*d++ = *s++;
}

PRIVATE void cpychar(r1, c1, r2, c2)
int r1, c1, r2, c2;
{
/* Copy character (r1,c1) to (r2,c2). */
  register char *src;
  register char *dst;
  register int nl;

  src = &vduinfo.vram[(r1 * BYTR) + c1];
  dst = &vduinfo.vram[(r2 * BYTR) + c2];
  nl = LINC;
  do {
	*dst = *src;
	src += BYT_LIN;
	dst += BYT_LIN;
  } while (--nl != 0);
}

PRIVATE void clrarea(r1, c1, r2, c2)
int r1, c1, r2, c2;
{
/* Clear the part of the screen between two points, inclusive. */
  if (++c2 == NCOL) {
	c2 = 0;
	r2++;
  }
  if (c1 > 0 && r1 < r2) {
	do
		clrchar(r1, c1);
	while (++c1 < NCOL);
	c1 = 0;
	r1++;
  }
  while (r1 < r2)
	clrline(r1++);
  while (c1 < c2)
	clrchar(r1, c1++);
}

PRIVATE void clrline(r)
int r;
{
/* Clear character row r. */
  register long *p;
  register char *q;
  register int i;

  p = (long *) &vduinfo.vram[r * BYTR];
  i = BYTR >> 2;
  while (--i >= 0)
	*p++ = 0;
  q = (char *) p;
  i = BYTR & 3;
  while (--i >= 0)
	*q++ = 0;
}

PRIVATE void clrchar(r, c)
int r, c;
{
/* Clear character (r,c). */
  register char *p;
  register int nl;

  p = &vduinfo.vram[(r * BYTR) + c];
  nl = LINC;
  do {
	*p = 0;
	p += BYT_LIN;
  } while (--nl != 0);
}

/*===========================================================================*
 *				moveto					     *
 *===========================================================================*/
PRIVATE void moveto(r, c)
int r, c;
{
  register struct vduinfo *v = &vduinfo;

  if (r < 0 || r >= NROW || c < 0 || c > NCOL)
	return;
  v->crow = r;
  v->ccol = c;
  if (c == NCOL)
	c--;			/* show cursor in last column */
  v->curs = &v->vram[(r * BYTR) + c];
}

/*===========================================================================*
 *				paint					     *
 *===========================================================================*/
PRIVATE void paint(c)
int c;
{
/* Copy a character from the font into video memory.  A set bit is black
 * on the Lisa, so the font is copied as it is for black on white.
 */
  register unsigned char *vp;
  register unsigned char *fp;
  register int nl;

  fp = &font8[(c & 0x7F) << 3];
  vp = (unsigned char *) vduinfo.curs;
  nl = FONT_LINES;
  if (vduinfo.attr == 0) {
	do {
		*vp = *fp++;
		vp += BYT_LIN;
	} while (--nl != 0);
	*vp = 0;
  } else {
	do {
		*vp = ~(*fp++);
		vp += BYT_LIN;
	} while (--nl != 0);
	*vp = 0xFF;
  }
}

/*===========================================================================*
 *				vducursor				     *
 *===========================================================================*/
PUBLIC void vducursor(onoff)
int onoff;
{
/* Show or hide the cursor by inverting its cell.  The cursor must be
 * hidden while characters are painted or the screen is moved.
 */
  register char *vp;
  register int nl;

  onoff = (onoff != 0);
  if (!vdu_ready || onoff == cursor_shown)
	return;
  cursor_shown = onoff;
  vp = vduinfo.curs;
  nl = LINC;
  do {
	*vp = ~(*vp);
	vp += BYT_LIN;
  } while (--nl != 0);
}

/*===========================================================================*
 *				vdu_init				     *
 *===========================================================================*/
PUBLIC void vdu_init()
{
/* Put the screen at the top VIDEO_PAGE bytes of RAM, clear it, and fill
 * the scan lines below the last text row with black, as UniPlus does so
 * that nothing shows there.  lisa_user_mem_end() keeps Minix out of the
 * page.
 */
  register struct vduinfo *v = &vduinfo;
  register char *p;
  long screen;

  screen = lisa_memend - VIDEO_PAGE;
  v->vram = (char *) screen;
  REG8(VIDEO_LATCH) = (int) ((screen + lisa_membase) >> 15);
  for (p = v->vram + NROW * BYTR; p < v->vram + VIDEO_PAGE; p++)
	*p = 0xFF;
  v->attr = 0;
  v->next = 0;
  moveto(0, 0);
  clrarea(0, 0, NROW - 1, NCOL - 1);
  vdu_ready = 1;
  vducursor(1);
}
#endif
