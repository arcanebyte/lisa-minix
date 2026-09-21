#
| public labels
	.globl	begtext
	.globl	begdata
	.globl	begbss
	.globl	exit
	.globl	data_org
#ifdef ACK
	.globl	EXIT
	.globl	.trpim
	.globl	.trppc
#endif /* ACK */
| external references
#ifdef ACK
	.text
	.section	.rodata
	.data
	.bss
#endif /* ACK */

	.text
begtext:
	move.l	stackpt,sp
	jsr	main
#ifdef ACK
EXIT:
#endif /* ACK */
exit:
	bra	exit		| this will never be executed

	.data
begdata:
	| fs needs to know where build stuffed table
data_org:
	| 0xDADA is magic number for build
	.word	0xDADA,0,0,0,0,0,0,0
#ifdef ACK
.trpim:	.word	0
.trppc:	.long	0
#endif /* ACK */

	.bss
begbss:
