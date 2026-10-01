/* xreg.c */
#include <stdio.h>
#include <string.h>

#include <lv2/sysfs.h>
#include <sys/file.h>

#include "util.h"
#include "xreg.h"

#define KEY_AREA_START 0x10u
#define KEY_AREA_END   0x10000u
#define VAL_AREA_START 0x10000u
#define VAL_AREA_END   0x20000u
#define KEYREF_BIAS    0x10u

static uint8_t  g_buf[XREG_SIZE];
static int      g_loaded;

const ps3_region_entry PS3_REGIONS[] = {
	{ 0x00, "Default (disabled)", 0, 0 },
	{ 0x83, "Japan",              2, 1 },
	{ 0x84, "USA",                1, 1 },
	{ 0x85, "Europe",             2, 2 },
	{ 0x86, "Korea",              3, 1 },
	{ 0x87, "United Kingdom",     2, 2 },
	{ 0x88, "Mexico",             4, 1 },
	{ 0x89, "Australia",          4, 2 },
	{ 0x8A, "Asia",               3, 1 },
	{ 0x8B, "Taiwan",             3, 1 },
	{ 0x8C, "Russia",             5, 4 },
	{ 0x8D, "China",              6, 4 },
	{ 0x8E, "Hong Kong",          3, 1 },
	{ 0x8F, "Brazil",             4, 1 }
};

const int PS3_REGION_COUNT = (int)(sizeof(PS3_REGIONS) / sizeof(PS3_REGIONS[0]));

int ps3_region_lookup(uint32_t ps3_region)
{
	int i;
	for (i = 0; i < PS3_REGION_COUNT; i++)
		if (PS3_REGIONS[i].ps3_region == ps3_region)
			return i;
	return -1;
}

const char *ps3_region_name(uint32_t ps3_region)
{
	int i = ps3_region_lookup(ps3_region);
	return (i >= 0) ? PS3_REGIONS[i].name : "Unknown";
}

const char *dvd_region_name(uint32_t code)
{
	switch (code)
	{
	case 0: return "Default";
	case 1: return "Region 1";
	case 2: return "Region 2";
	case 3: return "Region 3";
	case 4: return "Region 4";
	case 5: return "Region 5";
	case 6: return "Region 6";
	default: return "Invalid";
	}
}

const char *bd_region_name(uint32_t code)
{
	switch (code)
	{
	case 0: return "Default";
	case 1: return "Region A";
	case 2: return "Region B";
	case 4: return "Region C";
	default: return "Invalid";
	}
}

const char *tv_system_name(uint32_t code)
{
	switch (code)
	{
	case 0: return "NTSC";
	case 1: return "PAL";
	case 2: return "PAL 60Hz";
	case 3: return "NTSC-J";
	default: return "Invalid";
	}
}

/* ------------------------------------------------------------- file access -- */

static int read_file(const char *path, void *buf, uint64_t len)
{
	int fd = -1;
	int ret = sysFsOpen(path, SYS_O_RDONLY, &fd, NULL, 0);
	if (ret != 0 || fd < 0)
		return ret != 0 ? ret : -1;
	ret = io_read_all(fd, buf, (size_t)len);
	sysFsClose(fd);
	return ret;
}

static int write_file(const char *path, const void *buf, uint64_t len)
{
	int fd = fs_create(path, 0777);
	if (fd < 0)
		return -1;
	{
		int ret = io_write_all(fd, buf, (size_t)len);
		sysLv2FsFsync(fd);
		sysFsClose(fd);
		return ret;
	}
}

static int verify_header(void)
{
	static const uint8_t magic[16] = {
		0xBC, 0xAD, 0xAD, 0xBC, 0x00, 0x00, 0x00, 0x90,
		0x00, 0x00, 0x00, 0x02, 0xBC, 0xAD, 0xAD, 0xBC
	};
	return (memcmp(g_buf, magic, sizeof(magic)) == 0) ? 0 : -2;
}

