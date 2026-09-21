#ifdef boot_fd
#define	BOOT
#define	type_fd
#endif
#ifdef boot_dd
#define	BOOT
#define	type_dd
#endif

#ifndef type_dd
#ifndef type_fd
#define	type_fd
#endif
#endif

#ifdef ACK
	.text
	.section	.rodata
	.data
	.bss
#endif

	.text
start:
#ifdef BOOT
	bra.s	boot		| 000: jump to loader (2 bytes: fields below are at fixed offsets)
#else
	rts			| 000: do not boot
#endif
	.ascii	"MINIX "	| 002: 6 byte identification
	.byte	0,0,0		| 008: volume serial
	.byte	0,2		| 00B: 512 bytes/sector (low byte first)
	.byte	2		| 00D: 2 sectors/cluster
	.byte	1,0		| 00E: reserved sector (low byte first)
	.byte	2		| 010: number of FATS
	.byte	112,0		| 011: number of dirs (low byte first)
#ifdef type_fd
	.byte	208,2		| 013: 720 sectors (low byte first)
 	.byte	248		| 015: media descriptor (80 track SS)
#endif
#ifdef type_dd
	.byte	160,5		| 013: 1440 sectors (low byte first)
	.byte	249		| 015: media descriptor (80 track DS)
#endif
	.byte	5,0		| 016: sectors/FAT (low byte first)
	.byte	9,0		| 018: sectors/track (low byte first)
#ifdef type_fd
	.byte	1,0		| 01A: number of sides (low byte first)
#endif
#ifdef type_dd
	.byte	2,0		| 01A: number of sides (low byte first)
#endif
	.byte	0,0		| 01C: hidden sectors (low byte first)
