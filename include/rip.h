/* rip.h - DVD dumping engines */
#ifndef DVDREGION_RIP_H
#define DVDREGION_RIP_H

#include <stdint.h>
#include <stddef.h>

#define RIP_FAT_SPLIT 0xFFFF0000ULL   /* 4 GB - 64 KB, FAT32 file limit */

typedef struct
{
	uint64_t done;
	uint64_t total;
	uint32_t elapsed_ms;
	int      finished;
	int      ok;
	int      failed;
	int      patched;            /* VIDEO_TS.IFO region mask was zeroed */
	char     message[128];
	char     out_path[512];
} rip_status;

/* Return non-zero from the callback to abort the job. */
typedef int (*rip_progress_fn)(void *ctx, const rip_status *st);

/* 1:1 sector dump. Needs sys_storage_open, i.e. a modified hypervisor. */
int rip_raw_iso(const char *out_path, uint64_t sectors, int patch_region,
                rip_progress_fn cb, void *ctx, rip_status *st);

/* Copies /dev_bdvd/VIDEO_TS into `out_dir`. Works on every firmware. */
int rip_copy_video_ts(const char *out_dir, int patch_region,
                      rip_progress_fn cb, void *ctx, rip_status *st);

/* Zeroes the prohibited-region byte of a VIDEO_TS.IFO. 0 on success. */
int rip_region_free_ifo(const char *ifo_path);

#endif /* DVDREGION_RIP_H */
