#include <minix/config.h>
#if (MACHINE == LISA)
/* lisammu.c -- the Lisa MMU: one user context, reloaded on context switch.
 *
 * Minix-ST had no memory management hardware, so every process ran at its
 * physical address and fork used "shadowing" (copying data and stack back
 * and forth).  On the Lisa this file replaces that (PLAN.md phase 5):
 *
 * - The kernel, the tasks and every exception handler run in supervisor
 *   mode, which the Lisa always maps through MMU context 0.  Context 0 is
 *   left as the boot ROM set it: logical 0 to the end of RAM is all of
 *   physical RAM, plus the I/O space.  Minix "physical" addresses (clicks
 *   in the memory maps, umap() results) are context 0 logical addresses.
 *
 * - Everything in user mode runs in contexts 1-3, which the SEG1 and SEG2
 *   latches select: MM in context 2, FS in context 3, and everything else
 *   (INIT, user processes, the IDLE task) in context 1.  MM and FS have
 *   contexts of their own because nearly every system call goes to one of
 *   them and back; changing context is two latch writes, reloading one a
 *   register write for every segment involved.  Before a user-mode process
 *   runs, restart() in lisampx.s calls lisa_mmu_switch(), which selects its
 *   context and loads the process's memory map into it if that context
 *   does not already hold it: each 128 KB MMU segment that
 *   holds part of the process's image (text, data, gap and stack) gets an
 *   origin and a limit, and every other segment is invalid, so a stray
 *   access traps with a bus error.
 *
 * - A process's virtual address v is at context 0 address v + delta, with
 *   one delta for the whole image (the memory map must be contiguous with
 *   one offset; lisa_mmu_switch panics otherwise).  MM (src/mm/exec.c)
 *   gives programs it execs virtual address 0, so every process runs at
 *   the same addresses and fork is a plain copy (system.c do_fork).  MM,
 *   FS, INIT and IDLE keep delta 0.  The MMU maps 512-byte pages, so delta
 *   must be a multiple of 512: MM allocates memory at even clicks.
 *
 * - An image that starts at a segment boundary (programs at virtual 0) is
 *   protected on both sides.  One that starts inside a segment (MM, FS,
 *   INIT and children INIT forks before they exec) can reach the memory
 *   below it in that segment, since a segment limit bounds only one end.
 *   Those are system processes.  Text is not write-protected: the Minix
 *   memory map puts text and data in the same segment.
 *
 * Register encoding (UniPlus include/sys/mmu.h, sys/cxureg.c; LisaEm
 * lisa/cpu_board/mmu.c):
 *   segment s: limit register at s * 0x20000 + 0x8000, origin register
 *   at s * 0x20000 + 0x8008, both 12 bits, reachable only in setup mode.
 *   Origin: physical address of the segment's page 0, in 512-byte pages.
 *   Limit: access type in bits 8-11 (0x7 read-write, 0xC invalid) and, for
 *   a read-write segment of n pages, 256 - n in bits 0-7.
 *   Register writes go to the context the SEG1/SEG2 latches select, even
 *   in supervisor mode; in setup mode, RAM access (code and data at
 *   addresses with bit 14 set) still goes through context 0.
 *
 * Entry points:
 *   lisa_mmu_init:	invalidate contexts 1-3 and select context 1
 *   lisa_mmu_switch:	select and load the context of a process about to run
 *			in user mode
 */

#include "../kernel.h"
#include "../proc.h"
#include "lisaaddr.h"

#define REG8(a)		(*(volatile unsigned char *)(a))

#define NSEGS		MMU_NSEGS	/* MMU segments per context */
#define SEG_SHIFT	17		/* 128 KB segments */
#define PAGE_SHIFT	9		/* 512-byte pages */
#define PAGES_PER_SEG	256
#define USER_SEGS	126		/* 126 and 127 are the I/O spaces */

extern long lisa_membase;		/* lisampx.s */

/* The table lisa_mmu_write() works through, and the routine itself, are in
 * .setup, which the linker script puts where address bit 14 is set.
 */
struct mmu_write {
  long mw_addr;			/* register address */
  short mw_value;		/* 12-bit value */
};

#define MAX_WRITES	(2 * NSEGS)
extern struct mmu_write lisa_mmu_table[MAX_WRITES];	/* lisampx.s */
extern void lisa_mmu_write();				/* lisampx.s */

#define NCTX		3		/* user contexts 1, 2 and 3 */

PRIVATE struct uctx {
  char seg_valid[NSEGS];	/* segments valid in the context now */
  struct proc *proc;		/* whose map it holds */
  struct mem_map map[NR_SEGS];	/* and that map */
} uctx[NCTX];

PRIVATE int cur_ctx = -1;	/* context the latches select, 0-2 = 1-3 */
PRIVATE int nwrites;

FORWARD void select_ctx();
FORWARD void add_write();
FORWARD void flush_writes();
FORWARD int same_map();

/*===========================================================================*
 *				lisa_mmu_init				     *
 *===========================================================================*/
