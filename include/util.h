/* util.h - small helpers shared by every module */
#ifndef DVDREGION_UTIL_H
#define DVDREGION_UTIL_H

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------- endian ---- */

static inline uint16_t be16(const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	return (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
}

static inline uint32_t be32(const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
	       ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static inline uint32_t le32(const void *p)
{
	const uint8_t *b = (const uint8_t *)p;
	return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) |
	       ((uint32_t)b[1] << 8) | (uint32_t)b[0];
}

static inline void put_be32(void *p, uint32_t v)
{
	uint8_t *b = (uint8_t *)p;
	b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16);
	b[2] = (uint8_t)(v >> 8);  b[3] = (uint8_t)v;
}

/* --------------------------------------------------------------- strings ---- */

size_t ustrlcpy(char *dst, const char *src, size_t size);
size_t ustrlcat(char *dst, const char *src, size_t size);
int   ustrcasecmp_ascii(const char *a, const char *b);

/* Replace every character the PS3 filesystem rejects with '_'. */
void  sanitize_filename(const char *in, char *out, size_t out_size);

/* ------------------------------------------------------------ formatting ---- */

/* "4.70 GB", "812 MB", "0 B" */
void  format_size(uint64_t bytes, char *out, size_t out_size);

/* "01:23:45" for a duration in seconds */
void  format_duration(uint32_t seconds, char *out, size_t out_size);

/* "20260101-142530" (UTC) for the current clock, used in dump file names. */
void  format_stamp(char *out, size_t out_size);

/* ------------------------------------------------------------- filesystem --- */

#define APP_TITLE_ID "DVDRGN01000"

/* Create `path` and every missing parent. Existing dirs are left alone. */
int   fs_mkdir_p(const char *path, unsigned mode);

/* Free bytes on the device that contains `path` (device root recommended). */
int   fs_free_space(const char *path, uint64_t *out_bytes);

/* Open `path` for writing, create + chmod 0777 (required for /dev_hdd0). */
int   fs_create(const char *path, unsigned mode);

/* read()/write() loops that handle short transfers. Return 0 on full success. */
int   io_read_all(int fd, void *buf, size_t len);
int   io_write_all(int fd, const void *buf, size_t len);

/* TRUE when `path` exists. */
int   fs_exists(const char *path);

/* TRUE when `path` is a FAT32 mount point (4 GB per-file limit applies). */
int   fs_is_fat(const char *path);

#endif /* DVDREGION_UTIL_H */