int xreg_load_from(const char *path)
{
	g_loaded = 0;
	if (path == NULL)
		return -1;
	if (read_file(path, g_buf, XREG_SIZE) != 0)
		return -1;
	if (verify_header() != 0)
		return -2;
	g_loaded = 1;
	return 0;
}

int xreg_load(void)
{
	return xreg_load_from(XREG_PATH);
}

int xreg_backup(const char *path)
{
	if (!g_loaded)
		return -1;
	return write_file(path, g_buf, XREG_SIZE);
}

/* -------------------------------------------------------------- table walk -- */

typedef struct
{
	uint32_t key_off;      /* absolute offset of the key entry                 */
	uint32_t value_off;    /* absolute offset of the value bytes               */
	uint32_t value_len;
	uint8_t  value_type;   /* 0 bool, 1 int, 2 string                          */
	uint8_t  key_type;
} xreg_entry;

static int find_key_walk(const char *key, uint32_t *key_off)
{
	size_t n = strlen(key) + 1;   /* compare including the NUL */
	uint32_t off = KEY_AREA_START;

	while (off + 5 <= KEY_AREA_END)
	{
		uint16_t id  = be16(g_buf + off);
		uint16_t len = be16(g_buf + off + 2);
		uint8_t  typ = g_buf[off + 4];

		if (id == 0xAABB && len == 0xCCDD && typ == 0xEE)
			return -1;                       /* end of table */

		if (len == 0 || len > 0x7FF)
			return -1;                       /* malformed, stop guessing */

		if (off + 5 + len + 1 > KEY_AREA_END)
			return -1;

		if (typ <= 3 && n <= (size_t)len + 1 &&
		    memcmp(g_buf + off + 5, key, n) == 0)
		{
			*key_off = off;
			return 0;
		}

		off += 5u + len + 1u;
	}
	return -1;
}

/* Byte exact fallback: a key string always starts at (entry + 5) and always
 * begins with '/'. */
static int find_key_scan(const char *key, uint32_t *key_off)
{
	size_t n = strlen(key) + 1;
	uint32_t i;

	if (n < 2)
		return -1;

	for (i = KEY_AREA_START; i + n <= KEY_AREA_END; i++)
	{
		if (g_buf[i] != '/')
			continue;
		if (memcmp(g_buf + i, key, n) == 0 && i >= KEY_AREA_START + 5)
		{
			*key_off = i - 5;
			return 0;
		}
	}
	return -1;
}

static int find_value_walk(uint32_t key_off, xreg_entry *out)
{
	uint32_t want = key_off - KEYREF_BIAS;
	uint32_t off = VAL_AREA_START;

	if (key_off < KEYREF_BIAS)
		return -1;

	while (off + 9 <= VAL_AREA_END)
	{
		uint16_t flags = be16(g_buf + off);
		uint16_t kref  = be16(g_buf + off + 2);
		uint16_t id    = be16(g_buf + off + 4);
		uint16_t len   = be16(g_buf + off + 6);
		uint8_t  typ   = g_buf[off + 8];

		if (flags == 0xAABB && kref == 0xCCDD && id == 0xEE00)
			return -1;

		if (len > 0x7FF)
			return -1;

		if (off + 9u + len + 1u > VAL_AREA_END)
			return -1;

		if (kref == want && typ <= 2)
		{
			out->key_off    = key_off;
			out->value_off  = off + 9;
			out->value_len  = len;
			out->value_type = typ;
			return 0;
		}

		off += 9u + len + 1u;
	}
	return -1;
}

/* Fallback: look for the keyref field anywhere in the value area and sanity
 * check the entry that follows it. */
static int find_value_scan(uint32_t key_off, xreg_entry *out)
{
	uint32_t want = key_off - KEYREF_BIAS;
	uint32_t i;

	if (key_off < KEYREF_BIAS)
		return -1;

	for (i = VAL_AREA_START; i + 9 <= VAL_AREA_END; i += 2)
	{
		uint16_t len;
		uint8_t  typ;

		if (be16(g_buf + i + 2) != want)
			continue;

		len = be16(g_buf + i + 6);
		typ = g_buf[i + 8];
		if (typ > 2 || len > 0x7FF)
			continue;
		if (i + 9u + len + 1u > VAL_AREA_END)
			continue;

		out->key_off    = key_off;
		out->value_off  = i + 9;
		out->value_len  = len;
		out->value_type = typ;
		return 0;
	}
	return -1;
}

