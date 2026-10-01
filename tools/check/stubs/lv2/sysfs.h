/* Stub of <lv2/sysfs.h> for the host compile check. */
#ifndef STUB_LV2_SYSFS_H
#define STUB_LV2_SYSFS_H

#include <ppu-types.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_O_RDONLY    000000
#define SYS_O_WRONLY    000001
#define SYS_O_RDWR      000002
#define SYS_O_ACCMODE   000003
#define SYS_O_CREAT     000100
#define SYS_O_EXCL      000200
#define SYS_O_TRUNC     001000
#define SYS_O_APPEND    002000
#define SYS_O_MSELF     010000

typedef struct _sys_fs_stat
{
	s32  st_mode;
	s32  st_uid;
	s32  st_gid;
	long st_atime;
	long st_mtime;
	long st_ctime;
	u64  st_size;
	u64  st_blksize;
} sysFSStat;

typedef struct _sys_fs_dirent
{
	u8   d_type;
	u8   d_namlen;
	char d_name[MAXPATHLEN + 1];
} sysFSDirent;

typedef struct _sys_fs_directory_entry
{
	sysFSStat   attribute;
	sysFSDirent entry_name;
} sysFSDirectoryEntry;

typedef void (*sysFSAioCallback)(void *user_data, int result, void *data);

typedef struct _sys_fs_aio
{
	s32  fd;
	u64  offset;
	u32  buffer_addr;
	u64  size;
	u64  usrdata;
} sysFSAio;

s32 sysFsOpen(const char *path, s32 oflags, s32 *fd, const void *arg, u64 argsize);
s32 sysFsClose(s32 fd);
s32 sysFsRead(s32 fd, void *ptr, u64 len, u64 *read);
s32 sysFsWrite(s32 fd, const void *ptr, u64 size, u64 *written);
s32 sysFsLseek(s32 fd, s64 offset, s32 whence, u64 *position);
s32 sysFsStat(const char *path, sysFSStat *stat);
s32 sysFsFstat(s32 fd, sysFSStat *stat);
s32 sysFsChmod(const char *path, s32 mode);
s32 sysFsMkdir(const char *path, s32 mode);
s32 sysFsRmdir(const char *path);
s32 sysFsUnlink(const char *path);
s32 sysFsAccess(const char *path, s32 amode);
s32 sysFsOpendir(const char *path, s32 *fd);
s32 sysFsClosedir(s32 fd);
s32 sysFsReaddir(s32 fd, sysFSDirent *entry, u64 *read);
s32 sysFsAioInit(const char *path);
s32 sysFsAioFinish(const char *path);
s32 sysFsAioRead(sysFSAio *aio, s32 *id, sysFSAioCallback cb);
s32 sysFsAioWrite(sysFSAio *aio, s32 *id, sysFSAioCallback cb);
s32 sysFsGetFreeSize(const char *path, u32 *blockSize, u64 *freeBlocks);
s32 sysFsGetDirectoryEntries(s32 fd, sysFSDirectoryEntry *entries, u32 entrySize, u32 *dataCount);
s32 sysFsSetIoBuffer(s32 fd, size_t bufferSizeLimit, s32 pageType, sys_mem_container_t container);
s32 sysFsSetDefaultContainer(sys_mem_container_t container, size_t totalLimit);
s32 sysFsSetIoBufferFromDefaultContainer(s32 fd, size_t bufferSizeLimit, s32 pageType);

#ifdef __cplusplus
}
#endif

#endif