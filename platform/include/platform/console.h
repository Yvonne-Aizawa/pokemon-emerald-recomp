/*
 * platform/include/platform/console.h
 *
 * Where the game's messages (stdout/stderr) go.
 *
 * On Linux they go wherever the shell sends them, and this does nothing
 * (console_posix.c). The Windows executable is a windowed program, so no
 * console window opens beside the game (console_win32.c): its messages go
 * where its output was redirected (a terminal pipe, the tests); if it has
 * nowhere to go (started by double-click), to a log file; and with
 * `--console`, to a console -- the one it was started from, or a new one.
 */

#ifndef PLATFORM_CONSOLE_H
#define PLATFORM_CONSOLE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONSOLE_LOG_FILE "pkmemerald-log.txt"

/* Call once, early. `logDir` is where CONSOLE_LOG_FILE goes if needed. */
void Console_Setup(bool showConsole, const char *logDir);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_CONSOLE_H */
