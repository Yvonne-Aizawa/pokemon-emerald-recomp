/*
 * platform/src/console_posix.c
 *
 * POSIX implementation of console.h: stdout/stderr already go wherever the
 * shell sends them.
 */

#include "platform/console.h"

void Console_Setup(bool showConsole, const char *logDir)
{
    (void)showConsole;
    (void)logDir;
}
