#
	.globl	brksize
#ifdef ACK
	.text
	.section	.rodata
	.data
	.bss
#endif /* ACK */

	.data
brksize:
	.long	end
