/* rip.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lv2/sysfs.h>
#include <sys/file.h>

#include "disc.h"
#include "rip.h"
#include "storage.h"
#include "util.h"

#define RAW_CHUNK_SECTORS 32                      /* 64 KB */
#define COPY_CHUNK        (64 * 1024)

/* The bdvd bridge will not accept an arbitrarily large read: a 64 KB request
 * for VIDEO_TS.BUP comes back empty on real hardware even though the member
 * stats as 24576 bytes and opens cleanly. Reads therefore start at one sector
 * and only grow while they keep returning whole, which keeps a 6 GB copy fast
 * without ever issuing a request the bridge refuses. */
#define BDVD_READ_MIN     2048u
#define BDVD_SLEEP_EVERY  (512 * 1024)
#define READ_RETRIES      6

static uint8_t g_raw[RAW_CHUNK_SECTORS * 2048];
static uint8_t g_copy[COPY_CHUNK];

/* ------------------------------------------------------------------ patch -- */

int rip_region_free_ifo(const char *ifo_path)
{
	uint8_t hdr[256];
	s32     fd = -1;
	uint64_t pos = 0;
	uint8_t zero = 0;

	if (sysFsOpen(ifo_path, SYS_O_RDWR, &fd, NULL, 0) != 0 || fd < 0)
		return -1;

	if (io_read_all(fd, hdr, sizeof(hdr)) != 0 ||
	    (memcmp(hdr, "DVDVIDEO-VMG", 12) != 0 &&
	     memcmp(hdr, "DVDVIDEO-VTS", 12) != 0))
	{
		sysFsClose(fd);
		return -2;
	}

	if (sysFsLseek(fd, (s64)IFO_REGION_MASK_OFFSET, SEEK_SET, &pos) != 0)
	{
		sysFsClose(fd);
		return -3;
	}

	if (io_write_all(fd, &zero, 1) != 0)
	{
		sysFsClose(fd);
		return -4;
	}

	sysLv2FsFsync(fd);
	sysFsClose(fd);
	return 0;
}

/* Clearing only VIDEO_TS.IFO leaves every title set still region locked, which
 * is why patched copies so often still refuse to play. Walk the copied files
 * and clear the category byte in the VTS IFOs as well. */
static int rip_patch_title_sets(const char *out_dir)
{
	char path[700];
	char name[32];
	int  i;
	int  patched = 0;

	for (i = 1; i <= 99; i++)
	{
		int rc;

		snprintf(name, sizeof(name), "VTS_%02u_0.IFO", (unsigned)i);
		ustrlcpy(path, out_dir, sizeof(path));
		ustrlcat(path, "/", sizeof(path));
		ustrlcat(path, name, sizeof(path));

		if (!fs_exists(path))
		{
			snprintf(name, sizeof(name), "VTS_%02u_0.IFO;1", (unsigned)i);
			ustrlcpy(path, out_dir, sizeof(path));
			ustrlcat(path, "/", sizeof(path));
			ustrlcat(path, name, sizeof(path));
			if (!fs_exists(path))
				break;                 /* title sets are contiguous */
		}

		rc = rip_region_free_ifo(path);
		if (rc == 0)
			patched++;
		else if (rc == -1)
			break;                     /* not present after all */
	}

	return patched;
}

/* --------------------------------------------------------- split writers --- */

typedef struct
{
	char     stem[512];      /* the path as requested by the caller */
	int      fd;
	int      split;
	uint64_t part_left;
	int      parts;
} out_writer;

static int writer_next_part(out_writer *w);

static int writer_open(out_writer *w, const char *path)
{
	memset(w, 0, sizeof(*w));
	ustrlcpy(w->stem, path, sizeof(w->stem));
	w->split     = fs_is_fat(path) ? 1 : 0;   /* FAT32 needs 4 GB splitting */
	w->fd        = -1;
	w->parts     = 0;
	w->part_left = RIP_FAT_SPLIT;

	if (!w->split)
	{
		w->fd = fs_create(path, 0777);
		return (w->fd < 0) ? -1 : 0;
	}

	return writer_next_part(w);
}

