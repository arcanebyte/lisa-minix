/* forkbench -- time fork and fork+exec on Minix (PLAN.md phase 5).
 *
 * Usage: forkbench [N]
 *
 * Runs three loops, N times each (default 50), and prints how long each
 * took in seconds, from time() (times() has no elapsed time in Minix 1.5).
 * Each line is also printed when its loop starts, so the timestamps in the
 * serial log give finer times:
 *   fork:       fork; the child exits at once; the parent waits
 *   fork+work:  as fork, but the child first writes to 32 KB of the
 *               parent's data (on Minix-ST this forces the parent and the
 *               child to trade places, a shadowing "flip")
 *   fork+exec:  fork; the child execs /bin/echo; the parent waits
 * Output lines start with "forkbench:" so a test script can find them.
 */

#include <sys/types.h>
#include <stdio.h>

char big[32768];

long ticks()
{
  long time();

  return time((long *) 0);
}

int wait_child()
{
  int status;

  return wait(&status);
}

void report(what, n, t)
char *what;
int n;
long t;
{
  printf("forkbench: %-10s %3d times %5ld s\n", what, n, t);
  fflush(stdout);
}

void start(what)
char *what;
{
  printf("forkbench: %-10s start\n", what);
  fflush(stdout);
}

main(argc, argv)
int argc;
char *argv[];
{
  int n = 50, i;
  unsigned j;		/* int is 16 bits: an int never reaches sizeof(big) */
  long t;
  static char *args[] = { "echo", (char *) 0 };

  if (argc > 1) n = atoi(argv[1]);

  start("fork");
  t = ticks();
  for (i = 0; i < n; i++) {
	if (fork() == 0) _exit(0);
	wait_child();
  }
  report("fork", n, ticks() - t);

  start("fork+work");
  t = ticks();
  for (i = 0; i < n; i++) {
	if (fork() == 0) {
		for (j = 0; j < sizeof(big); j++) big[j] = j;
		_exit(0);
	}
	wait_child();
  }
  report("fork+work", n, ticks() - t);

  start("fork+exec");
  t = ticks();
  for (i = 0; i < n; i++) {
	if (fork() == 0) {
		close(1);	/* keep echo's newline off the console */
		execv("/bin/echo", args);
		_exit(1);
	}
	wait_child();
  }
  report("fork+exec", n, ticks() - t);
  printf("forkbench: done\n");
  exit(0);
}
