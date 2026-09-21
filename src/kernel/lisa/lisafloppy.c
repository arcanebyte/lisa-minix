#include <minix/config.h>
#if (MACHINE == LISA)
/* lisafloppy.c -- the Lisa 2's Sony 400K floppy drive (/dev/fd0).
 *
 * The drive is run by a 6504 microprocessor on the I/O board, which does
 * the GCR encoding, seeking and speed control itself.  The 68000 talks to
 * it through shared RAM at 0xFCC000, one byte at every odd address: it
 * puts the drive, side, track, sector and a function (read, write) there,
 * waits until the controller is ready, writes a "go" command, and the
 * controller interrupts on level 1, flagged by FDIR (bit 0x10 of the COPS
 * VIA's port B), when it has finished.  Data and the 12 tag bytes of the
 * sector are in the shared RAM.
 *
 * Sources: the facts below are from UniPlus include/sys/sony.h and
 * sys/sony.c, sys/l1.c (read, not copied) and LisaEm lisa/io_board/floppy.c;
 * see docs/lisa-kernel.md.  To be checked against the Lisa Hardware Manual
 * and a real Lisa.
 *
 * The task takes the same messages as the other disk tasks (lisapro.c).
 * Minor 0 is the whole disk: 800 blocks of 512 bytes, 400 Minix blocks.
 * A 400K disk has 80 tracks in five speed zones of 16 tracks with 12, 11,
 * 10, 9 and 8 sectors; block numbers run across the zones in order.
 */

#include "../kernel.h"
#include <minix/callnr.h>
#include <minix/com.h>
#include "../proc.h"
#include "lisaaddr.h"

#define REG8(a)		(*(volatile unsigned char *)(a))
#define FD(off)		REG8(0xFCC000L + (off))

/* Shared RAM, odd addresses. [U include/sys/sony.h; E floppy.c] */
#define F_GO		0x001	/* command for the controller */
#define F_FUNC		0x003	/* function, or interrupt status bits */
#define F_DRIVE		0x005	/* 0x80: the Lisa 2's drive */
#define F_SIDE		0x007
#define F_SECTOR	0x009
#define F_TRACK		0x00B
#define F_STATUS	0x011	/* result of the last function */
#define F_TYPE		0x015	/* 1: 400K drive, 2: 800K */
#define F_DISKIN	0x041	/* 0xFF: disk in the drive */
#define F_CONNECTED	0x049	/* 0xFF: drive connected */
#define F_TAGS		0x3E9	/* 12 tag bytes, every other address */
#define F_DATA		0x401	/* 512 data bytes, every other address */

/* Commands (F_GO) and functions (F_FUNC). */
#define GO_RW		0x81	/* perform the function */
#define GO_CLEAR	0x85	/* clear the interrupt status bits in F_FUNC */
#define GO_MASK		0x86	/* enable the interrupts in F_FUNC */
#define FN_READ		0
#define FN_WRITE	1
#define DRIVE2		0x80	/* the Lisa 2 drive, and its interrupt bit */
#define CLEAR_ALL	0x77	/* interrupt status bits UniPlus clears */

/* Ready handshake: the parallel port VIA's port B shows DSKDIAG (0x40) when
 * the controller can take a command. [U include/sys/pport.h, sys/sony.c]
 */
#define DSKDIAG		0x40
#define FDIR		0x10	/* COPS VIA port B: floppy interrupt request */

#define SECTOR_SIZE	512
#define NBLOCKS		800
#define TAG_BYTES	12
#define READY_WAIT	100000L	/* polls for the controller to be ready */
#define TIMEOUT_TICKS	(10 * HZ)
#define MAX_ERRORS	3

PRIVATE message mess;
PRIVATE int present;		/* a 400K drive is connected */
PRIVATE unsigned char sector_buf[SECTOR_SIZE];

/* Shared with the interrupt handler. */
PRIVATE volatile int fd_busy;	/* a function is running */
PRIVATE volatile int fd_op;	/* FN_READ or FN_WRITE */
PRIVATE volatile int fd_status;	/* its result, -1 for a timeout */