static void writer_close(out_writer *w)
{
	if (w->fd >= 0)
	{
		sysLv2FsFsync(w->fd);
		sysFsClose(w->fd);
		w->fd = -1;
	}
}

static int writer_next_part(out_writer *w)
{
	char p[600];

	if (!w->split)
		return 0;                      /* a single file just keeps going */

	writer_close(w);
	w->parts++;
	snprintf(p, sizeof(p), "%s.%d", w->stem, w->parts - 1);
	w->fd = fs_create(p, 0777);
	if (w->fd < 0)
		return -1;
	w->part_left = RIP_FAT_SPLIT;
	return 0;
}

static int writer_write(out_writer *w, const void *buf, size_t len)
{
	const uint8_t *p = (const uint8_t *)buf;

	if (w->fd < 0)
		return -1;

	while (len > 0)
	{
		size_t n = len;

		if (w->split)
		{
			if (w->part_left == 0)
			{
				if (writer_next_part(w) != 0)
					return -1;
			}
			if ((uint64_t)n > w->part_left)
				n = (size_t)w->part_left;
		}

		if (io_write_all(w->fd, p, n) != 0)
			return -1;

		p   += n;
		len -= n;
		if (w->split)
			w->part_left -= n;
	}
	return 0;
}

static int report(rip_progress_fn cb, void *ctx, rip_status *st, uint64_t done)
{
	st->done = done;
	if (cb != NULL)
		return cb(ctx, st);
	return 0;
}

/* ---------------------------------------------------------------- raw ISO -- */

