/*
 * platform/src/host_fs_win32.c
 *
 * Windows implementation of host_fs.h. Unlike POSIX rename, moving a file
 * over an existing one needs MoveFileEx(MOVEFILE_REPLACE_EXISTING); with
 * MOVEFILE_WRITE_THROUGH it returns only once the move is on disk.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/host_fs.h"

#include <stdio.h>
#include <string.h>

static void FormatError(char *error, size_t errorSize, const char *what, const char *path)
{
    DWORD code = GetLastError();
    char message[256];

    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0,
                       message, sizeof(message), NULL) == 0)
        snprintf(message, sizeof(message), "error %lu", (unsigned long)code);
    else
        message[strcspn(message, "\r\n")] = '\0';
    snprintf(error, errorSize, "%s '%s': %s", what, path, message);
}

bool HostFs_MakeDir(const char *path)
{
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool HostFs_WriteFileAtomic(const char *path, const void *data, size_t size,
                            char *error, size_t errorSize)
{
    char tmpPath[4096 + 8];
    HANDLE file;
    DWORD written;
    bool ok;

    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    file = CreateFileA(tmpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        FormatError(error, errorSize, "cannot write", tmpPath);
        return false;
    }
    ok = WriteFile(file, data, (DWORD)size, &written, NULL) && written == size;
    if (!ok)
        FormatError(error, errorSize, "write failed to", tmpPath);
    else if (!(ok = FlushFileBuffers(file)))
        FormatError(error, errorSize, "could not flush", tmpPath);
    CloseHandle(file);
    if (ok && !(ok = MoveFileExA(tmpPath, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)))
        FormatError(error, errorSize, "could not commit", path);
    if (!ok)
        DeleteFileA(tmpPath);
    return ok;
}
