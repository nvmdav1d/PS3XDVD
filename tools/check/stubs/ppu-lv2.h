/* Stub of <ppu-lv2.h> for the host compile check.
 *
 * The real header builds the `sc` instruction inline. Here the same macros are
 * plain calls into a recorder so the argument order can still be checked. */
#ifndef STUB_PPU_LV2_H
#define STUB_PPU_LV2_H

#include <ppu-types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Records the syscall number and the seven argument registers. */
long ps3check_record(long num, long a1, long a2, long a3,
                     long a4, long a5, long a6, long a7);

#define PS3CHECK_SC(n, a1, a2, a3, a4, a5, a6, a7) \
	ps3check_record((long)(n), (long)(a1), (long)(a2), (long)(a3), \
	                (long)(a4), (long)(a5), (long)(a6), (long)(a7))

#define lv2syscall0(n)                 PS3CHECK_SC(n, 0, 0, 0, 0, 0, 0, 0)
#define lv2syscall1(n,a1)              PS3CHECK_SC(n, a1, 0, 0, 0, 0, 0, 0)
#define lv2syscall2(n,a1,a2)           PS3CHECK_SC(n, a1, a2, 0, 0, 0, 0, 0)
#define lv2syscall3(n,a1,a2,a3)        PS3CHECK_SC(n, a1, a2, a3, 0, 0, 0, 0)
#define lv2syscall4(n,a1,a2,a3,a4)     PS3CHECK_SC(n, a1, a2, a3, a4, 0, 0, 0)
#define lv2syscall5(n,a1,a2,a3,a4,a5)  PS3CHECK_SC(n, a1, a2, a3, a4, a5, 0, 0)
#define lv2syscall6(n,a1,a2,a3,a4,a5,a6) \
	PS3CHECK_SC(n, a1, a2, a3, a4, a5, a6, 0)
#define lv2syscall7(n,a1,a2,a3,a4,a5,a6,a7) \
	PS3CHECK_SC(n, a1, a2, a3, a4, a5, a6, a7)

extern long p1;

#define return_to_user_prog(ret_type)  return (ret_type)(p1)

#ifdef __cplusplus
}
#endif

#endif