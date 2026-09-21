#
	.globl	send
	.globl	receive
	.globl	sendrec
#ifdef ACK
	.text
	.section	.rodata
	.data
	.bss
#endif /* ACK */
| =====================================================================
|                           send and receive                          =
| =====================================================================
| send(), receive(), sendrec() destroy d0, d1, and a0.

| See ../h/com.h for C definitions
SEND	= 1
RECEIVE	= 2
BOTH	= 3

	.text

send:	move.w	#SEND,d0	| send(dest, ptr)
	bra	L0

receive:
	move.w	#RECEIVE,d0	| receive(src, ptr)
	bra	L0

sendrec:
	move.w	#BOTH,d0	| sendrec(srcdest, ptr)
L0:				| d0 = SEND/RECEIVE/BOTH
	move.w	4(sp),d1	| d1 = dest-src
	move.l	6(sp),a0	| a0 = message pointer
	trap	#0		| trap to the kernel
	rts			| return