int rip_raw_iso(const char *out_path, uint64_t sectors, int patch_region,
                rip_progress_fn cb, void *ctx, rip_status *st)
{
	uint32_t handle = 0;
	out_writer w;
	uint64_t done  = 0;
	uint64_t ifo_file_off = 0;
	int      have_ifo_off = 0;
	int      ret = 0;
	int      cancelled = 0;

	memset(st, 0, sizeof(*st));
	ustrlcpy(st->out_path, out_path, sizeof(st->out_path));
	st->total = sectors * 2048ULL;
	ustrlcpy(st->message, "Opening drive...", sizeof(st->message));

	if (storage_open(BDVD_DEVICE_ID, &handle) != 0 || handle == 0)
	{
		ustrlcpy(st->message,
		         "Drive access denied (1:1 dump needs CFW or HEN)",
		         sizeof(st->message));
		st->failed = 1;
		st->finished = 1;
		return -1;
	}

	if (writer_open(&w, out_path) != 0)
	{
		storage_close(handle);
		ustrlcpy(st->message, "Cannot create the output file",
		         sizeof(st->message));
		st->failed = 1;
		st->finished = 1;
		return -1;
	}

	ustrlcpy(st->message, "Dumping sectors", sizeof(st->message));

	while (done < sectors)
	{
		uint64_t left = sectors - done;
		uint32_t want = (left > RAW_CHUNK_SECTORS)
		              ? RAW_CHUNK_SECTORS : (uint32_t)left;
		uint32_t got = 0;
		int attempt;
		int r = 0;

		for (attempt = 0; attempt < READ_RETRIES; attempt++)
		{
			got = 0;
			r = storage_read(handle, done / 2048ULL, want, g_raw, &got);
			if (r == 0 && got == want)
				break;

			/* The drive occasionally drops out mid-disc; a close/open
			 * cycle is the documented recovery. */
			usleep(100000);
			storage_close(handle);
			handle = 0;
			if (storage_open(BDVD_DEVICE_ID, &handle) != 0 || handle == 0)
			{
				handle = 0;
				break;
			}
			if (attempt == 1)
				ustrlcpy(st->message, "Drive hiccup, retrying", sizeof(st->message));
		}

		if (handle == 0 || got != want)
		{
			/* This used to say "disc may be damaged", which sends people off
			 * to clean a disc that is fine. A 1:1 dump needs the physical
			 * drive, and on a console that refuses or drops raw access this
			 * is where it shows up, long after the folder copy would have
			 * worked. */
			ustrlcpy(st->message,
			         "Raw drive read failed. This is the 1:1 ISO mode, which "
			         "needs direct access to the Blu-ray drive.\n\n"
			         "Your console most likely will not give it. Switch Dump "
			         "mode to VIDEO_TS folder, which only reads files and "
			         "does not need it.",
			         sizeof(st->message));
			st->failed = 1;
			ret = -1;
			break;
		}

		/* Remember where VIDEO_TS.IFO landed so the region byte can be
		 * cleared without a second pass over 4.7 GB. */
		if (patch_region && !have_ifo_off)
		{
			uint32_t s;
			for (s = 0; s + 12u <= want * 2048u; s += 2048u)
			{
				if (memcmp(g_raw + s, "DVDVIDEO-VMG", 12) == 0)
				{
					ifo_file_off = done * 2048ULL + s;
					have_ifo_off = 1;
					break;
				}
			}
		}

		if (writer_write(&w, g_raw, (size_t)want * 2048u) != 0)
		{
			ustrlcpy(st->message, "Write error, is the drive full?",
			         sizeof(st->message));
			st->failed = 1;
			ret = -1;
			break;
		}

		done += want;

		if (report(cb, ctx, st, done * 2048ULL))
		{
			cancelled = 1;
			break;
		}

		usleep(500);   /* keep the drive and the UI happy */
	}

	writer_close(&w);
	if (handle != 0)
		storage_close(handle);

	if (cancelled)
	{
		ustrlcpy(st->message, "Cancelled", sizeof(st->message));
		st->finished = 1;
		return 1;
	}

	if (!st->failed && patch_region)
	{
		if (have_ifo_off && !fs_is_fat(out_path))
		{
			s32 fd = -1;
			if (sysFsOpen(out_path, SYS_O_RDWR, &fd, NULL, 0) == 0 && fd >= 0)
			{
				uint64_t pos = 0;
				uint8_t zero = 0;
				sysFsLseek(fd, (s64)(ifo_file_off + IFO_REGION_MASK_OFFSET),
				           SEEK_SET, &pos);
				if (io_write_all(fd, &zero, 1) == 0)
					st->patched = 1;
				sysLv2FsFsync(fd);
				sysFsClose(fd);
			}
		}

		if (!st->patched)
			ustrlcpy(st->message,
			         "Done (region byte not patched - use the copy mode instead)",
			         sizeof(st->message));
	}

	if (!st->failed)
	{
		st->ok = 1;
		if (strncmp(st->message, "Dumping", 6) == 0)
			ustrlcpy(st->message, st->patched ? "Done, region mask cleared"
			                                  : "Done", sizeof(st->message));
	}

	st->finished = 1;
	return ret;
}

/* ------------------------------------------------------------ VIDEO_TS copy - */

typedef struct
{
	char     name[256];      /* name as reported by the directory listing */
	char     clean[256];     /* name without the ISO9660 version suffix  */
	uint64_t size;
	int      sized;          /* 0 when st_size was unusable, copy until EOF */
} copy_entry;

static void strip_version(char *dst, const char *src, size_t n)
{
	size_t len = ustrlcpy(dst, src, n);
	if (len > 2 && strcmp(dst + len - 2, ";1") == 0)
		dst[len - 2] = '\0';
}

/* Builds the path of a member, retrying with the ISO9660 version suffix. The
 * bdvd bridge does not always stat or open a name the way the directory listing
 * spells it, and getting this wrong means silently copying an incomplete disc.
 */