FORWARD int fd_ready();
FORWARD int fd_sector();
FORWARD int do_rdwt();
FORWARD void fd_timeout();
FORWARD void set_timer();

/*===========================================================================*
 *				floppy_task				     *
 *===========================================================================*/
PUBLIC void floppy_task()
{
  register int r, caller, procno;
  int s;

  /* Set up the controller as UniPlus snopen() does: select the drive,
   * clear its interrupt status and enable its interrupts.
   */
  if (FD(F_TYPE) == 1 && fd_ready() == OK) {
	FD(F_DRIVE) = DRIVE2;
	FD(F_SIDE) = 0;
	if (FD(F_CONNECTED) == 0xFF) {
		s = lock();
		FD(F_FUNC) = 0xFF;
		FD(F_GO) = GO_CLEAR;
		restore(s);
		if (fd_ready() == OK) {
			s = lock();
			FD(F_FUNC) = DRIVE2;
			FD(F_GO) = GO_MASK;
			restore(s);
			present = 1;
		}
	}
  }
  if (!present) printf("Floppy: no 400K drive\n");

  while (TRUE) {
	receive(ANY, &mess);
	if (mess.m_source == HARDWARE) continue;	/* stray completion */
	if (mess.m_source < 0)
		panic("floppy task got message from ", mess.m_source);
	caller = mess.m_source;
	procno = mess.PROC_NR;

	switch (mess.m_type) {
	    case DISK_READ:
	    case DISK_WRITE:	r = do_rdwt(&mess);	break;
	    case SCATTERED_IO:	r = do_vrdwt(&mess, do_rdwt); break;
	    default:		r = EINVAL;		break;
	}

	mess.m_type = TASK_REPLY;
	mess.REP_PROC_NR = procno;
	mess.REP_STATUS = r;
	send(caller, &mess);
  }
}

/*===========================================================================*
 *				do_rdwt					     *
 *===========================================================================*/
PRIVATE int do_rdwt(mp)
register message *mp;
{
  register struct proc *rp;
  register int r, n, count, errors, i;
  long block;
  phys_bytes address;
  register unsigned char *p, *q;

  if (!present || mp->DEVICE != 0) return(EIO);
  if ((mp->POSITION % SECTOR_SIZE) != 0 || (mp->COUNT % SECTOR_SIZE) != 0)
	return(EINVAL);
  block = mp->POSITION / SECTOR_SIZE;
  count = mp->COUNT / SECTOR_SIZE;
  rp = proc_addr(mp->PROC_NR);
  address = umap(rp, D, (vir_bytes) mp->ADDRESS, (vir_bytes) mp->COUNT);
  if (address == 0) return(EINVAL);
  if (block >= NBLOCKS) return(0);
  if (block + count > NBLOCKS) count = (int) (NBLOCKS - block);

  r = OK;
  for (n = 0; n < count && r == OK; n++) {
	p = (unsigned char *) address + (long) n * SECTOR_SIZE;
	if (mp->m_type == DISK_WRITE)
		for (q = sector_buf, i = 0; i < SECTOR_SIZE; i++) *q++ = *p++;
	for (errors = 0; errors < MAX_ERRORS; errors++) {
		r = fd_sector(mp->m_type == DISK_WRITE ? FN_WRITE : FN_READ,
			      (int) (block + n));
		if (r == OK || r == ENXIO) break;
	}
	if (r == OK && mp->m_type == DISK_READ)
		for (q = sector_buf, i = 0; i < SECTOR_SIZE; i++) *p++ = *q++;
  }
  if (r == ENXIO) {
	printf("Floppy: no disk in the drive\n");
	return(EIO);
  }
  if (r != OK) {
	printf("Floppy: %s error at block %D, status %x\n",
		mp->m_type == DISK_WRITE ? "write" : "read",
		block + n - 1, fd_status & 0xFF);
	return(n > 1 ? (n - 1) * SECTOR_SIZE : EIO);
  }
  return(count * SECTOR_SIZE);
}

/*===========================================================================*
 *				fd_sector				     *
 *===========================================================================*/
