#include <minix/config.h>
#if (MACHINE == LISA)
/* lisapro.c -- ProFile hard disk task for Minix on the Lisa (/dev/hd*).
 *
 * The ProFile on the Lisa's built-in parallel port, driven by polling (no
 * interrupts).  It replaces winchester_task (stwini.c on the ST) and takes
 * the same messages:
 *
 *    m_type      DEVICE    PROC_NR     COUNT    POSITION  ADDRESS
 * ----------------------------------------------------------------
 * |  DISK_READ | device  | proc nr |  bytes  |  offset | buf ptr |
 * |------------+---------+---------+---------+---------+---------|
 * | DISK_WRITE | device  | proc nr |  bytes  |  offset | buf ptr |
 * |------------+---------+---------+---------+---------+---------|
 * |SCATTERED_IO| device  | proc nr | requests|         | iov ptr |
 * ----------------------------------------------------------------
 *
 * Minor devices:
 *   0 (/dev/hd0)  the Minix file system area, from the image header in
 *                 block 1 written by tools/mklisa.py
 *   1 (/dev/hd1)  the whole ProFile, all blocks
 *
 * Protocol (Apple "ProFile HD Communications Protocol", rev 11-14-83; the
 * register sequence was checked against UniPlus stand/pro.c, read only):
 * each phase is a handshake in which the host lowers /CMD, the drive
 * answers with a state byte and lowers /BSY, the host replies $55 and raises
 * /CMD, and the drive raises /BSY when it is done.  A command is six bytes:
 * command (0 read, 1 write), block number (3 bytes), retry count, spare
 * threshold.  A read returns 4 status bytes, 20 tag bytes and 512 data
 * bytes; a write takes 20 tag bytes and 512 data bytes.  All data moves
 * through VIA register 1, whose access pulses CA2 (/PSTRB) with PCR $6B.
 */

#include "../kernel.h"
#include <minix/callnr.h>
#include <minix/com.h>
#include "../proc.h"
#include "lisaaddr.h"

#define REG8(a)		(*(volatile unsigned char *)(a))
#define VIA(n)		REG8(VIA_PAR_REG(n))

/* 6522 register numbers */
#define V_ORB		0
#define V_ORA		1
#define V_DDRB		2
#define V_DDRA		3
#define V_ACR		11
#define V_PCR		12
#define V_IER		14

/* Port B bits (UniPlus include/sys/pport.h, Lisa Hardware Manual p. 45) */
#define PB_OCD		0x01	/* open cable detect */
#define PB_BSY		0x02	/* /BSY from the drive: 1 = not busy */
#define PB_DEN		0x04	/* /disk enable, 0 = interface buffers on */
#define PB_DRW		0x08	/* direction: 1 = read from the drive */
#define PB_CMD		0x10	/* /CMD: 0 = command */

/* Drive states returned in the handshake */
#define S_CMD		1	/* ready for a command */
#define S_READ		2	/* block read, ready to send it */
#define S_WRITE		3	/* ready to receive data to write */
#define S_PERFORM	6	/* data received, ready to write it */

#define P_GO		0x55	/* host reply: proceed */
#define P_IDLE		0x00	/* host reply: back to idle */

#define PRO_READ	0
#define PRO_WRITE	1

#define STATUS_BYTES	4
#define TAG_BYTES	20
#define SECTOR_SIZE	512
#define SPARE_TABLE	0xFFFFFFL	/* block number of the spare table */

/* Status byte 1 errors: operation failed, timeout, CRC, seek, >532 bytes,
 * no $55.  Status byte 3: controller reset.
 */
#define ST1_ERRORS	0xDD
#define ST3_RESET	0x80

#define MAX_ERRORS	5
#define BSY_WAIT	200000L	/* polls for /BSY: generous, see pro_wait */

#define NR_MINORS	2
#define MINOR_FS	0
#define MINOR_WHOLE	1

/* Image header in block 1 (tools/mklisa.py) */
#define HDR_MAGIC	0x4D4E584CL	/* "MNXL" */
#define H_FS_FIRST	24
#define H_FS_COUNT	28

PRIVATE struct {
  long start;			/* first ProFile block */
  long size;			/* size in ProFile blocks */
} part[NR_MINORS];

PRIVATE message mess;
PRIVATE unsigned char status[STATUS_BYTES];
PRIVATE unsigned char tagbuf[TAG_BYTES];
PRIVATE unsigned char blockbuf[TAG_BYTES + SECTOR_SIZE];

