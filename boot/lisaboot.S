| lisaboot.S -- ProFile boot block for Minix on the Apple Lisa.
|
| The boot ROM reads block 0 of the boot ProFile (tag at 0x1FFEC, data at
| 0x20000), checks that tag bytes 4-5 are 0xAAAA and calls the data with
| the address of its read-block routine for slot cards on the stack. This
| code must be position independent and at most 512 bytes.
|
| It reads the image header in block 1 (written by tools/mklisa.py), loads
| the image into a staging area, copies it to its load address and jumps to
| its entry point with
|	d5 = boot device byte (ROM global 0x1B3)
|	d6 = physical start of RAM (ROM global 0x2A4)
|	d7 = logical end of RAM (ROM global 0x2A8)
| These are read before the copy, which may overwrite the ROM's low-memory
| globals.
|
| ROM read routine calling convention, as used by UniPlus stand/wbblk.s
| (built-in port) and stand/pbblk.s (slot cards): d1 = block number,
| d2 = timeout count, d3 = retries, d4 = spare threshold, a1 = where the
| 20 tag bytes go, a2 = where the 512 data bytes go, a0 = the value the ROM
| passed in a0; result in d0, 0 = success. Across calls this code relies
| only on d5, a2, a4 and a5 being preserved, as those boot blocks do;
| everything else is reloaded from the header buffer.
|
| Header (block 1, big-endian longs): magic "MNXL", version, first image
| block, image block count, load address, entry address, first file system
| block, file system block count.
|
| Assembled with m68k-elf-gcc -x assembler-with-cpp -Wa,--register-prefix-optional.

ROM_PROREAD	= 0xFE0090	| read block from the built-in parallel port
ROM_MONITOR	= 0xFE0084	| ROM monitor
ROM_BOOTDEV	= 0x1B3
ROM_MEMBASE	= 0x2A4
ROM_MEMEND	= 0x2A8

STACK		= 0x3FC00	| between this block and the staging area
HDRBUF		= 0x3FE00	| header block data
TAGBUF		= HDRBUF-20	| tag bytes of every block read
STAGE		= 0x40000	| image staging area
MAGIC		= 0x4D4E584C	| "MNXL"
VERSION		= 1

H_MAGIC		= 0
H_VERSION	= 4
H_FIRST		= 8
H_COUNT		= 12
H_LOAD		= 16
H_ENTRY		= 20

	.text
	.globl	start
start:
	move.l	(sp),a4			| slot card read routine, from the ROM
	move.l	a0,a5			| a0 must be passed back to the ROM
	move.w	#0x2700,sr
	move.l	#STACK,sp
	cmp.b	#2,ROM_BOOTDEV
	bgt.s	1f			| booted from a slot card
	move.l	#ROM_PROREAD,a4		| built-in parallel port
1:
	| Read and check the header.
	move.l	#1,d1
	move.l	#HDRBUF,a2
	bsr	rdblk
	cmp.l	#MAGIC,HDRBUF+H_MAGIC
	bne	fail
	cmp.l	#VERSION,HDRBUF+H_VERSION
	bne	fail

	| Read the image into the staging area. The block number is worked
	| out from a2 for each block.
	move.l	HDRBUF+H_COUNT,d5	| blocks left
	beq	fail
	move.l	#STAGE,a2
2:	move.l	a2,d1
	sub.l	#STAGE,d1
	moveq	#9,d0
	lsr.l	d0,d1
	add.l	HDRBUF+H_FIRST,d1
	bsr	rdblk
	add.l	#512,a2
	subq.l	#1,d5
	bne.s	2b

	| a2 is now the end of the staged image. Put the copy routine there,
	| above both the staged image and its destination.
	move.l	a2,a1
	lea	copy(pc),a0
	move.w	#copyend-copy-1,d0
3:	move.b	(a0)+,(a1)+
	dbra	d0,3b

	| Arguments for the copy routine and the kernel.
	move.l	HDRBUF+H_COUNT,d4
	asl.l	#7,d4			| longs to copy: blocks * 512 / 4
	move.l	#STAGE,a0		| from
	move.l	HDRBUF+H_LOAD,a1	| to
	move.l	HDRBUF+H_ENTRY,a3	| entry point
	clr.l	d5
	move.b	ROM_BOOTDEV,d5
	move.l	ROM_MEMBASE,d6
	move.l	ROM_MEMEND,d7
	jmp	(a2)

| Read block d1 into a2. Every block's 20 tag bytes go to the same place,
| TAGBUF, below the header block; placing them just below a2 would
| overwrite the last 20 bytes of the block read before. Uses d0-d4, a0, a1.
rdblk:
	move.l	#TAGBUF,a1
	move.l	#0x1000,d2		| timeout count
	move.l	#10,d3			| retries
	move.l	#3,d4			| spare threshold
	move.l	a5,a0
	clr.l	d0
	jsr	(a4)
	tst.b	d0
	bne.s	fail
	rts

fail:
	jmp	ROM_MONITOR

| Copied above the staged image before it runs. Copies d4 longs from a0 to
| a1 in ascending order, which is safe when the destination is below the
| source, then jumps to the entry point in a3.
copy:
	move.l	(a0)+,(a1)+
	subq.l	#1,d4
	bne.s	copy
	jmp	(a3)
copyend:
