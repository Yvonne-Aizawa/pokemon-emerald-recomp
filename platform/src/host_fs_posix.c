/*
 * platform/src/host_fs_posix.c
 *
 * POSIX implementation of host_fs.h.
 */

#define _POSIX_C_SOURCE 200809L

#include "platform/host_fs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool HostFs_MakeDir(const char *path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool HostFs_WriteFileAtomic(const char *path, const void *data, size_t size,
                            char *error, size_t errorSize)
{
    char tmpPath[4096 + 8];
    const char *bytes = data;
    size_t done = 0;
    int fd;

    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    fd = open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        snprintf(error, errorSize, "cannot write '%s': %s", tmpPath, strerror(errno));
        return false;
    }
    while (done < size)
    {
        ssize_t n = write(fd, bytes + done, size - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            snprintf(error, errorSize, "write to '%s' failed: %s", tmpPath, strerror(errno));
            close(fd);
            unlink(tmpPath);
            return false;
        }
        done += (size_t)n;
    }
    if (fsync(fd) != 0 || close(fd) != 0 || rename(tmpPath, path) != 0)
    {
        snprintf(error, errorSize, "could not commit '%s': %s", path, strerror(errno));
        unlink(tmpPath);
        return false;
    }
    return true;
}
