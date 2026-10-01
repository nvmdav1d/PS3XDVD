/* Stub of <sys/time.h> for the host compile check (gettimeofday). */
#ifndef STUB_SYS_TIME_H
#define STUB_SYS_TIME_H

#include <ppu-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct timeval
{
	long tv_sec;
	long tv_usec;
};

struct timezone
{
	int tz_minuteswest;
	int tz_dsttime;
};

int gettimeofday(struct timeval *tv, struct timezone *tz);

#ifdef __cplusplus
}
#endif

#endif