FORWARD int do_rdwt();
FORWARD int pro_block();
FORWARD int pro_handshake();
FORWARD void pro_idle();
FORWARD long get_long();

/*===========================================================================*
 *				winchester_task				     *
 *===========================================================================*/
PUBLIC void winchester_task()
{
  register int r, caller, procno;
  unsigned char *b;

  pro_idle();

  /* Whole disk: size from the spare table (bytes 18-20 of the block). */
  if (pro_block(PRO_READ, SPARE_TABLE, blockbuf) == OK) {
	b = blockbuf;
	part[MINOR_WHOLE].size = ((long) b[18] << 16) | ((long) b[19] << 8) | b[20];
  }
  /* File system area: from the image header in block 1. */
  if (part[MINOR_WHOLE].size != 0 && pro_block(PRO_READ, 1L, blockbuf) == OK) {
	b = blockbuf + TAG_BYTES;
	if (get_long(b) == HDR_MAGIC) {
		part[MINOR_FS].start = get_long(b + H_FS_FIRST);
		part[MINOR_FS].size = get_long(b + H_FS_COUNT);
	}
  }
  printf("ProFile: %D blocks; file system at block %D, %D blocks\n",
	part[MINOR_WHOLE].size, part[MINOR_FS].start, part[MINOR_FS].size);

  while (TRUE) {
	receive(ANY, &mess);
	if (mess.m_source < 0)
		panic("disk task got message from ", mess.m_source);
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
	mess.REP_STATUS = r;	/* # of bytes transferred or error code */
	send(caller, &mess);
  }
}

/*===========================================================================*
 *				do_rdwt					     *
 *===========================================================================*/
PRIVATE int do_rdwt(mp)
register message *mp;
{
/* Read or write whole 512-byte blocks (from stwini.c). */
  register struct proc *rp;
  register int r, errors, count, n, minor;
  long block, avail;
  phys_bytes address;
  register unsigned char *p, *q;
  int i;

  minor = mp->DEVICE;
  if (minor < 0 || minor >= NR_MINORS || part[minor].size == 0)
	return(EIO);
  if ((mp->POSITION % SECTOR_SIZE) != 0 || (mp->COUNT % SECTOR_SIZE) != 0)
	return(EINVAL);
  block = mp->POSITION / SECTOR_SIZE;
  count = mp->COUNT / SECTOR_SIZE;
  rp = proc_addr(mp->PROC_NR);
  address = umap(rp, D, (vir_bytes) mp->ADDRESS, (vir_bytes) mp->COUNT);
  if (address == 0)
	return(EINVAL);
  avail = part[minor].size - block;
  if (avail <= 0)
	return(0);
  if (avail < count)
	count = (int) avail;
  block += part[minor].start;

  rp->p_physio = 1;		/* disable (un)shadowing */
  r = OK;
  for (n = 0; n < count && r == OK; n++) {
	p = (unsigned char *) address + (long) n * SECTOR_SIZE;
	if (mp->m_type == DISK_WRITE) {
		q = blockbuf;
		for (i = 0; i < TAG_BYTES; i++) *q++ = 0;
		for (i = 0; i < SECTOR_SIZE; i++) *q++ = *p++;
	}
	for (errors = 0; errors < MAX_ERRORS; errors++) {
		r = pro_block(mp->m_type == DISK_WRITE ? PRO_WRITE : PRO_READ,
							block + n, blockbuf);
		if (r == OK) break;
	}
	if (r == OK && mp->m_type == DISK_READ) {
		q = blockbuf + TAG_BYTES;
		for (i = 0; i < SECTOR_SIZE; i++) *p++ = *q++;
	}
  }
  rp->p_physio = 0;		/* enable (un)shadowing */
  if (r != OK) {
	printf("ProFile: %s error at block %D, status %x %x %x %x\n",
		mp->m_type == DISK_WRITE ? "write" : "read", block + n - 1,
		status[0], status[1], status[2], status[3]);
	return(n > 1 ? (n - 1) * SECTOR_SIZE : EIO);
  }
  return(count * SECTOR_SIZE);
}

/*===========================================================================*
 *				pro_block				     *
 *===========================================================================*/
