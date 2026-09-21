#include <minix/config.h>
#include "../const.h"
#if (MACHINE == LISA)

| lisampx.s -- exception vectors, start-up, context save and restore for
| Minix on the Apple Lisa. Derived from stmpx.s (Minix-ST); the save,
| restart and async logic is unchanged except where noted.
|
| Differences from stmpx.s:
| - The ST vector table put each vector's trap number in the high byte of
|   the handler address and recovered it from the return address pushed by
|   "bsr". Here every vector has its own stub that stores the number in
|   trapno (synchronous) or pushes it (asynchronous), so nothing depends on
|   the CPU keeping the high byte of the PC.
| - Lisa interrupt levels (UniPlus sys/ivec.s): 1 vertical retrace and
|   parallel port VIA, 2 COPS (keyboard, mouse, clock), 3-5 expansion
|   slots, 6 SCC, 7 NMI. unlock() enables all levels (the ST used level 2
|   to block its horizontal blank interrupt).
| - The boot block (boot/lisaboot.S) passes the boot device in d5, the
|   physical start of RAM in d6 and the logical end of RAM in d7.
| - restart loads MMU context 1 with the process's memory map before it
|   returns to user mode (lisammu.c), and lisa_mmu_write does the register
|   writes in setup mode from .setup.
|
| Only the registers that are scratch for the C compiler are saved by the
| asynchronous interrupt stubs; the others are saved by the C prologues.

#define FREEREGS d0-d1/a0-a1

	.globl	lock
	.globl	unlock
	.globl	restore
	.globl	reboot
	.globl	sizes
	.globl	lisa_bootdev, lisa_membase, lisa_memend
	.globl	lisa_mmu_write, lisa_mmu_table

| offsets into a proc table entry
sava6	= 56
savsp	= 60
savpc	= 64
savsr	= 68
savtt	= 77

| ---- exception vectors, address 0 --------------------------------------------

	.text
	.long	0			| 0: reset SSP
	.long	start			| 1: reset PC
	.long	errvec2			| 2: bus error
	.long	errvec3			| 3: address error
	.irp	n,4,5,6,7,8
	.long	trpvec\n
	.endr
	.long	trc			| 9: trace
	.irp	n,10,11,12,13,14,15
	.long	trpvec\n
	.endr
	.irp	n,16,17,18,19,20,21,22,23,24
	.long	nonvec\n
	.endr
	.long	lv1			| 25: level 1
	.long	lv2			| 26: level 2
	.irp	n,27,28,29
	.long	nonvec\n		| 27-29: levels 3-5 (expansion slots)
	.endr
	.long	lv6			| 30: level 6: SCC
	.long	nonvec31		| 31: level 7: NMI
	.long	sys			| 32: TRAP #0, system call
	.irp	n,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
	.long	trpvec\n
	.endr
	.irp	n,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63
	.long	nonvec\n
	.endr
	.rept	256-64
	.long	nonvec64
	.endr

| gap kept from stmpx.s
	.space	0x0200

| ---- start-up --------------------------------------------------------------------

start:
	move.w	#0x2700,sr
	move.l	#k_stktop,sp
	move.l	d5,lisa_bootdev
	move.l	d6,lisa_membase
	move.l	d7,lisa_memend
	jsr	main
	bra	restart

| ---- synchronous traps: save full context -------------------------------------

| Each stub masks interrupts before recording its trap number, so an
| interrupt cannot change trapno before save copies it.

	.irp	n,2,3
errvec\n:
	move.w	#0x2700,sr
	move.b	#\n,trapno
	add.l	#8,sp			| remove extra context of a group 0 frame
	bra	trp
	.endr

	.irp	n,4,5,6,7,8,10,11,12,13,14,15,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
trpvec\n:
	move.w	#0x2700,sr
	move.b	#\n,trapno
	bra	trp
	.endr

	.irp	n,16,17,18,19,20,21,22,23,24,27,28,29,31,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64
nonvec\n:
	move.w	#0x2700,sr
	move.b	#\n,trapno
	bra	non
	.endr

trp:
	bsr	save
	jsr	trap
	bra	restart
trc:
	btst	#5,(sp)			| tracing through trap?
	beq	trace
	rte				| don't trace; execute system call
trace:
	move.w	#0x2700,sr
	move.b	#9,trapno
	bra	trp
non:
	bsr	save
	jsr	none
	bra	restart
sys:
	move.w	#0x2700,sr
	move.b	#32,trapno
	bsr	save			| d0, d1 and a0 not modified
	move.l	a0,-(sp)		| m_ptr
	move.w	d1,-(sp)		| src_dest
	move.w	d0,-(sp)		| SEND/RECEIVE/BOTH
	move.l	proc_ptr,a6		| needed to store return value
					| warning: sys_call may change proc_ptr
	jsr	sys_call		| sys_call(func,src_dest,m_ptr)
	move.l	d0,(a6)
	add.l	#8,sp
	bra	restart

| ---- asynchronous interrupts: context saved only if necessary ---------------

