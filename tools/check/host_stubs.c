/* Support code for the host compile check and unit tests.
 *
 * Never linked into the real app. Besides the syscall recorder it provides a
 * tiny in-memory filesystem so the filesystem-facing logic (xRegistry load /
 * patch / save, path helpers) can be executed and asserted on a host machine.
 */
#include <ppu-lv2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/file.h>
#include <sys/time.h>
#include <unistd.h>

long p1;

/* ------------------------------------------------------- syscall recorder -- */

long ps3check_record(long num, long a1, long a2, long a3,
                     long a4, long a5, long a6, long a7)
{
	(void)a1; (void)a2; (void)a3; (void)a4;
	(void)a5; (void)a6; (void)a7; (void)num;
	p1 = 0;
	return 0;
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	(void)tz;
	if (tv == NULL)
		return -1;
	tv->tv_sec  = (long)time(NULL);
	tv->tv_usec = 0;
	return 0;
}

int usleep(unsigned int usec)
{
	(void)usec;
	return 0;
}

/* ------------------------------------------------------- fake filesystem --- */

#define FAKE_MAX_FILES 32
#define FAKE_MAX_DIRS  16
#define FAKE_MAX_DATA  (1024 * 1024)

typedef struct
{
	char    name[256];
	uint8_t data[FAKE_MAX_DATA];
	size_t  len;
	int     used;
	int     hidden;   /* stat fails, but readdir/open still see it */
} fake_file;

typedef struct
{
	char name[256];
	int  used;
} fake_dir;

static fake_file g_files[FAKE_MAX_FILES];
static fake_dir  g_dirs[FAKE_MAX_DIRS];
static uint64_t  g_free_bytes = 4ULL * 1024 * 1024 * 1024;

#define FAKE_MAX_OPEN_DIRS 8
#define FAKE_MAX_ENTRIES   64

/* open directory handles, so readdir can walk a listing one entry at a time */
static struct
{
	int  used;
	int  next;
	int  count;
	char entries[FAKE_MAX_ENTRIES][64];
} g_dh[FAKE_MAX_OPEN_DIRS];

static fake_file *file_find(const char *path)
{
	int i;
	for (i = 0; i < FAKE_MAX_FILES; i++)
		if (g_files[i].used && strcmp(g_files[i].name, path) == 0)
			return &g_files[i];
	return NULL;
}

static fake_file *file_alloc(const char *path)
{
	int i;
	fake_file *f = file_find(path);
	if (f != NULL)
		return f;
	for (i = 0; i < FAKE_MAX_FILES; i++)
	{
		if (!g_files[i].used)
		{
			memset(&g_files[i], 0, sizeof(g_files[i]));
			snprintf(g_files[i].name, sizeof(g_files[i].name), "%s", path);
			g_files[i].used = 1;
			return &g_files[i];
		}
	}
	return NULL;
}

/* open handle table: negative fds are errors, >= 0 index into it */
#define FAKE_MAX_FD 32
static struct { fake_file *f; size_t pos; int writable; int ro; } g_fd[FAKE_MAX_FD];

void fakefs_reset(void);
void fakefs_put(const char *path, const void *data, size_t len);
int  fakefs_get(const char *path, void *out, size_t len);
int  fakefs_exists(const char *path);
uint64_t fakefs_free(void);
void fakefs_set_free(uint64_t bytes);
void fakefs_hide(const char *path);

/* Makes stat() fail for one member while the directory listing and open() keep
 * working. This is what the bdvd bridge does with the large VOB files, and it
 * is what turned a 6 GB disc into a 48 KB "dump". */
void fakefs_hide(const char *path)
{
	fake_file *f = file_find(path);
	if (f != NULL)
		f->hidden = 1;
}

void fakefs_reset(void)
{
	memset(g_files, 0, sizeof(g_files));
	memset(g_dirs, 0, sizeof(g_dirs));
	memset(g_fd, 0, sizeof(g_fd));
	memset(g_dh, 0, sizeof(g_dh));
	g_free_bytes = 4ULL * 1024 * 1024 * 1024;
}

void fakefs_put(const char *path, const void *data, size_t len)
{
	fake_file *f = file_alloc(path);
	if (f == NULL || len > FAKE_MAX_DATA)
		return;
	memcpy(f->data, data, len);
	f->len = len;
}

int fakefs_get(const char *path, void *out, size_t len)
{
	fake_file *f = file_find(path);
	if (f == NULL)
		return -1;
	if (len > f->len)
		return -1;
	memcpy(out, f->data, len);
	return 0;
}

