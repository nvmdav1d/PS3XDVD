/* Support code for the host compile check. Never linked into the real app. */
#include <ppu-lv2.h>

#include <string.h>
#include <time.h>

#include <sys/time.h>
#include <unistd.h>

long p1;

/* The real inline asm leaves whatever the kernel returned in r3. Pretend it
 * succeeded so the check does not have to model error paths. */
long ps3check_record(long num, long a1, long a2, long a3,
                     long a4, long a5, long a6, long a7)
{
	(void)a1; (void)a2; (void)a3; (void)a4;
	(void)a5; (void)a6; (void)a7; (void)num;
	p1 = 0;
	return 0;
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	(void)tz;
	if (tv == NULL)
		return -1;
	tv->tv_sec  = (long)time(NULL);
	tv->tv_usec = 0;
	return 0;
}

int usleep(unsigned int usec)
{
	(void)usec;
	return 0;
}