/* segv -- a user program that touches memory outside itself (PLAN.md phase 5).
 *
 * Usage: segv [ADDRESS]
 *
 * Writes a byte to ADDRESS (hex, default 80000: 512 KB, above this small
 * program's image but inside the RAM of a 1 MB Lisa, so without memory
 * protection the write lands in some other process or the kernel), then to
 * 600000, which is outside RAM, and says after each write whether it
 * survived. With the MMU the first write is a bus error, the kernel sends
 * SIGSEGV and the process dies without printing "survived".
 */

#include <stdio.h>

main(argc, argv)
int argc;
char *argv[];
{
  unsigned long addr = 0x80000L;
  char *p;

  if (argc > 1) sscanf(argv[1], "%lx", &addr);
  printf("segv: writing to %lx\n", addr);
  fflush(stdout);
  p = (char *) addr;
  *p = 0x55;
  printf("segv: survived the write to %lx\n", addr);
  p = (char *) 0x600000L;
  printf("segv: writing to 600000\n");
  fflush(stdout);
  *p = 0x55;
  printf("segv: survived the write to 600000\n");
  exit(0);
}
