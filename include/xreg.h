/* xreg.h - reader/writer for /dev_flash2/etc/xRegistry.sys
 *
 * The PS3 keeps its user settings in a single 256 KB binary file.  Region
 * settings live in it as plain 32-bit big-endian integers:
 *
 *   /setting/bddvd/dvdRegionCode   0..6
 *   /setting/bddvd/bdRegionCode    1=A 2=B 4=C
 *   /setting/bddvd/dvdTvSystem     0=NTSC 1=PAL 2=PAL60 3=NTSC-J
 *   /setting/system/region         0x00 default, 0x83..0x8F country code
 *
 * Layout (reverse engineered by Mysis, stoker25 and the PS3 wiki):
 *
 *   0x00000  header, 16 bytes
 *   0x00010  key table    : u16 id, u16 len, u8 type, char key[len], u8 0x00
 *   0xFFF0   fixed block  : 4D26 x2, not part of the entries
 *   0x10000  value table  : u16 flags, u16 keyref, u16 id, u16 len, u8 type,
 *                          u8 value[len], u8 0x00
 *
 *   keyref == (absolute offset of the key entry) - 0x10
 *   key table ends on   AABB CCDD EE
 *   value table ends on AABB CCDD EE00
 *
 * Both tables use that marker as an in-table breakpoint as well, so the walk
 * stops there and a byte-exact rescan is used as a fallback.
 */
#ifndef DVDREGION_XREG_H
#define DVDREGION_XREG_H

#include <stdint.h>

#define XREG_PATH        "/dev_flash2/etc/xRegistry.sys"
#define XREG_BACKUP_PATH "/dev_flash2/etc/backup/xRegistry.sys"
#define XREG_SIZE        0x40000

#define DVD_REGION_FREE_MARK 0xFF  /* not written, used for display only */

/* One entry of the regionPS3 table from xai_plugin, plus a readable name. */
typedef struct
{
	uint32_t    ps3_region;   /* 0x00 or 0x83..0x8F, stored in /setting/system/region */
	const char *name;
	uint32_t    dvd_region;   /* 0..6 */
	uint32_t    bd_region;    /* 0 = default, 1 = A, 2 = B, 4 = C */
} ps3_region_entry;

extern const ps3_region_entry PS3_REGIONS[];
extern const int PS3_REGION_COUNT;

int  ps3_region_lookup(uint32_t ps3_region);            /* -1 when unknown   */
const char *ps3_region_name(uint32_t ps3_region);
const char *dvd_region_name(uint32_t code);            /* "Region 1..6", ... */
const char *bd_region_name(uint32_t code);             /* "Region A/B/C", ...*/
const char *tv_system_name(uint32_t code);             /* "NTSC", "PAL", ... */

/* Current values as stored in the registry. */
typedef struct
{
	uint32_t ps3_region;
	uint32_t dvd_region;
	uint32_t bd_region;
	uint32_t tv_system;
	int      have_ps3;
	int      have_dvd;
	int      have_bd;
	int      have_tv;
	int      loaded;        /* 1 when the file was read and at least one key found */
} xreg_state;

/* Loads xRegistry.sys. Returns 0 on success. */
int  xreg_load(void);

/* Loads an arbitrary registry image (used by the restore path).  The bytes
 * end up in the same working buffer xreg_save() writes back. */
int  xreg_load_from(const char *path);

/* Reads the four region settings into `out`. */
int  xreg_read_state(xreg_state *out);

/* Writes the four region settings. Values are only written when the key
 * already exists in the file, so a layout we do not understand is never
 * corrupted.  Returns the number of keys written, or -1 on a read/parse
 * failure. */
int  xreg_apply(const xreg_state *st);

/* Writes the in-memory image back to flash (primary + backup location). */
int  xreg_save(void);

/* Copies the currently loaded (unmodified) image to `path`. */
int  xreg_backup(const char *path);

#endif /* DVDREGION_XREG_H */
