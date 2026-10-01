/* Stub of <sys/file.h> for the host compile check. */
#ifndef STUB_SYS_FILE_H
#define STUB_SYS_FILE_H

#include <ppu-types.h>
#include <lv2/sysfs.h>

#ifdef __cplusplus
extern "C" {
#endif

s32 sysLv2FsOpen(const char *path, s32 oflags, s32 *fd, u32 mode, const void *arg, u64 argsize);
s32 sysLv2FsClose(s32 fd);
s32 sysLv2FsRead(s32 fd, void *ptr, u64 len, u64 *read);
s32 sysLv2FsWrite(s32 fd, const void *ptr, u64 len, u64 *written);
s32 sysLv2FsLseek(s32 fd, s64 offset, s32 whence, u64 *position);
s32 sysLv2FsStat(const char *path, sysFSStat *stat);
s32 sysLv2FsMkdir(const char *path, s32 mode);
s32 sysLv2FsRename(const char *path, const char *newpath);
s32 sysLv2FsUnlink(const char *path);
s32 sysLv2FsFsync(s32 fd);
s32 sysLv2FsTruncate(s32 fd, u64 size);
s32 sysLv2FsMount(const char *deviceName, const char *deviceFileSystem,
                  const char *devicePath, int writeProt);

#ifdef __cplusplus
}
#endif

#endif