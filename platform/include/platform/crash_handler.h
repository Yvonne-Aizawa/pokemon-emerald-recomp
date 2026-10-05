/*
 * platform/include/platform/crash_handler.h
 *
 * Crash reports: on a fatal signal (SIGSEGV, SIGBUS, SIGILL, SIGFPE,
 * SIGABRT) print what happened, the faulting address, a symbolized call
 * stack and the game's state, to stderr and to a report file -- then let the
 * signal take its normal course (core dump, debugger).
 *
 * Function names come from the executable's own symbol table, read at
 * install time, so static functions are named too and no debug build is
 * needed.
 */

#ifndef PLATFORM_CRASH_HANDLER_H
#define PLATFORM_CRASH_HANDLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRASH_REPORT_FILE "pkmemerald-crash.txt"

/* Install the handler; the report also goes to `reportPath`. */
void Crash_Install(const char *reportPath);

/* Extra state for the report. Both are read from inside the signal
 * handler, so they must stay valid and be safe to read at any time. */
void Crash_SetFrameCounter(uint32_t (*frameCounter)(void));
/* Report the function pointer stored at `slot` as `label: <symbol>`. */
void Crash_WatchFunctionPointer(const char *label, void *const *slot);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_CRASH_HANDLER_H */
