/* disc.h - DVD-Video detection and VIDEO_TS.IFO parsing */
#ifndef DVDREGION_DISC_H
#define DVDREGION_DISC_H

#include <stdint.h>
#include <stddef.h>

#define BDVD_ROOT "/dev_bdvd"
#define DVD_VIDEO_TS_DIR "VIDEO_TS"
#define DVD_VMG_IFO "VIDEO_TS.IFO"

#define IFO_REGION_MASK_OFFSET 0x22   /* VMG category field, byte 1 = prohibited mask */

typedef struct
{
	int      present;          /* a VIDEO_TS directory exists              */
	int      ifo_ok;           /* VIDEO_TS.IFO was found and is sane       */
	char     provider[33];     /* 32 ASCII bytes from the VMG provider id  */
	uint8_t  region_mask;      /* prohibited-region mask at IFO + 0x22     */
	uint8_t  region_mask2;     /* the following byte, reported for info    */
	uint32_t allowed_regions;  /* bit r (1..8) set => that region plays    */
	char     region_list[40];  /* "R2" / "R2,R3" / "ALL" / "RCE?"          */
	int      region_free;      /* mask == 0                                */
	int      rce_suspected;    /* mask == 0xFF, RCE or unreadable          */
	uint32_t vmg_last_sector;
	uint32_t ifo_last_sector;
	uint32_t mat_end;
	uint32_t fp_pgc;
	uint32_t num_titles;

	/* Why VIDEO_TS.IFO could not be used: 0 ok, 1 open/read failed,
	 * 2 magic mismatch (present but not a DVD-Video VMG). */
	int      ifo_err;
} disc_info;

/* Reads /dev_bdvd. Returns 0 when a DVD-Video disc was recognised. */
int disc_probe(disc_info *out);

/* Best-effort total sector count (2048 bytes each). `how` gets a short
 * description of where the number came from. Returns 0 on success.
 * This talks to the drive, so the caller must cache the answer. */
int disc_sector_count(uint64_t *out_sectors, char *how, size_t how_size);

/* Total bytes behind /dev_bdvd/VIDEO_TS, used for the free space check. */
int disc_video_ts_size(uint64_t *out_bytes);

/* Reads the first `len` bytes of `name` from /dev_bdvd/VIDEO_TS, trying both
 * the plain ISO9660 name and the ";1" versioned form. */
int disc_read_ifo(void *buf, uint32_t len);

void disc_region_string(uint8_t mask, uint32_t allowed, char *out, size_t out_size);

/* A short, filesystem safe file name for the loaded disc. */
void disc_make_basename(const disc_info *info, char *out, size_t out_size);

#endif /* DVDREGION_DISC_H */
