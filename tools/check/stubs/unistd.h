/* Stub of <unistd.h> for the host compile check (usleep only). */
#ifndef STUB_UNISTD_H
#define STUB_UNISTD_H

#include <ppu-types.h>

#ifdef __cplusplus
extern "C" {
#endif

int usleep(unsigned int usec);
int sleep(unsigned int sec);

#ifdef __cplusplus
}
#endif

#endif