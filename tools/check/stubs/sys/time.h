/* Stub of <sys/time.h> for the host compile check (gettimeofday). */
#ifndef STUB_SYS_TIME_H
#define STUB_SYS_TIME_H

#include <ppu-types.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
/* mingw's <time.h> pulls in _timeval.h, which already defines both structs.
 * MSVC's does not, so it has to keep the definitions below. */
#  include <time.h>
#else
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
#endif

int gettimeofday(struct timeval *tv, struct timezone *tz);

#ifdef __cplusplus
}
#endif

#endif