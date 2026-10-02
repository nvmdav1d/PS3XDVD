/* disc.c */
#include <stdio.h>
#include <string.h>

#include <lv2/sysfs.h>

#include "disc.h"
#include "storage.h"
#include "util.h"

#define DVD_MAX_SECTORS 0xA00000ULL   /* 10.5 GB, covers dual layer DVD */

static uint8_t g_sector[2048];

/* ------------------------------------------------------------------ helpers - */

static int path_exists(const char *a, const char *b)
{
	char p[512];
	ustrlcpy(p, a, sizeof(p));
	ustrlcat(p, b, sizeof(p));
	return fs_exists(p);
}

/* /dev_bdvd exposes the UDF bridge through ISO9660, so names may carry the
 * "version suffix".  Try both spellings. */
static int read_video_ts_ifo(void *buf, uint32_t len)
{
	static const char *bases[] = {
		"/dev_bdvd/VIDEO_TS/VIDEO_TS.IFO",
		"/dev_bdvd/VIDEO_TS/VIDEO_TS.IFO;1",
		"/dev_bdvd/VIDEO_TS;/VIDEO_TS.IFO",
		"/dev_bdvd/VIDEO_TS;/VIDEO_TS.IFO;1"
	};
	size_t i;

	for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++)
	{
		int fd = -1;
		int ret = sysFsOpen(bases[i], SYS_O_RDONLY, &fd, NULL, 0);
		if (ret != 0 || fd < 0)
			continue;
		ret = io_read_all(fd, buf, len);
		sysFsClose(fd);
		if (ret == 0)
			return 0;
	}
	return -1;
}

int disc_read_ifo(void *buf, uint32_t len)
{
	return read_video_ts_ifo(buf, len);
}

/* The VMG describes the volume, but a player resolves the region per title
 * set, so the first VTS is the authority that actually decides playback. The
 * two disagree often enough on real discs that reading only one of them is how
 * you end up confidently reporting the wrong answer. */
static int read_vts_ifo(void *buf, uint32_t len)
{
	static const char *bases[] = {
		"/dev_bdvd/VIDEO_TS/VTS_01_0.IFO",
		"/dev_bdvd/VIDEO_TS/VTS_01_0.IFO;1",
		"/dev_bdvd/VIDEO_TS;/VTS_01_0.IFO",
		"/dev_bdvd/VIDEO_TS;/VTS_01_0.IFO;1"
	};
	size_t i;

	for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++)
	{
		int fd = -1;
		int ret = sysFsOpen(bases[i], SYS_O_RDONLY, &fd, NULL, 0);
		if (ret != 0 || fd < 0)
			continue;
		ret = io_read_all(fd, buf, len);
		sysFsClose(fd);
		if (ret == 0)
			return 0;
	}
	return -1;
}

static int video_ts_present(void)
{
	return path_exists(BDVD_ROOT, "/VIDEO_TS") ||
	       path_exists(BDVD_ROOT, "/VIDEO_TS;1");
}

static void copy_provider(char *dst, const uint8_t *src, size_t n)
{
	size_t i;

	for (i = 0; i < n && i < 32; i++)
	{
		unsigned char c = src[i];
		dst[i] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
	}
	dst[32] = '\0';

	/* trim */
	{
		size_t len = strlen(dst);
		while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\t'))
			dst[--len] = '\0';
	}
}

void disc_region_string(uint8_t mask, uint32_t allowed, char *out, size_t out_size)
{
	static const char *names[8] = { "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8" };
	size_t o = 0;
	int i;

	if (out_size == 0)
		return;

	if (mask == 0x00)
	{
		/* Deliberately not called "region free": a zero VMG category is what
		 * RPC-2 and pressed discs report too, and calling it region free is
		 * how this app used to lie to the user. */
		ustrlcpy(out, "ALL (no restriction declared)", out_size);
		return;
	}
	if (mask == 0xFF)
	{
		ustrlcpy(out, "RCE / unknown", out_size);
		return;
	}

	out[0] = '\0';
	for (i = 0; i < 8; i++)
	{
		if (!(allowed & (1u << i)))
			continue;
		if (o + 4 >= out_size)
			break;
		if (o > 0)
			out[o++] = ',';
		out[o++] = names[i][0];
		out[o++] = names[i][1];
		out[o] = '\0';
	}
	if (o == 0)
		ustrlcpy(out, "none", out_size);
}