| Each pushes the scratch registers and the vector number, which becomes the
| C handler's argument, then enters async with the handler in a0.
lv1:
	movem.l	FREEREGS,-(sp)
	move.w	#25,-(sp)
	move.l	#lisa_level1,a0
	bra	async
lv2:
	movem.l	FREEREGS,-(sp)
	move.w	#26,-(sp)
	move.l	#lisa_level2,a0
	bra	async
lv6:
	movem.l	FREEREGS,-(sp)
	move.w	#30,-(sp)
	move.l	#lisa_level6,a0
	bra	async

async:
	add.b	#1,k_reenter		| from -1 if not reentering
	jsr	(a0)			| call service routine
	move.w	#0x2700,sr
	move.b	1(sp),trapno		| vector number, for save below
	tst.w	(sp)+			| pop vector number
	sub.b	#1,k_reenter
	movem.l	(sp)+,FREEREGS
	btst	#5,(sp)			| previously in kernel mode?
					| (interrupted a task or another interrupt)
	bne	L3			| yes: branch and return from interrupt
	cmp.l	#0,rdy_head+TASK_Q	| any task just readied?
	bne	L4			| yes: branch and do task switch
L3:
	rte
L4:
	bsr	save
	jsr	lock_pick_proc
	bra	restart

| ---- task switch by save and restart ----------------------------------------

save:
	move.w	#0x2700,sr
	move.l	a6,-(sp)
	move.l	proc_ptr,a6
	movem.l	d0-d7/a0-a5,(a6)
	move.l	(sp)+,sava6(a6)		| a6
	lea	10(sp),a1
	btst	#5,4(sp)		| test old S-bit
	bne	L5			| jump if S-bit on
	move.l	usp,a1
L5:	move.l	a1,savsp(a6)		| old sp: usp or ksp
	move.b	trapno,savtt(a6)	| trap type
	move.l	(sp)+,a1		| return address
	move.w	(sp)+,savsr(a6)		| sr
	move.l	(sp)+,savpc(a6)		| pc
	add.b	#1,k_reenter		| from -1 if not reentering
	jmp	(a1)

restart:
| Flush any held-up interrupts.
| This reenables interrupts, so the current interrupt handler may reenter.
| This doesn't matter, because the current handler is about to exit and no
| other handlers can reenter since flushing is only done when k_reenter == 0.

	move.w	#0x2700,sr
	tst.b	k_reenter
	bne	over_call_unhold
	cmp.l	#0,held_head
	beq	over_call_unhold
	jsr	unhold
over_call_unhold:
	sub.b	#1,k_reenter
	jsr	checksp
	move.l	proc_ptr,a6
	btst	#5,savsr(a6)		| returning to user mode?
	bne	L5a
	move.l	a6,-(sp)		| yes: load MMU context 1 for it
	jsr	lisa_mmu_switch
	addq.l	#4,sp
	move.l	proc_ptr,a6
L5a:	move.l	savsp(a6),a0		| old sp: usp or ksp
	btst	#5,savsr(a6)		| test old S-bit
	bne	L6			| jump if S-bit on
	move.l	a0,usp
	bra	L7
L6:	move.l	a0,sp
L7:	move.l	savpc(a6),-(sp)		| pc
	move.w	savsr(a6),-(sp)		| sr
	movem.l	(a6),d0-d7/a0-a6
	rte

| ---- interrupt priority -------------------------------------------------------

lock:
	move.w	sr,d0
	move.w	#0x2700,sr
	rts
restore:
	move.w	4(sp),sr
	rts
unlock:
	move.w	#0x2000,sr		| all interrupt levels enabled
	rts
reboot:
	move.l	4,a0
	jmp	(a0)

| ---- MMU register writes in setup mode ---------------------------------------

| void lisa_mmu_write(int n): perform the first n writes of lisa_mmu_table
| (lisammu.c): each entry a long register address and a word value. In
| setup mode code and data must lie where address bit 14 is set, so both
| are in .setup, which toolchain/lisa-kernel.ld places there. Interrupts
| must be off, and nothing touches the stack while setup mode is on.

	.section .setup,"awx"
lisa_mmu_write:
	move.w	4(sp),d0		| n, 16-bit int
	subq.w	#1,d0
	bmi	2f
	lea	lisa_mmu_table,a0
	move.b	#0,0xFCE010		| setup mode on
1:	move.l	(a0)+,a1
	move.w	(a0)+,(a1)
	dbra	d0,1b
	move.b	#0,0xFCE012		| setup mode off
2:	rts

	.even
lisa_mmu_table:
	.space	6*256			| MAX_WRITES entries (lisammu.c)
	.even

| ---- data ----------------------------------------------------------------------

	.data

| build (tools/build.py) patches the sizes of kernel, mm, fs and init here;
| 0x526F is its magic number.
sizes:
	.word	0x526F,0,0,0,0,0,0,0

lisa_bootdev:	.long	0
lisa_membase:	.long	0
lisa_memend:	.long	0
trapno:		.byte	0
		.byte	0

	.space	K_STACK_BYTES
k_stktop:

	.text
	.globl	write
write:
	rts

#endif /* MACHINE == LISA */