int fakefs_exists(const char *path)
{
	int i;
	if (file_find(path) != NULL)
		return 1;
	for (i = 0; i < FAKE_MAX_DIRS; i++)
		if (g_dirs[i].used && strcmp(g_dirs[i].name, path) == 0)
			return 1;
	return 0;
}

uint64_t fakefs_free(void) { return g_free_bytes; }
void fakefs_set_free(uint64_t bytes) { g_free_bytes = bytes; }

/* ------------------------------------------------------------ sysFs shims -- */

s32 sysFsOpen(const char *path, s32 oflags, s32 *fd, const void *arg, u64 argsize)
{
	int want_write = (oflags & SYS_O_ACCMODE) != SYS_O_RDONLY;
	int creat      = (oflags & SYS_O_CREAT) != 0;
	int trunc      = (oflags & SYS_O_TRUNC) != 0;
	fake_file *f;
	int i;

	(void)arg; (void)argsize;

	f = file_find(path);
	if (f == NULL)
	{
		if (!creat)
			return -2;                       /* ENOENT */
		f = file_alloc(path);
		if (f == NULL)
			return -4;
	}

	if (trunc)
		f->len = 0;

	for (i = 0; i < FAKE_MAX_FD; i++)
	{
		if (g_fd[i].f == NULL)
		{
			g_fd[i].f       = f;
			g_fd[i].pos     = 0;
			g_fd[i].writable = want_write;
			g_fd[i].ro      = !want_write;
			*fd = i;
			return 0;
		}
	}
	return -4;
}

s32 sysFsClose(s32 fd)
{
	if (fd < 0 || fd >= FAKE_MAX_FD || g_fd[fd].f == NULL)
		return -2;
	g_fd[fd].f = NULL;
	return 0;
}

s32 sysFsRead(s32 fd, void *ptr, u64 len, u64 *read)
{
	fake_file *f;
	size_t n;

	if (fd < 0 || fd >= FAKE_MAX_FD || g_fd[fd].f == NULL)
		return -2;
	f = g_fd[fd].f;

	if (g_fd[fd].pos >= f->len)
	{
		*read = 0;
		return 0;
	}
	n = (size_t)len;
	if (g_fd[fd].pos + n > f->len)
		n = f->len - g_fd[fd].pos;

	memcpy(ptr, f->data + g_fd[fd].pos, n);
	g_fd[fd].pos += n;
	*read = n;
	return 0;
}

s32 sysFsWrite(s32 fd, const void *ptr, u64 size, u64 *written)
{
	fake_file *f;
	size_t room;

	if (fd < 0 || fd >= FAKE_MAX_FD || g_fd[fd].f == NULL)
		return -2;
	if (!g_fd[fd].writable)
		return -5;                           /* EACCES */
	f = g_fd[fd].f;

	if (g_fd[fd].pos >= FAKE_MAX_DATA)
	{
		*written = 0;
		return 0;
	}
	room = FAKE_MAX_DATA - g_fd[fd].pos;
	if ((size_t)size > room)
		size = room;

	memcpy(f->data + g_fd[fd].pos, ptr, (size_t)size);
	g_fd[fd].pos += (size_t)size;
	if (g_fd[fd].pos > f->len)
		f->len = g_fd[fd].pos;
	*written = size;
	return 0;
}

s32 sysFsLseek(s32 fd, s64 offset, s32 whence, u64 *position)
{
	long np;

	if (fd < 0 || fd >= FAKE_MAX_FD || g_fd[fd].f == NULL)
		return -2;

	np = (long)(whence == SEEK_END ? (long)g_fd[fd].f->len
	                                : g_fd[fd].pos) + (long)offset;
	if (np < 0)
		np = 0;
	g_fd[fd].pos = (size_t)np;
	*position = (u64)np;
	return 0;
}