PRIVATE int pro_block(cmd, block, buf)
int cmd;			/* PRO_READ or PRO_WRITE */
long block;			/* ProFile block number */
unsigned char *buf;		/* 20 tag bytes, then 512 data bytes */
{
/* Transfer one block.  Returns OK, or EIO with status[] filled in if the
 * drive reported one.
 */
  register int i;
  register unsigned char *p;

  for (i = 0; i < STATUS_BYTES; i++) status[i] = 0;
  if (pro_handshake(S_CMD) != OK) return(EIO);

  /* Send the command block; the handshake left the port set for output. */
  VIA(V_ORA) = cmd;
  VIA(V_ORA) = (int) (block >> 16) & 0xFF;
  VIA(V_ORA) = (int) (block >> 8) & 0xFF;
  VIA(V_ORA) = (int) block & 0xFF;
  VIA(V_ORA) = 10;		/* retry count */
  VIA(V_ORA) = 3;		/* spare threshold */

  if (cmd == PRO_READ) {
	if (pro_handshake(S_READ) != OK) return(EIO);
  } else {
	if (pro_handshake(S_WRITE) != OK) return(EIO);
	VIA(V_ORB) &= ~PB_DRW;		/* host to drive */
	VIA(V_DDRA) = 0xFF;
	p = buf;
	for (i = 0; i < TAG_BYTES + SECTOR_SIZE; i++)
		VIA(V_ORA) = *p++;
	if (pro_handshake(S_PERFORM) != OK) return(EIO);
  }

  /* Status, then for a read the tag and data. */
  VIA(V_ORB) |= PB_DRW;			/* drive to host */
  VIA(V_DDRA) = 0;
  for (i = 0; i < STATUS_BYTES; i++)
	status[i] = VIA(V_ORA);
  if (cmd == PRO_READ) {
	p = buf;
	for (i = 0; i < TAG_BYTES + SECTOR_SIZE; i++)
		*p++ = VIA(V_ORA);
  }
  pro_idle();
  if ((status[0] & ST1_ERRORS) || (status[2] & ST3_RESET)) return(EIO);
  return(OK);
}

/*===========================================================================*
 *				pro_handshake				     *
 *===========================================================================*/
PRIVATE int pro_handshake(state)
int state;			/* drive state expected */
{
/* Lower /CMD, wait for the drive's state byte, answer $55 if it is 'state'
 * (otherwise $00), raise /CMD and wait until the drive is done.  Leaves the
 * data port set for output.
 */
  register int resp;
  volatile long n;

  for (n = BSY_WAIT; (VIA(V_ORB) & PB_BSY) == 0; n--)
	if (n == 0) goto timeout;

  VIA(V_ORB) |= PB_DRW;			/* drive to host */
  VIA(V_DDRA) = 0;
  VIA(V_ORB) &= ~PB_CMD;		/* /CMD low */

  for (n = BSY_WAIT; VIA(V_ORB) & PB_BSY; n--)
	if (n == 0) goto timeout;

  resp = VIA(V_ORA) & 0xFF;		/* drive's state byte */
  VIA(V_ORB) &= ~PB_DRW;		/* host to drive */
  VIA(V_DDRA) = 0xFF;
  VIA(V_ORA) = (resp == state) ? P_GO : P_IDLE;
  VIA(V_ORB) |= PB_CMD;			/* /CMD high: drive samples reply */

  for (n = BSY_WAIT; (VIA(V_ORB) & PB_BSY) == 0; n--)
	if (n == 0) goto timeout;
  if (resp != state) {
	printf("ProFile: state %d, expected %d\n", resp, state);
	pro_idle();
	return(EIO);
  }
  return(OK);

timeout:
  printf("ProFile: no response (waiting for state %d)\n", state);
  pro_idle();
  return(EIO);
}

/*===========================================================================*
 *				pro_idle				     *
 *===========================================================================*/
PRIVATE void pro_idle()
{
/* Set up the VIA and put the interface in its idle state: data port input,
 * interface buffers enabled, direction drive to host, /CMD high, no VIA
 * interrupts (the driver polls).
 */
  VIA(V_IER) = 0x7F;
  VIA(V_ACR) = 0;
  VIA(V_PCR) = 0x6B;		/* CA2 pulses on each register 1 access */
  VIA(V_DDRA) = 0;
  VIA(V_DDRB) = PB_DEN | PB_DRW | PB_CMD;
  VIA(V_ORB) = (VIA(V_ORB) & ~PB_DEN) | PB_DRW | PB_CMD;
}

PRIVATE long get_long(p)
unsigned char *p;
{
  return(((long) p[0] << 24) | ((long) p[1] << 16) | ((long) p[2] << 8) | p[3]);
}
#endif