static int open_video_ts_member(const char *nm, s32 *fd)
{
	char path[600];
	int  ret;

	ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
	ustrlcat(path, nm, sizeof(path));
	ret = sysFsOpen(path, SYS_O_RDONLY, fd, NULL, 0);
	if (ret == 0 && *fd >= 0)
		return 0;

	if (strlen(nm) > 2 && strcmp(nm + strlen(nm) - 2, ";1") == 0)
	{
		ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
		ustrlcat(path, nm, sizeof(path));
		path[strlen(path) - 2] = '\0';
		ret = sysFsOpen(path, SYS_O_RDONLY, fd, NULL, 0);
		if (ret == 0 && *fd >= 0)
			return 0;
	} else
	{
		ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
		ustrlcat(path, nm, sizeof(path));
		ustrlcat(path, ";1", sizeof(path));
		ret = sysFsOpen(path, SYS_O_RDONLY, fd, NULL, 0);
		if (ret == 0 && *fd >= 0)
			return 0;
	}

	return -1;
}

/* Opens the other spelling of a member name, for the case where the listing
 * gives one form, open accepts it, but the read comes back empty. */
static int open_video_ts_member_alt(const char *nm, s32 *fd)
{
	char path[600];
	size_t len = strlen(nm);
	int  ret;

	if (len > 2 && strcmp(nm + len - 2, ";1") == 0)
	{
		ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
		ustrlcat(path, nm, sizeof(path));
		path[len - 2] = '\0';
	}
	else
	{
		ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
		ustrlcat(path, nm, sizeof(path));
		ustrlcat(path, ";1", sizeof(path));
	}

	ret = sysFsOpen(path, SYS_O_RDONLY, fd, NULL, 0);
	return (ret == 0 && *fd >= 0) ? 0 : -1;
}
static int stat_video_ts_member(const char *nm, uint64_t *size)
{
	char path[600];
	sysFSStat st;

	ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
	ustrlcat(path, nm, sizeof(path));
	if (sysFsStat(path, &st) == 0 && st.st_size > 0)
	{
		*size = st.st_size;
		return 0;
	}

	if (strlen(nm) > 2 && strcmp(nm + strlen(nm) - 2, ";1") == 0)
	{
		ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
		ustrlcat(path, nm, sizeof(path));
		path[strlen(path) - 2] = '\0';
		if (sysFsStat(path, &st) == 0 && st.st_size > 0)
		{
			*size = st.st_size;
			return 0;
		}
		return -1;
	}

	ustrlcpy(path, "/dev_bdvd/VIDEO_TS/", sizeof(path));
	ustrlcat(path, nm, sizeof(path));
	ustrlcat(path, ";1", sizeof(path));
	if (sysFsStat(path, &st) == 0 && st.st_size > 0)
	{
		*size = st.st_size;
		return 0;
	}

	return -1;
}

static int enumerate(const char *dir, copy_entry **out, uint32_t *out_count,
                     uint64_t *out_total, uint32_t *out_unsized)
{
	s32 fd = -1;
	uint64_t read = 0;
	copy_entry *list = NULL;
	uint32_t count = 0, cap = 0;
	uint64_t total = 0;
	uint32_t unsized = 0;

	(void)dir;
	if (sysFsOpendir("/dev_bdvd/VIDEO_TS", &fd) != 0)
		return -1;

	for (;;)
	{
		sysFSDirent ent;
		char nm[257];
		size_t l;

		memset(&ent, 0, sizeof(ent));
		if (sysFsReaddir(fd, &ent, &read) != 0 || read == 0)
			break;
		if (ent.d_namlen == 0)
			continue;

		l = ent.d_namlen;
		if (l > sizeof(nm) - 1)
			l = sizeof(nm) - 1;
		memcpy(nm, ent.d_name, l);
		nm[l] = '\0';
		if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0')))
			continue;

		if (count == cap)
		{
			copy_entry *grown;
			cap = cap ? cap * 2 : 16;
			grown = (copy_entry *)realloc(list, (size_t)cap * sizeof(copy_entry));
			if (grown == NULL)
				break;
			list = grown;
		}

		ustrlcpy(list[count].name, nm, sizeof(list[count].name));
		strip_version(list[count].clean, nm, sizeof(list[count].clean));
		list[count].size = 0;

		/* A member whose size could not be stat'ed is still kept: the old
		 * code dropped it here, which is how a 6 GB disc turned into a 48 KB
		 * "dump" with no video in it. The copy then reads it until EOF. */
		if (stat_video_ts_member(nm, &list[count].size) == 0)
		{
			list[count].sized = 1;
			total += list[count].size;
		}
		else
		{
			list[count].sized = 0;
			unsized++;
		}
		count++;
	}

	sysFsClosedir(fd);
	*out = list;
	*out_count = count;
	*out_total = total;
	if (out_unsized != NULL)
		*out_unsized = unsized;
	return 0;
}