static int xreg_find(const char *key, xreg_entry *out)
{
	uint32_t key_off;

	if (!g_loaded)
		return -1;

	if (find_key_walk(key, &key_off) != 0 &&
	    find_key_scan(key, &key_off) != 0)
		return -1;

	if (find_value_walk(key_off, out) != 0 &&
	    find_value_scan(key_off, out) != 0)
		return -1;

	out->key_type = g_buf[key_off + 4];
	return 0;
}

/* -------------------------------------------------------------- public API -- */

static int read_u32(const char *key, uint32_t *out)
{
	xreg_entry e;
	uint32_t v = 0;
	uint32_t i;

	if (xreg_find(key, &e) != 0)
		return -1;
	/* 0 = boolean, 1 = integer.  Both are plain big endian numbers here. */
	if (e.value_type > 1)
		return -1;
	if (e.value_len < 1 || e.value_len > 4)
		return -1;

	for (i = 0; i < e.value_len; i++)
		v = (v << 8) | g_buf[e.value_off + i];

	*out = v;
	return 0;
}

static int write_u32(const char *key, uint32_t v)
{
	xreg_entry e;
	uint32_t i;

	if (xreg_find(key, &e) != 0)
		return -1;
	/* Only numeric entries are rewritten: overwriting a string in place
	 * could silently drop its terminator. */
	if (e.value_type > 1)
		return -1;
	if (e.value_len < 1 || e.value_len > 4)
		return -1;

	for (i = 0; i < e.value_len; i++)
		g_buf[e.value_off + e.value_len - 1 - i] = (uint8_t)(v >> (8 * i));

	return 0;
}

int xreg_read_state(xreg_state *out)
{
	memset(out, 0, sizeof(*out));

	if (!g_loaded)
		return -1;

	out->have_ps3 = (read_u32("/setting/system/region",       &out->ps3_region) == 0);
	out->have_dvd = (read_u32("/setting/bddvd/dvdRegionCode", &out->dvd_region) == 0);
	out->have_bd  = (read_u32("/setting/bddvd/bdRegionCode",  &out->bd_region)  == 0);
	out->have_tv  = (read_u32("/setting/bddvd/dvdTvSystem",   &out->tv_system)  == 0);

	/* The DVD code is the one setting this app exists for, so it decides
	 * whether the console is considered usable. */
	out->loaded = out->have_dvd;
	return out->loaded ? 0 : -1;
}

int xreg_apply(const xreg_state *st)
{
	int written = 0;

	if (!g_loaded)
		return -1;

	/* Region first: it is the value that carries the DVD/Blu-ray codes with
	 * it, so a partial write still leaves a coherent DVD setting. */
	if (write_u32("/setting/system/region", st->ps3_region) == 0)
		written++;

	if (write_u32("/setting/bddvd/dvdRegionCode", st->dvd_region) == 0)
		written++;

	if (write_u32("/setting/bddvd/bdRegionCode", st->bd_region) == 0)
		written++;

	if (write_u32("/setting/bddvd/dvdTvSystem", st->tv_system) == 0)
		written++;

	return written;
}

int xreg_save(void)
{
	if (!g_loaded)
		return -1;

	if (write_file(XREG_PATH, g_buf, XREG_SIZE) != 0)
		return -1;

	/* Keep the mirrored copy consistent; a console that finds a broken
	 * primary falls back to this one.  The directory may not exist. */
	{
		char dir[64];
		ustrlcpy(dir, "/dev_flash2/etc/backup", sizeof(dir));
		fs_mkdir_p(dir, 0777);
		write_file(XREG_BACKUP_PATH, g_buf, XREG_SIZE);
	}

	return 0;
}