int disc_probe(disc_info *out)
{
	uint8_t ifo[256];

	memset(out, 0, sizeof(*out));
	ustrlcpy(out->provider, "Unknown", sizeof(out->provider));
	ustrlcpy(out->region_list, "-", sizeof(out->region_list));

	out->present = video_ts_present();
	if (!out->present)
		return -1;

	memset(ifo, 0, sizeof(ifo));
	if (read_video_ts_ifo(ifo, sizeof(ifo)) != 0)
	{
		out->ifo_err = 1;
		return -1;
	}

	if (memcmp(ifo, "DVDVIDEO-VMG", 12) != 0)
	{
		out->ifo_err = 2;   /* VIDEO_TS exists, but this is not a DVD-Video VMG */
		return -1;
	}

	out->ifo_err = 0;
	out->ifo_ok  = 1;

	/* VMGI_MAT layout, big endian. The 12 byte identifier occupies 0x00..0x0B,
	 * so the two sector counts start at 0x0C and 0x10:
	 *   0x00 "DVDVIDEO-VMG" (12)  0x0C vmg last sector   0x10 vmg ifo last sector
	 *   0x20 version             0x22 category (4 bytes) 0x26 number of volumes
	 *   0x28 volume number       0x2A side id            0x3E number of title sets
	 *   0x40 provider id (32)    0x80 end of VMGI_MAT    0x84 first play PGC
	 */
	out->vmg_last_sector = be32(ifo + 0x0C);
	out->ifo_last_sector = be32(ifo + 0x10);
	out->region_mask     = ifo[IFO_REGION_MASK_OFFSET];
	out->region_mask2    = ifo[IFO_REGION_MASK_OFFSET + 1];
	out->num_titles      = be16(ifo + 0x3E);
	out->mat_end         = be32(ifo + 0x80);
	out->fp_pgc          = be32(ifo + 0x84);

	copy_provider(out->provider, ifo + 0x40, 32);

	out->allowed_regions = 0;

	/* Cross check the volume IFO against the first title set. */
	out->have_vts = 0;
	out->vts_mask = 0;
	out->region_conflict = 0;
	{
		uint8_t vts[256];

		memset(vts, 0, sizeof(vts));
		if (read_vts_ifo(vts, sizeof(vts)) == 0 &&
		    memcmp(vts, "DVDVIDEO-VTS", 12) == 0)
		{
			out->have_vts  = 1;
			out->vts_mask = vts[IFO_REGION_MASK_OFFSET];
			if (out->vts_mask != out->region_mask)
				out->region_conflict = 1;
		}
	}

	if (out->region_mask != 0xFF)
	{
		int i;
		for (i = 0; i < 8; i++)
			if (!(out->region_mask & (1u << i)))
				out->allowed_regions |= (1u << i);
	}

	out->rce_suspected = (out->region_mask == 0xFF);

	/* Only claim "region free" when the byte is zero and nothing contradicts
	 * it. A single zero byte is not proof: pressed discs and RPC-2 titles
	 * both report 0x00 here while still refusing to play, and the previous
	 * build got this wrong in exactly that way. */
	out->region_free = (out->region_mask == 0x00) && !out->region_conflict;

	if (out->region_conflict)
		ustrlcpy(out->region_list, "unreliable", sizeof(out->region_list));
	else
		disc_region_string(out->region_mask, out->allowed_regions,
		                   out->region_list, sizeof(out->region_list));

	return 0;
}

void disc_make_basename(const disc_info *info, char *out, size_t out_size)
{
	static const char *names[8] = { "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8" };
	char stamp[24];
	char raw[160];
	char tag[16];
	size_t o = 0;
	int i;

	format_stamp(stamp, sizeof(stamp));

	if (info->region_free)
	{
		ustrlcpy(tag, "FREE", sizeof(tag));
	}
	else if (info->rce_suspected)
	{
		ustrlcpy(tag, "RCE", sizeof(tag));
	}
	else
	{
		tag[0] = '\0';
		for (i = 0; i < 8; i++)
		{
			if (!(info->allowed_regions & (1u << i)))
				continue;
			if (o + 3 >= sizeof(tag))
				break;
			tag[o++] = names[i][0];
			tag[o++] = names[i][1];
			tag[o] = '\0';
		}
		if (tag[0] == '\0')
			ustrlcpy(tag, "UNK", sizeof(tag));
	}

	snprintf(raw, sizeof(raw), "%s_%s_%s",
	         (info->ifo_ok && info->provider[0] != '\0') ? info->provider : "DVD",
	         tag, stamp);

	sanitize_filename(raw, out, out_size);
}