int rip_copy_video_ts(const char *out_dir, int patch_region,
                      rip_progress_fn cb, void *ctx, rip_status *st)
{
	copy_entry *list = NULL;
	uint32_t count = 0;
uint64_t total = 0;
	uint64_t done = 0;
	uint64_t done_in_file = 0;
	uint32_t unsized = 0;
	uint32_t i;
	int cancelled = 0;

	memset(st, 0, sizeof(*st));
	ustrlcpy(st->out_path, out_dir, sizeof(st->out_path));
	ustrlcpy(st->message, "Reading disc directory", sizeof(st->message));

	if (enumerate("/dev_bdvd/VIDEO_TS", &list, &count, &total, &unsized) != 0 ||
	    count == 0)
	{
		ustrlcpy(st->message, "VIDEO_TS not found on the disc", sizeof(st->message));
		st->failed = 1;
		st->finished = 1;
		free(list);
		return -1;
	}

	/* When a member had no usable size the total is only a guess, so it is
	 * grown as real bytes arrive. Otherwise the progress bar would sit at a
	 * few percent for the whole copy. */
	st->total = total;
	fs_mkdir_p(out_dir, 0777);

	for (i = 0; i < count && !cancelled; i++)
	{
		char dst[700];
		s32  sfd = -1;
		int  failed = 0;

		ustrlcpy(dst, out_dir, sizeof(dst));
		ustrlcat(dst, "/", sizeof(dst));
		ustrlcat(dst, list[i].clean, sizeof(dst));

		if (open_video_ts_member(list[i].name, &sfd) != 0)
		{
			/* No longer skipped in silence: a member that cannot be opened
			 * means the copy is incomplete and must say so. */
			snprintf(st->message, sizeof(st->message),
			         "Cannot open %.100s on the disc", list[i].clean);
			st->failed = 1;
			st->finished = 1;
			free(list);
			return -1;
		}

		{
			int dfd = fs_create(dst, 0777);
			size_t chunk = BDVD_READ_MIN;    /* see the note above the loop */
			size_t since_sleep = 0;

			if (dfd < 0)
			{
				sysFsClose(sfd);
				snprintf(st->message, sizeof(st->message),
				         "Cannot create %.100s", list[i].clean);
				st->failed = 1;
				st->finished = 1;
				free(list);
				return -1;
			}

			/* Read until EOF instead of trusting st_size.
			 *
			 * Observed on real hardware: VIDEO_TS.BUP lists fine, stats as
			 * 24576 bytes and opens, but a single 64 KB read of it returns
			 * nothing at all, so the old loop reported "copied 0 of 24576".
			 * The bdvd bridge does not accept arbitrarily large reads, so
			 * this starts at one sector and grows only while reads keep
			 * coming back whole. Growing adaptively matters: sleeping per
			 * 2 KB read would turn a 6 GB copy into an hours long job. */
			for (;;)
			{
				uint64_t got = 0;
				size_t  want;

				if (list[i].sized && list[i].size > done_in_file)
				{
					uint64_t left = list[i].size - done_in_file;
					want = (left < (uint64_t)chunk) ? (size_t)left : chunk;
				}
				else
				{
					want = chunk;
				}

				if (want == 0)
					break;

				if (sysFsRead(sfd, g_copy, (u64)want, &got) != 0 || got == 0)
				{
					/* End of file, or this read size is too big for the
					 * bridge. A member that had not delivered a single byte
					 * yet is retried under the other name spelling before
					 * being called a failure. */
					if (got == 0 && done_in_file == 0)
					{
						uint64_t save_pos = 0;

						if (sysFsLseek(sfd, 0, SEEK_CUR, &save_pos) == 0)
						{
							sysFsClose(sfd);
							sfd = -1;
							if (open_video_ts_member_alt(list[i].name, &sfd) == 0)
							{
								sysFsClose(dfd);
								dfd = fs_create(dst, 0777);
								if (dfd < 0)
									break;
								chunk = BDVD_READ_MIN;
								since_sleep = 0;
								continue;
							}
						}
snprintf(st->message, sizeof(st->message),
					         "Read %.80s: no data from the drive",
					         list[i].clean);
						st->failed = 1;
						st->finished = 1;
						free(list);
						return -1;
					}
					break;                     /* end of file */
				}

				if (got < (uint64_t)want)
				{
					/* A short read is normal here, so keep the size that
					 * worked instead of asking for more. */
					chunk = (size_t)got;
					if (chunk < BDVD_READ_MIN)
						chunk = BDVD_READ_MIN;
				}
				else if (chunk < COPY_CHUNK)
				{
					chunk *= 2;
					if (chunk > COPY_CHUNK)
						chunk = COPY_CHUNK;
				}

				if (io_write_all(dfd, g_copy, (size_t)got) != 0)
				{
					failed = 1;
					break;
				}

				done_in_file += got;
				done += got;
				since_sleep += (size_t)got;

				if (report(cb, ctx, st, done))
				{
					cancelled = 1;
					break;
				}

				if (since_sleep >= BDVD_SLEEP_EVERY)
				{
					since_sleep = 0;
					usleep(1000);
				}
			}

			sysLv2FsFsync(dfd);
			sysFsClose(dfd);
			sysFsClose(sfd);
		}

		if (list[i].sized && done_in_file != list[i].size)
		{
			/* The size was known and the bytes do not match it, so the
			 * member on the disc is not what stat claimed. */
			snprintf(st->message, sizeof(st->message),
			         "%.100s copied %llu of %llu bytes",
			         list[i].clean,
			         (unsigned long long)done_in_file,
			         (unsigned long long)list[i].size);
			st->failed = 1;
			st->finished = 1;
			free(list);
			return -1;
		}

		/* With no trustworthy upfront total, extend it by what was really
		 * written so the progress display stays meaningful. */
		if (unsized > 0)
			st->total += done_in_file;

		done_in_file = 0;

		if (failed)
		{
			snprintf(st->message, sizeof(st->message), "Error copying %.100s",
			         list[i].clean);
			st->failed = 1;
			st->finished = 1;
			free(list);
			return -1;
		}
	}

	free(list);

	if (cancelled)
	{
		ustrlcpy(st->message, "Cancelled", sizeof(st->message));
		st->finished = 1;
		return 1;
	}

	if (patch_region)
	{
		char ifo[700];
		int  vts = 0;

		ustrlcpy(ifo, out_dir, sizeof(ifo));
		ustrlcat(ifo, "/VIDEO_TS.IFO", sizeof(ifo));
		if (rip_region_free_ifo(ifo) == 0)
			st->patched = 1;

		vts = rip_patch_title_sets(out_dir);
		if (vts > 0)
		{
			snprintf(st->message, sizeof(st->message),
			         "Done, %s%d title set mask%s cleared",
			         st->patched ? "VMG + " : "", vts, (vts == 1) ? "" : "s");
			st->finished = 1;
			st->ok = 1;
			return 0;
		}
	}

	st->ok = 1;
	ustrlcpy(st->message, st->patched ? "Done, region mask cleared" : "Done",
	         sizeof(st->message));
	st->finished = 1;
	return 0;
}
