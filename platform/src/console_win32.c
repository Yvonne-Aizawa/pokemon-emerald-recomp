/*
 * platform/src/console_win32.c
 *
 * Windows implementation of console.h. The executable is built as a
 * windowed (GUI subsystem) program, so Windows opens no console for it.
 *
 * stdout/stderr that already lead somewhere (redirected to a file or pipe,
 * as the tests do) are kept. Otherwise:
 *   --console        Attach to the console of the program that started us
 *                    (cmd, PowerShell), or open a new console window.
 *   no --console     Write them to CONSOLE_LOG_FILE in the save directory,
 *                    replaced on each start (a double-click start).
 *
 * Both the C runtime's stdout/stderr and the Win32 standard handles are
 * pointed at the destination: the crash handler writes to the latter.
 * Output is unbuffered so nothing is lost if the game crashes.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/console.h"

#include <io.h>
#include <stdio.h>

static bool HasOutput(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

    return out != NULL && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN;
}

static void UseConsole(void)
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole())
        return;
    SetConsoleOutputCP(CP_UTF8);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(_fileno(stdout)));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(_fileno(stderr)));
    /* A console attached after the prompt was printed: start on a new line. */
    fputc('\n', stdout);
}

/* In a windowed program stdout/stderr start with no file descriptor behind
 * them, so they are reopened rather than redirected with _dup2. */
static void UseLogFile(const char *logDir)
{
    char path[MAX_PATH];

    if (snprintf(path, sizeof(path), "%s\\%s", logDir, CONSOLE_LOG_FILE) >= (int)sizeof(path))
        return;
    if (freopen(path, "w", stdout) == NULL)
        return;
    if (freopen("NUL", "w", stderr) != NULL)
        _dup2(_fileno(stdout), _fileno(stderr));
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)_get_osfhandle(_fileno(stdout)));
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(_fileno(stderr)));
}

void Console_Setup(bool showConsole, const char *logDir)
{
    if (HasOutput())
        ;
    else if (showConsole)
        UseConsole();
    else if (logDir != NULL)
        UseLogFile(logDir);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}