/* ------------------------------------------------------------- sector count - */

static int read_sector_ok(uint32_t handle, uint64_t lba)
{
	uint32_t got = 0;
	int ret = storage_read(handle, lba, 1, g_sector, &got);
	if (ret != 0)
		return 0;
	return (got == 1);
}

static int probe_by_search(uint32_t handle, uint64_t *out_last)
{
	uint64_t lo = 0, hi = DVD_MAX_SECTORS;

	if (!read_sector_ok(handle, 0))
		return -1;

	/* largest readable LBA in [0, DVD_MAX_SECTORS] */
	while (lo < hi)
	{
		uint64_t mid = lo + (hi - lo + 1) / 2;
		if (read_sector_ok(handle, mid))
			lo = mid;
		else
			hi = mid - 1;
	}

	*out_last = lo;
	return 0;
}

int disc_sector_count(uint64_t *out_sectors, char *how, size_t how_size)
{
	uint32_t handle = 0;
	uint32_t leadout = 0;
	storage_device_info info;
	uint64_t last = 0;
	int ret;

	if (out_sectors == NULL)
		return -1;
	*out_sectors = 0;
	if (how != NULL && how_size > 0)
		ustrlcpy(how, "unknown", how_size);

	ret = storage_open(BDVD_DEVICE_ID, &handle);
	if (ret != 0 || handle == 0)
	{
		if (how != NULL && how_size > 0)
			ustrlcpy(how, "raw access not permitted", how_size);
		return -1;
	}

	if (storage_read_leadout(handle, &leadout) == 0)
	{
		*out_sectors = leadout;
		if (how != NULL && how_size > 0)
			ustrlcpy(how, "ATAPI READ TOC", how_size);
		storage_close(handle);
		return 0;
	}

	if (storage_get_info(BDVD_DEVICE_ID, &info) == 0 &&
	    info.sector_size == 2048 &&
	    info.sector_count > 0x1000ULL && info.sector_count <= DVD_MAX_SECTORS)
	{
		*out_sectors = info.sector_count;
		if (how != NULL && how_size > 0)
			ustrlcpy(how, "device info", how_size);
		storage_close(handle);
		return 0;
	}

	if (probe_by_search(handle, &last) == 0)
	{
		*out_sectors = last + 1;
		if (how != NULL && how_size > 0)
			ustrlcpy(how, "read probe", how_size);
		storage_close(handle);
		return 0;
	}

	storage_close(handle);
	if (how != NULL && how_size > 0)
		ustrlcpy(how, "unknown", how_size);
	return -1;
}

/* -------------------------------------------------------------- folder size */

int disc_video_ts_size(uint64_t *out_bytes)
{
	s32 fd = -1;
	uint64_t read = 0;
	uint64_t total = 0;
	char path[512];

	if (out_bytes == NULL)
		return -1;
	*out_bytes = 0;

	if (sysFsOpendir("/dev_bdvd/VIDEO_TS", &fd) != 0)
		return -1;

	for (;;)
	{
		sysFSDirent ent;
		sysFSStat st;
		char nm[257];
		size_t l;

		memset(&ent, 0, sizeof(ent));
		if (sysFsReaddir(fd, &ent, &read) != 0 || read == 0)
			break;
		/* d_namlen is a u8 on GameOS, so it can never exceed nm; the clamp
		 * only matters if a future header widens the field. */
		if (ent.d_namlen == 0)
			continue;

		l = ent.d_namlen;
		if (l > sizeof(nm) - 1)
			l = sizeof(nm) - 1;
		memcpy(nm, ent.d_name, l);
		nm[l] = '\0';
		if (nm[0] == '.')
			continue;

		ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
		ustrlcat(path, nm, sizeof(path));
		if (sysFsStat(path, &st) == 0)
			total += st.st_size;
	}

	sysFsClosedir(fd);
	*out_bytes = total;
	return 0;
}