PRIVATE int fd_sector(op, block)
int op;				/* FN_READ or FN_WRITE */
int block;			/* 0-799 */
{
/* Read or write one sector through sector_buf. */
  int track, sector, spt, i, s;
  register unsigned char *q;
  message m;			/* not mess: that may hold the request */

  if (FD(F_DISKIN) != 0xFF) return(ENXIO);

  /* Block number to track and sector: zones of 16 tracks, 12 down to 8
   * sectors per track [U sys/sony.c; E floppy.c].
   */
  track = 0;
  for (spt = 12; block >= 16 * spt; spt--) {
	block -= 16 * spt;
	track += 16;
  }
  track += block / spt;
  sector = block % spt;

  if (fd_ready() != OK) return(EIO);
  FD(F_SIDE) = 0;
  FD(F_DRIVE) = DRIVE2;
  FD(F_TRACK) = track;
  FD(F_SECTOR) = sector;
  FD(F_FUNC) = op;
  if (op == FN_WRITE) {
	for (q = sector_buf, i = 0; i < SECTOR_SIZE; i++)
		FD(F_DATA + 2 * i) = *q++;
	for (i = 0; i < TAG_BYTES; i++)
		FD(F_TAGS + 2 * i) = 0;
  }
  if (fd_ready() != OK) return(EIO);

  set_timer(TIMEOUT_TICKS);
  s = lock();
  fd_op = op;
  fd_status = -1;
  fd_busy = 1;
  FD(F_GO) = GO_RW;
  restore(s);
  receive(HARDWARE, &m);	/* from lisa_fd_int, or fd_timeout */
  set_timer(0);
  if (fd_busy) {		/* timed out */
	fd_busy = 0;
	printf("Floppy: no answer from the controller\n");
	return(EIO);
  }
  return(fd_status == 0 ? OK : EIO);
}

/*===========================================================================*
 *				lisa_fd_int				     *
 *===========================================================================*/
PUBLIC int lisa_fd_int()
{
/* Level 1: if the floppy controller is interrupting (FDIR), collect the
 * result, acknowledge it and wake the task, and return 1; otherwise return
 * 0 and let the caller treat the interrupt as vertical retrace.  Disk
 * insert and eject button interrupts are acknowledged and ignored.
 */
  register int i;
  register unsigned char *q;

  if ((REG8(VIA_COPS_REG(VIA_ORB)) & FDIR) == 0) return(0);

  if (fd_busy) {
	fd_status = FD(F_STATUS);
	if (fd_status == 0 && fd_op == FN_READ)
		for (q = sector_buf, i = 0; i < SECTOR_SIZE; i++)
			*q++ = FD(F_DATA + 2 * i);
  }
  (void) fd_ready();
  FD(F_FUNC) = CLEAR_ALL;
  FD(F_GO) = GO_CLEAR;
  if (fd_busy) {
	fd_busy = 0;
	interrupt(FLOPPY);
  }
  return(1);
}

/*===========================================================================*
 *				fd_ready				     *
 *===========================================================================*/
PRIVATE int fd_ready()
{
/* Wait until the controller can take a command: DSKDIAG set and the last
 * go byte taken [U sys/sony.c].
 */
  register long n;

  for (n = READY_WAIT; n > 0; n--)
	if ((REG8(VIA_PAR_REG(VIA_ORB)) & DSKDIAG) != 0 && FD(F_GO) == 0)
		return(OK);
  return(EIO);
}

/*===========================================================================*
 *				timeout					     *
 *===========================================================================*/
PRIVATE void set_timer(ticks)
int ticks;			/* 0 cancels */
{
  message m;

  m.m_type = SET_ALARM;
  m.CLOCK_PROC_NR = FLOPPY;
  m.DELTA_TICKS = (long) ticks;
  m.FUNC_TO_CALL = fd_timeout;
  sendrec(CLOCK, &m);
}

PRIVATE void fd_timeout()
{
/* Called by the clock task if the controller never answered. */
  if (fd_busy) interrupt(FLOPPY);
}

PUBLIC void fd_timer()
{
}
#endif