s32 sysFsStat(const char *path, sysFSStat *stat)
{
	fake_file *f = file_find(path);
	size_t plen = strlen(path);
	int i;

	/* A hidden file behaves as if stat cannot see it, while the directory
	 * listing and open still can. */
	if (f != NULL && f->hidden)
		return -2;

	if (f == NULL)
	{
		for (i = 0; i < FAKE_MAX_DIRS; i++)
			if (g_dirs[i].used && strcmp(g_dirs[i].name, path) == 0)
			{
				memset(stat, 0, sizeof(*stat));
				stat->st_mode = 0040000;
				return 0;
			}

		/* Anything that is a prefix of a known file is a directory. */
		for (i = 0; i < FAKE_MAX_FILES; i++)
		{
			if (!g_files[i].used)
				continue;
			if (strncmp(g_files[i].name, path, plen) == 0 &&
			    g_files[i].name[plen] == '/')
			{
				memset(stat, 0, sizeof(*stat));
				stat->st_mode = 0040000;
				return 0;
			}
		}
		return -2;
	}
	memset(stat, 0, sizeof(*stat));
	stat->st_mode = 0100000;
	stat->st_size = f->len;
	return 0;
}

s32 sysFsChmod(const char *path, s32 mode) { (void)path; (void)mode; return 0; }

s32 sysFsMkdir(const char *path, s32 mode)
{
	int i;
	(void)mode;
	for (i = 0; i < FAKE_MAX_DIRS; i++)
	{
		if (g_dirs[i].used && strcmp(g_dirs[i].name, path) == 0)
			return -7;                        /* EEXIST */
		if (!g_dirs[i].used)
		{
			snprintf(g_dirs[i].name, sizeof(g_dirs[i].name), "%s", path);
			g_dirs[i].used = 1;
			return 0;
		}
	}
	return -4;
}

s32 sysFsUnlink(const char *path)
{
	fake_file *f = file_find(path);
	if (f == NULL)
		return -2;
	f->used = 0;
	return 0;
}

s32 sysFsGetFreeSize(const char *path, u32 *blockSize, u64 *freeBlocks)
{
	(void)path;
	*blockSize  = 512;
	*freeBlocks = g_free_bytes / 512;
	return 0;
}

/* ------------------------------------------------------------- directory API */

s32 sysFsOpendir(const char *path, s32 *fd)
{
	size_t plen = strlen(path);
	int slot, i, n = 0;

	for (slot = 0; slot < FAKE_MAX_OPEN_DIRS; slot++)
		if (!g_dh[slot].used)
			break;
	if (slot == FAKE_MAX_OPEN_DIRS)
		return -4;

	memset(&g_dh[slot], 0, sizeof(g_dh[slot]));
	g_dh[slot].used = 1;

	/* Leave room for the "." and ".." entries added below. */
	for (i = 0; i < FAKE_MAX_FILES && n < FAKE_MAX_ENTRIES - 2; i++)
	{
		const char *base;
		size_t nlen;

		if (!g_files[i].used)
			continue;
		if (strncmp(g_files[i].name, path, plen) != 0 ||
		    g_files[i].name[plen] != '/')
			continue;

		base = g_files[i].name + plen + 1;
		if (strchr(base, '/') != NULL)
			continue;                       /* not a direct child */

		nlen = strlen(base);
		if (nlen >= sizeof(g_dh[slot].entries[0]))
			continue;
		snprintf(g_dh[slot].entries[n++], sizeof(g_dh[slot].entries[0]),
		         "%s", base);
	}

	/* "." and "..", like the real thing */
	snprintf(g_dh[slot].entries[n],     sizeof(g_dh[slot].entries[0]), ".");
	snprintf(g_dh[slot].entries[n + 1], sizeof(g_dh[slot].entries[0]), "..");
	g_dh[slot].count = n + 2;

	*fd = slot;
	return 0;
}

s32 sysFsReaddir(s32 fd, sysFSDirent *entry, u64 *read)
{
	const char *name;
	size_t nlen;

	if (fd < 0 || fd >= FAKE_MAX_OPEN_DIRS || !g_dh[fd].used)
		return -2;

	if (g_dh[fd].next >= g_dh[fd].count)
	{
		*read = 0;
		return 0;
	}

	name = g_dh[fd].entries[g_dh[fd].next++];
	nlen = strlen(name);

	memset(entry, 0, sizeof(*entry));
	entry->d_type   = (nlen == 1 || (nlen == 2 && name[1] == '.')) ? 4 : 8;
	entry->d_namlen = (u8)nlen;
	memcpy(entry->d_name, name, nlen);

	*read = sizeof(sysFSDirent);
	return 0;
}

s32 sysFsClosedir(s32 fd)
{
	if (fd < 0 || fd >= FAKE_MAX_OPEN_DIRS)
		return -2;
	g_dh[fd].used = 0;
	return 0;
}

s32 sysLv2FsFsync(s32 fd) { (void)fd; return 0; }