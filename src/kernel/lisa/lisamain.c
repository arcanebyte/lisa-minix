#include <minix/config.h>
#if (MACHINE == LISA)
/* This file contains the main program of MINIX for the Apple Lisa.
 * Derived from stmain.c (Minix-ST).  The routine main() initializes the
 * system and starts the ball rolling by setting up the proc table and
 * scheduling each task to run to initialize itself.
 *
 * The entries into this file are:
 *   main:		MINIX main program
 *   none:		called for an interrupt to an unused vector
 *   rupt:		called for an unexpected interrupt (async)
 *   trap:		called for an unexpected trap (synchronous)
 *   checksp:		check a process' stack pointer after a trap
 *   panic:		abort MINIX due to a fatal error
 *   lisa_level1:	level 1 interrupt: vertical retrace (the clock)
 *   lisa_level2:	level 2 interrupt: COPS
 *   lisa_level6:	level 6 interrupt: SCC
 *   lisa_user_mem_end:	logical end of the memory Minix may use
 *   idle_task and the stub tasks for devices not supported yet
 *   (the ProFile task is in lisapro.c, the floppy task in lisafloppy.c)
 */

#include "../kernel.h"
#include <signal.h>
#include <minix/callnr.h>
#include <minix/com.h>
#include "../proc.h"
#include "lisaaddr.h"

#define REG8(a)		(*(volatile unsigned char *)(a))

extern long lisa_bootdev, lisa_membase, lisa_memend;	/* lisampx.s */

FORWARD void initlisa();

/*===========================================================================*
 *                                   main                                    *
 *===========================================================================*/
PUBLIC void main()
{
/* Start the ball rolling. */

  register struct proc *rp;
  register int t;
  register vir_clicks size;
  register phys_clicks base;
  reg_t ktsb;

  initlisa();

  /* Clear the process table.
   * Set up mappings for proc_addr() and proc_number() macros.
   */
  for (rp = BEG_PROC_ADDR, t = -NR_TASKS; rp < END_PROC_ADDR; ++rp, ++t) {
        rp->p_flags = P_SLOT_FREE;
        rp->p_nr = t;           /* proc number from ptr */
        (pproc_addr + NR_TASKS)[t] = rp;        /* proc ptr from number */
  }

  size = sizes[0] + sizes[1];	/* kernel text + data size */
  base = size;			/* end of kernel */

  ktsb = ((reg_t) t_stack + (ALIGNMENT - 1)) & ~((reg_t) ALIGNMENT - 1);
  for (t = -NR_TASKS; t < 0; t++) {	/* for all drivers */
	rp = proc_addr(t);
	rp->p_flags = 0;
	ktsb += tasktab[t+NR_TASKS].stksize;
	rp->p_reg.sp = ktsb;
	rp->p_splow = rp->p_reg.sp;
	rp->p_reg.pc = (reg_t) tasktab[t + NR_TASKS].initial_pc;
	if (!isidlehardware(t)) {
		lock_ready(rp);	/* IDLE, HARDWARE neveready */
		rp->p_reg.psw = 0x2000;	/* S-bit, all interrupts on */
	} else {
		rp->p_reg.psw = 0x0000;
	}
	rp->p_map[T].mem_len  = sizes[0];
	rp->p_map[D].mem_len  = sizes[1];
	rp->p_map[D].mem_phys = sizes[0];
	rp->p_map[S].mem_phys = size;
	rp->p_map[D].mem_vir  = rp->p_map[D].mem_phys;
	rp->p_map[S].mem_vir  = rp->p_map[S].mem_phys;
  }

  rp = proc_addr(HARDWARE);
  rp->p_map[D].mem_len  = ~0;	/* maximum size */
  rp->p_map[D].mem_phys = 0;
  rp->p_map[D].mem_vir  = 0;

  for (t = 0; t <= LOW_USER; t++) {
	rp = proc_addr(t);
	rp->p_flags = 0;
	lock_ready(rp);
	rp->p_reg.psw = (reg_t)0x0000;	/* user mode, all interrupts on */
	rp->p_reg.pc = (reg_t) ((long)base << CLICK_SHIFT);
	size = sizes[2*t + 2];
	rp->p_map[T].mem_len  = size;
	rp->p_map[T].mem_phys = base;
	base += size;
	size = sizes[2*t + 3];
	rp->p_map[D].mem_len  = size;
	rp->p_map[D].mem_phys = base;
	base += size;
	rp->p_map[S].mem_len  = 0;
	rp->p_map[S].mem_phys = base;
	rp->p_map[T].mem_vir  = rp->p_map[T].mem_phys;
	rp->p_map[D].mem_vir  = rp->p_map[D].mem_phys;
	rp->p_map[S].mem_vir  = rp->p_map[S].mem_phys;
  }

  bill_ptr = proc_addr(HARDWARE);	/* it has to point somewhere */
  lock_pick_proc();

  /* go back to assembly code to start running the current process. */
}


/*===========================================================================*
 *                              none, rupt, trap                             *
 *===========================================================================*/
PUBLIC void none()
{
  panic("Nonexisting interrupt. Vector =", proc_ptr->p_trap);
}

PUBLIC void rupt()
{
  panic("Unexpected interrupt.  Vector =", proc_ptr->p_trap);
}