PUBLIC void lisa_mmu_init()
{
/* Mark every segment of contexts 1-3 invalid and leave context 1 selected.
 * Called from initlisa() with interrupts off.
 */
  int c, s;

  for (c = NCTX - 1; c >= 0; c--) {
	select_ctx(c);
	nwrites = 0;
	for (s = 0; s < NSEGS; s++) {
		add_write(((long) s << SEG_SHIFT) + MMU_SLR, MMU_ACC_INVAL);
		uctx[c].seg_valid[s] = 0;
	}
	flush_writes();
	uctx[c].proc = NIL_PROC;
  }
}

PRIVATE void select_ctx(c)
int c;				/* 0, 1, 2 for contexts 1, 2, 3 */
{
/* Set the SEG latches: SEG1 alone is context 1, SEG2 alone context 2,
 * both context 3 [U include/sys/mmu.h; E include/vars.h CXASEL].
 */
  if (c == cur_ctx) return;
  if (c == 1) REG8(MMU_SEG1_OFF) = 0; else REG8(MMU_SEG1_ON) = 0;
  if (c == 0) REG8(MMU_SEG2_OFF) = 0; else REG8(MMU_SEG2_ON) = 0;
  cur_ctx = c;
}

/*===========================================================================*
 *				lisa_mmu_switch				     *
 *===========================================================================*/
PUBLIC void lisa_mmu_switch(rp)
register struct proc *rp;
{
/* Select rp's context and load it with rp's memory map.  Called from
 * restart() with interrupts off, just before rp runs in user mode.
 */
  register struct mem_map *mp;
  register struct uctx *u;
  char want[NSEGS];
  long lo, hi, delta, d, seg_base, origin, end;
  int s, seg, pages, first, c;

  c = (rp->p_nr == MM_PROC_NR ? 1 : rp->p_nr == FS_PROC_NR ? 2 : 0);
  select_ctx(c);		/* register writes go to this context too */
  u = &uctx[c];
  mp = rp->p_map;
  if (rp == u->proc && same_map(u, mp)) return;

  /* The image runs from the lowest to the highest virtual address of any
   * non-empty segment; all segments must share one offset.
   */
  lo = 0x7FFFFFFFL;
  hi = 0;
  delta = 0;
  first = 1;
  for (seg = T; seg <= S; seg++) {
	if (mp[seg].mem_len == 0) continue;
	d = ((long) mp[seg].mem_phys - (long) mp[seg].mem_vir) << CLICK_SHIFT;
	if (!first && d != delta)
		panic("MMU: segments with different offsets, proc", rp->p_nr);
	first = 0;
	delta = d;
	if (((long) mp[seg].mem_vir << CLICK_SHIFT) < lo)
		lo = (long) mp[seg].mem_vir << CLICK_SHIFT;
	end = ((long) mp[seg].mem_vir + mp[seg].mem_len) << CLICK_SHIFT;
	if (end > hi) hi = end;
  }
  if (delta & ((1L << PAGE_SHIFT) - 1))
	panic("MMU: memory map not on a page boundary, proc", rp->p_nr);
  if (hi > ((long) USER_SEGS << SEG_SHIFT))
	panic("MMU: process image too high, proc", rp->p_nr);

  for (s = 0; s < NSEGS; s++) want[s] = 0;
  nwrites = 0;
  if (hi > lo) {
	for (s = (int) (lo >> SEG_SHIFT); s <= (int) ((hi - 1) >> SEG_SHIFT); s++) {
		seg_base = (long) s << SEG_SHIFT;
		origin = (seg_base + delta + lisa_membase) >> PAGE_SHIFT;
		end = hi - seg_base;
		if (end > (1L << SEG_SHIFT)) end = 1L << SEG_SHIFT;
		pages = (int) ((end + (1L << PAGE_SHIFT) - 1) >> PAGE_SHIFT);
		add_write(seg_base + MMU_SOR, (int) (origin & 0xFFF));
		add_write(seg_base + MMU_SLR,
			  MMU_ACC_RW | ((PAGES_PER_SEG - pages) & 0xFF));
		want[s] = 1;
	}
  }
  for (s = 0; s < NSEGS; s++) {
	if (u->seg_valid[s] && !want[s])
		add_write(((long) s << SEG_SHIFT) + MMU_SLR, MMU_ACC_INVAL);
	u->seg_valid[s] = want[s];
  }
  flush_writes();
  u->proc = rp;
  for (seg = 0; seg < NR_SEGS; seg++) u->map[seg] = mp[seg];
}

PRIVATE int same_map(u, mp)
register struct uctx *u;
register struct mem_map *mp;
{
  register int seg;

  for (seg = 0; seg < NR_SEGS; seg++)
	if (mp[seg].mem_vir != u->map[seg].mem_vir ||
	    mp[seg].mem_phys != u->map[seg].mem_phys ||
	    mp[seg].mem_len != u->map[seg].mem_len)
		return(0);
  return(1);
}

PRIVATE void add_write(addr, value)
long addr;
int value;
{
  if (nwrites == MAX_WRITES) flush_writes();
  lisa_mmu_table[nwrites].mw_addr = addr;
  lisa_mmu_table[nwrites].mw_value = value;
  nwrites++;
}

PRIVATE void flush_writes()
{
  if (nwrites > 0) lisa_mmu_write(nwrites);
  nwrites = 0;
}
#endif
