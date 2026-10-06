/*
 * platform/include/platform/host_fs.h
 *
 * The file operations saving needs that differ between operating systems
 * (host_fs_posix.c, host_fs_win32.c).
 */

#ifndef PLATFORM_HOST_FS_H
#define PLATFORM_HOST_FS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create directory `path` (its parent must exist). True if it exists
 * afterwards; otherwise errno says why. */
bool HostFs_MakeDir(const char *path);

/* Replace `path` with `size` bytes from `data`, atomically: write
 * `path`.tmp, flush it to disk, then move it over `path`. An interrupted
 * write leaves the old file intact. On failure returns false, with a
 * description of the failed step in `error` (`errorSize` bytes). */
bool HostFs_WriteFileAtomic(const char *path, const void *data, size_t size,
                            char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HOST_FS_H */