PUBLIC void trap()
{
  register t;
  register struct proc *rp;
  static char vecsig[] = {
	0, 0, SIGSEGV, SIGBUS, SIGILL, SIGILL, SIGILL, SIGABRT,
	SIGILL, SIGTRAP, SIGEMT, SIGFPE, SIGSTKFLT
  };

  rp = proc_ptr;
  t = rp->p_trap;
  if (rp->p_reg.psw & 0x2000) panic("trap via vector", t);
  if (t >= 0 && t < sizeof(vecsig)/sizeof(vecsig[0]) && vecsig[t]) {
	t = vecsig[t];
  } else {
	printf("\nUnexpected trap.  Vector = %d\n", t);
	printf("This may be due to accidentally including\n");
	printf("a non-MINIX library routine that is trying to make a system call.\n");
	t = SIGILL;
  }
  if (t != SIGSTKFLT) {	/* DEBUG */
	printf("sig=%d to pid=%d at pc=%X\n",
		t, rp->p_pid, rp->p_reg.pc);
	dump();
  }
  cause_sig(proc_number(rp), t);
}

PUBLIC void checksp()
{
  register struct proc *rp;
  register phys_bytes ad;

  rp = proc_ptr;
  /* if a user process is is supervisor mode don't check stack */
  if ((rp->p_nr >= 0) && (rp->p_reg.psw & 0x2000)) return;
  if (rp->p_reg.sp < rp->p_splow)
	rp->p_splow = rp->p_reg.sp;
  if (rp->p_map[S].mem_len == 0)
	return;
  ad = (phys_bytes)rp->p_map[S].mem_vir;	/* sp is a virtual address */
  ad <<= CLICK_SHIFT;
  if ((phys_bytes)rp->p_reg.sp > ad)
	return;
  /*
   * Stack violation.
   */
  ad = (phys_bytes)rp->p_map[D].mem_vir;
  ad += (phys_bytes)rp->p_map[D].mem_len;
  ad <<= CLICK_SHIFT;
  if ((phys_bytes)rp->p_reg.sp < ad + CLICK_SIZE)
	printf("Stack low (pid=%d,pc=%X,sp=%X,end=%X)\n",
		rp->p_pid, (long)rp->p_reg.pc,
		(long)rp->p_reg.sp, (long)ad);
  rp->p_trap = 12;	/* fake trap causing SIGSTKFLT */
  trap();
}

/*===========================================================================*
 *                                   panic                                   *
 *===========================================================================*/
PUBLIC void panic(s,n)
char *s;
int n;
{
/* The system has run aground of a fatal error.  Terminate execution.
 * If the panic originated in MM or FS, the string will be empty and the
 * file system already syncked.  If the panic originates in the kernel, we are
 * kind of stuck.
 */

  if (*s != 0) {
	printf("\nKernel panic: %s",s);
	if (n != NO_NUM) printf(" %d", n);
	printf("\n");
  }
  dump();
  printf("\nPush RESET button\n");
  for (;;)
	;
}

/*===========================================================================*
 *                                   initlisa                                *
 *===========================================================================*/
PRIVATE void initlisa()
{
/* Lisa specific initialization: mask the VIAs, which share interrupt
 * levels 1 and 2 and may have been left enabled by the boot ROM, set up
 * the console (screen and serial port, for kernel printf) and the COPS
 * keyboard interrupt, make MMU context 1 the (still empty) user context,
 * and turn on the vertical retrace interrupt, which drives the clock.
 */
  REG8(VIA_PAR_REG(VIA_IER)) = 0x7F;
  REG8(VIA_COPS_REG(VIA_IER)) = 0x7F;
  lisa_cons_init();
  vdu_init();
  lisa_kbd_init();
  lisa_mmu_init();
  REG8(VRT_ON) = 0;
}

/*===========================================================================*
 *                                   lisa_user_mem_end                       *
 *===========================================================================*/
PUBLIC long lisa_user_mem_end()
{
/* Minix uses logical memory from 0 to the end of RAM reported by the boot
 * ROM, less the video page at the top (lisavdu.c).
 */
  return(lisa_memend - VIDEO_PAGE);
}

/*===========================================================================*
 *                                   interrupts                              *
 *===========================================================================*/
PUBLIC void lisa_level1(t)
int t;
{
/* Vertical retrace, about 60 Hz, is the clock tick (HZ is 60).  Acknowledge
 * it by turning the interrupt off and on (UniPlus sys/l1.c).  The parallel
 * port VIA and the floppy controller also use level 1; they are masked.
 *
 * All interrupts are locked out around the two writes for LisaEm's sake:
 * its handling of the VRT_ON write (reset_video_timing, get_next_timer_event
 * in lisa/cpu_board/irq.c) takes a pending VIA interrupt, such as the COPS
 * at level 2, in the middle of the write instruction, and the instruction
 * then adds its length to the new PC, so the level 2 handler started 8
 * bytes in and the stack was wrecked.  With the mask at 7 LisaEm leaves
 * the interrupt pending until restore().  A real 68000 does not need this.
 */
  int s;

  if (lisa_fd_int()) return;	/* the floppy controller (lisafloppy.c) */
  s = lock();
  REG8(VRT_OFF) = 0;
  REG8(VRT_ON) = 0;
  restore(s);
  clock_handler();
}

PUBLIC void lisa_level2(t)
int t;
{
/* The COPS has a byte for us (keyboard, mouse or clock): lisakbd.c. */
  lisa_cops_int();
}

PUBLIC void lisa_level6(t)
int t;
{
  lisa_scc_int();
}

/*===========================================================================*
 *                                   stubs                                   *
 *===========================================================================*/

PUBLIC void fake_task(s)
char *s;
{
  message m;

  for (;;) {
	receive(ANY, &m);
	printf("%s received %d from %d\n", s, m.m_type, m.m_source);
  }
}

PUBLIC void printer_task()	{ fake_task("printer_task"); }
PUBLIC void pr_restart()	{ }

PUBLIC void idle_task()
{
  while (1);
}
#endif
