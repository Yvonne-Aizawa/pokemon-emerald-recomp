/*
 * platform/include/platform/crash_handler.h
 *
 * Crash reports: on a fatal signal (SIGSEGV, SIGBUS, SIGILL, SIGFPE,
 * SIGABRT) print what happened, the faulting address, a symbolized call
 * stack and the game's state, to stderr and to a report file -- then let the
 * signal take its normal course (core dump, debugger).
 *
 * Each crash gets its own report file, named after the local date and time
 * of the crash (2026-10-06_19-10-32_pkmemerald-crash.txt), so earlier
 * reports are kept.
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

/* Folder for the reports, inside the save directory. */
#define CRASH_REPORT_DIR "crashes"
/* Report file names: YYYY-MM-DD_HH-MM-SS followed by this. */
#define CRASH_REPORT_SUFFIX "_pkmemerald-crash.txt"

/* Install the handler; each report also goes to a new, timestamped file in
 * `reportDir`, which must already exist. */
void Crash_Install(const char *reportDir);

/* Extra state for the report. Both are read from inside the signal
 * handler, so they must stay valid and be safe to read at any time. */
void Crash_SetFrameCounter(uint32_t (*frameCounter)(void));
/* Report the function pointer stored at `slot` as `label: <symbol>`. */
void Crash_WatchFunctionPointer(const char *label, void *const *slot);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_CRASH_HANDLER_H */
