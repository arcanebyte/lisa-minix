| lisastart.S -- start-up, exception vectors and MMU access for the phase 1
| Lisa test kernel (PLAN.md phase 1). Linked at address 0 by
| toolchain/lisa-test.ld and started by boot/lisaboot.S with
| d5 = boot device, d6 = physical start of RAM, d7 = logical end of RAM.
|
| Assembled with -x assembler-with-cpp -Wa,--register-prefix-optional.

VRT_OFF		= 0xFCE018
VRT_ON		= 0xFCE01A
MMU_SETUP_ON	= 0xFCE010
MMU_SETUP_OFF	= 0xFCE012

| ---- exception vectors, address 0 ----------------------------------------

| Every vector except 0-3 and 25 has its own stub, which pushes the vector
| number so the fault report can name it.
	.macro	vector n
	.long	vec\n
	.endm
	.macro	stub n
vec\n:	move.w	#0x2700,sr
	movem.l	d0-d7/a0-a6,-(sp)
	move.w	#\n,-(sp)
	bra	exception
	.endm

	.section .vectors,"ax"
	.long	kstack_top		| 0: reset SSP (not used)
	.long	start			| 1: reset PC
	.long	buserr			| 2: bus error
	.long	addrerr			| 3: address error
	.irp	n,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24
	vector	\n
	.endr
	.long	level1			| 25: level 1: vertical retrace, parallel VIA
	.long	level2			| 26: level 2: COPS (keyboard, mouse, clock)
	.irp	n,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
	vector	\n
	.endr

| ---- start-up -------------------------------------------------------------

	.text
	.globl	start
start:
	move.w	#0x2700,sr
	move.l	#kstack_top,sp
	move.l	d5,boot_dev
	move.l	d6,boot_membase
	move.l	d7,boot_memend
	move.l	#bss_start,a0
	move.l	#bss_end,a1
1:	cmp.l	a1,a0
	bcc.s	2f
	clr.b	(a0)+
	bra.s	1b
2:	jsr	main
halt:	stop	#0x2700
	bra.s	halt

| ---- interrupt priority -----------------------------------------------------

	.globl	spl0, spl7, splx
spl0:	move.w	sr,d0
	move.w	#0x2000,sr
	rts
spl7:	move.w	sr,d0
	move.w	#0x2700,sr
	rts
splx:	move.w	4(sp),sr		| int argument: 2 bytes under -mshort
	rts

| ---- exceptions ---------------------------------------------------------------

| Bus and address errors push a 14-byte group 0 frame: function code word,
| access address, instruction register word, SR, PC.
buserr:
	move.w	#0x2700,sr
	movem.l	d0-d7/a0-a6,-(sp)
	move.w	#2,-(sp)
	bra.s	group0
addrerr:
	move.w	#0x2700,sr
	movem.l	d0-d7/a0-a6,-(sp)
	move.w	#3,-(sp)
group0:
	move.l	sp,a0
	move.l	a0,-(sp)		| frame: vector, 15 registers, 14 bytes
	jsr	group0_fault		| does not return
	bra	halt

	.irp	n,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
	stub	\n
	.endr

exception:
	move.l	sp,a0
	move.l	a0,-(sp)		| frame: vector, 15 registers, SR, PC
	jsr	other_fault		| does not return
	bra	halt

| Level 1: count vertical retrace interrupts. Acknowledged by turning the
| interrupt off and on again (UniPlus sys/l1.c does the same).
level1:
	move.b	#0,VRT_OFF
	move.b	#0,VRT_ON
	addq.l	#1,ticks
	rte

| Level 2: the COPS has sent a byte (keyboard, mouse or clock). Reading the
| COPS VIA's port A takes the byte and clears the CA1 flag; the bytes are
| only counted here. [U include/sys/cops.h; E lisa/cpu_board/irq.c]
COPS_ORA	= 0xFCDD83
level2:
	tst.b	COPS_ORA
	addq.l	#1,cops_events
	rte

| ---- MMU registers ------------------------------------------------------------

| long mmu_read(long address): read a 12-bit MMU register in setup mode.
| void mmu_write(long address, long value)
| While setup mode is on, the code must run at an address whose bit 14 is
| set (lisaaddr.h), so these live in .setup, which toolchain/lisa-test.ld
| places accordingly. No stack use while setup mode is on.
	.section .setup,"ax"
	.globl	mmu_read, mmu_write
mmu_read:
	move.l	4(sp),a0
	move.w	sr,d1
	move.w	#0x2700,sr
	moveq	#0,d0
	move.b	d0,MMU_SETUP_ON
	move.w	(a0),d0
	move.b	#0,MMU_SETUP_OFF
	move.w	d1,sr
	and.l	#0xFFF,d0
	rts

mmu_write:
	move.l	4(sp),a0
	move.l	8(sp),d0
	move.w	sr,d1
	move.w	#0x2700,sr
	move.b	#0,MMU_SETUP_ON
	move.w	d0,(a0)
	move.b	#0,MMU_SETUP_OFF
	move.w	d1,sr
	rts

| ---- data --------------------------------------------------------------------

	.data
	.globl	boot_dev, boot_membase, boot_memend, ticks, cops_events
boot_dev:	.long	0
boot_membase:	.long	0
boot_memend:	.long	0
ticks:		.long	0
cops_events:	.long	0
