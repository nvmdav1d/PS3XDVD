/* util.c */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <lv2/sysfs.h>
#include <sys/file.h>

#include "util.h"

/* --------------------------------------------------------------- strings ---- */

size_t ustrlcpy(char *dst, const char *src, size_t size)
{
	size_t i = 0;
	if (size == 0)
		return 0;
	if (src == NULL)
	{
		dst[0] = '\0';
		return 0;
	}
	while (src[i] != '\0' && i + 1 < size)
	{
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
	return i;
}

size_t ustrlcat(char *dst, const char *src, size_t size)
{
	size_t d = 0;
	if (size == 0)
		return 0;
	while (d + 1 < size && dst[d] != '\0')
		d++;
	return d + ustrlcpy(dst + d, src, size - d);
}

static char lower_ascii(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

int ustrcasecmp_ascii(const char *a, const char *b)
{
	while (*a != '\0' && *b != '\0')
	{
		char ca = lower_ascii(*a);
		char cb = lower_ascii(*b);
		if (ca != cb)
			return (int)((unsigned char)ca) - (int)((unsigned char)cb);
		a++;
		b++;
	}
	return (int)((unsigned char)lower_ascii(*a)) - (int)((unsigned char)lower_ascii(*b));
}

void sanitize_filename(const char *in, char *out, size_t out_size)
{
	static const char bad[] = "<|>:*?\"\\/";
	size_t o = 0;

	if (out_size == 0)
		return;

	if (in == NULL)
	{
		out[0] = '\0';
		return;
	}

	for (; *in != '\0' && o + 1 < out_size; in++)
	{
		unsigned char c = (unsigned char)*in;

		if (c < 0x20 || strchr(bad, (char)c) != NULL)
		{
			out[o++] = '_';
			continue;
		}
		/* collapse runs of spaces, they confuse the PS3 shell */
		if (c == ' ' && o > 0 && out[o - 1] == ' ')
			continue;
		out[o++] = (char)c;
	}
	out[o] = '\0';

	/* trim trailing spaces and dots, which FAT dislikes */
	while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '.'))
		out[--o] = '\0';
	if (o == 0)
		ustrlcpy(out, "DISC", out_size);
}

/* ------------------------------------------------------------ formatting ---- */

void format_size(uint64_t bytes, char *out, size_t out_size)
{
	static const char *unit[] = { "B", "KB", "MB", "GB", "TB" };
	double v = (double)bytes;
	int u = 0;

	while (v >= 1024.0 && u < 4)
	{
		v /= 1024.0;
		u++;
	}

	if (u == 0)
		snprintf(out, out_size, "%d %s", (int)v, unit[u]);
	else
		snprintf(out, out_size, "%.2f %s", v, unit[u]);
}

void format_duration(uint32_t seconds, char *out, size_t out_size)
{
	if (seconds >= 3600)
		snprintf(out, out_size, "%02u:%02u:%02u",
		         seconds / 3600, (seconds / 60) % 60, seconds % 60);
	else
		snprintf(out, out_size, "%02u:%02u", (seconds / 60) % 60, seconds % 60);
}

void format_stamp(char *out, size_t out_size)
{
	time_t now = time(NULL);
	struct tm *tmv;

	if (out_size == 0)
		return;

	tmv = gmtime(&now);
	if (now == (time_t)-1 || tmv == NULL)
	{
		ustrlcpy(out, "00000000-000000", out_size);
		return;
	}

	snprintf(out, out_size, "%04d%02d%02d-%02d%02d%02d",
	         tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
	         tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
}

/* ------------------------------------------------------------- filesystem --- */

static int is_slash(char c)
{
	return c == '/' || c == '\\';
}

/* Creates every component of `path`. sysFsMkdir() returns EEXIST for existing
 * directories, which is exactly what we want here, so results are ignored. */
int fs_mkdir_p(const char *path, unsigned mode)
{
	char buf[512];
	size_t i, len;

	if (path == NULL || path[0] == '\0')
		return -1;

	ustrlcpy(buf, path, sizeof(buf));
	len = strlen(buf);
	while (len > 1 && is_slash(buf[len - 1]))
		buf[--len] = '\0';

	/* buf[0] is '/' for absolute paths, so start the walk at index 1 */
	for (i = 1; i < len; i++)
	{
		if (!is_slash(buf[i]))
			continue;
		buf[i] = '\0';
		sysFsMkdir(buf, mode);
		buf[i] = path[i];
	}
	sysFsMkdir(buf, mode);

	return 0;
}

int fs_free_space(const char *path, uint64_t *out_bytes)
{
	uint32_t block_size = 0;
	uint64_t blocks = 0;
	int ret;

	if (out_bytes == NULL)
		return -1;

	ret = sysFsGetFreeSize(path, &block_size, &blocks);
	if (ret != 0)
	{
		*out_bytes = 0;
		return ret;
	}

	*out_bytes = (uint64_t)block_size * blocks;
	return 0;
}

int fs_create(const char *path, unsigned mode)
{
	int fd = -1;
	int ret;

	ret = sysFsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, NULL, 0);
	if (ret != 0 || fd < 0)
		return ret != 0 ? ret : -1;

	/* The internal HDD refuses to hand freshly created files to the OS
	 * unless the permissions are widened (IRISMAN / webMAN both do this). */
	sysFsChmod(path, mode);

	return fd;
}

int io_read_all(int fd, void *buf, size_t len)
{
	uint8_t *p = (uint8_t *)buf;
	size_t done = 0;

	while (done < len)
	{
		uint64_t got = 0;
		int ret = sysFsRead(fd, p + done, (u64)(len - done), &got);
		if (ret != 0)
			return ret;
		if (got == 0)
			return -1; /* early EOF */
		done += (size_t)got;
	}
	return 0;
}

int io_write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	size_t done = 0;

	while (done < len)
	{
		uint64_t put = 0;
		int ret = sysFsWrite(fd, p + done, (u64)(len - done), &put);
		if (ret != 0)
			return ret;
		if (put == 0)
			return -1; /* no progress, bail out instead of spinning */
		done += (size_t)put;
	}
	return 0;
}

int fs_exists(const char *path)
{
	sysFSStat st;
	return sysFsStat(path, &st) == 0;
}

int fs_is_fat(const char *path)
{
	/* PS3's internal filesystem has no 4 GB limit; USB sticks are FAT32 and
	 * need splitting at 0xFFFF0000 bytes. */
	return (path != NULL && strncmp(path, "/dev_usb", 8) == 0) ? 1 : 0;
}
