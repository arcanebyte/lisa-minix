#
	.globl	begsig
#ifdef ACK
	.text
	.section	.rodata
	.data
	.bss
#endif /* ACK */

#ifdef ALCYON
#define FREEREGS d0-d2/a0-a2
#endif
#ifdef ACK
#define FREEREGS d0-d2/a0-a1
#endif

mtype = 2			| M+mtype = &M.m_type
	.text
begsig:
	movem.l	FREEREGS,-(sp)
	clr.l	d0
#ifdef ALCYON
	move.w	24(sp),d0	| d0 = signal number
#endif
#ifdef ACK
	move.w	20(sp),d0	| d0 = signal number
#endif
	move.w	_M+mtype,-(sp)	| push status of last system call
	move.w	d0,-(sp)	| func called with signal number as arg
	asl.l	#2,d0		| pointers are four bytes on 68000
	move.l	#__vectab,a0
	move.l	-4(a0,d0),a0	| a0 = address of routine to call
	jsr	(a0)
back:
	add.l	#2,sp		| get signal number off stack
	move.w	(sp)+,_M+mtype	| restore status of previous system call
	movem.l	(sp)+,FREEREGS
	add.l	#2,sp		| remove signal number from stack
	rtr
