/*
 * platform/src/crash_handler_win32.c
 *
 * Windows implementation of crash_handler.h, with the same report as
 * crash_handler_linux.c.
 *
 * Crashes arrive as unhandled SEH exceptions (SetUnhandledExceptionFilter),
 * abort() as SIGABRT. Function names come from the executable's own COFF
 * symbol table (MinGW keeps it unless the binary is stripped), read at
 * install time. The call stack is walked through saved frame pointers from
 * the crash context: Windows builds use -fno-omit-frame-pointer, since
 * neither libgcc's unwinder (which can't cross the exception dispatcher) nor
 * DbgHelp (which can't read MinGW's DWARF) can walk it otherwise.
 *
 * After the report the exception continues to Windows' own handling
 * (Windows Error Reporting, or a debugger).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform/crash_handler.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 64
#define MAX_WATCHES 8

struct Symbol
{
    uintptr_t addr;
    size_t size;
    const char *name;
};

struct Watch
{
    const char *label;
    void *const *slot;
};

static struct Symbol *sSymbols;
static size_t sSymbolCount;
static char *sSymbolNames;
static char sReportDir[1024];
static char sReportPath[1024 + 64];
static uint32_t (*sFrameCounter)(void);
static struct Watch sWatches[MAX_WATCHES];
static int sWatchCount;

/* --------------------------------------------------------------------- */
/* Symbols (read at install time, not in the handler)                    */
/* --------------------------------------------------------------------- */

static int CompareSymbols(const void *a, const void *b)
{
    uintptr_t x = ((const struct Symbol *)a)->addr, y = ((const struct Symbol *)b)->addr;
    return (x > y) - (x < y);
}

static bool ReadAt(FILE *f, void *buf, size_t size, long offset)
{
    return fseek(f, offset, SEEK_SET) == 0 && fread(buf, 1, size, f) == size;
}

static void LoadSymbols(void)
{
    char exePath[MAX_PATH];
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER file;
    IMAGE_SECTION_HEADER *sections = NULL;
    IMAGE_SYMBOL *syms = NULL;
    DWORD peOffset, stringsSize;
    long sectionsOffset;
    FILE *f;
    size_t i;

    if (GetModuleFileNameA(NULL, exePath, sizeof(exePath)) == 0 || (f = fopen(exePath, "rb")) == NULL)
        return;
    if (!ReadAt(f, &dos, sizeof(dos), 0) || dos.e_magic != IMAGE_DOS_SIGNATURE
     || !ReadAt(f, &peOffset, sizeof(peOffset), dos.e_lfanew) || peOffset != IMAGE_NT_SIGNATURE
     || !ReadAt(f, &file, sizeof(file), dos.e_lfanew + 4)
     || file.PointerToSymbolTable == 0 || file.NumberOfSymbols == 0)
        goto done;

    sectionsOffset = dos.e_lfanew + 4 + (long)sizeof(file) + file.SizeOfOptionalHeader;
    sections = malloc(file.NumberOfSections * sizeof(*sections));
    syms = malloc(file.NumberOfSymbols * sizeof(*syms));
    if (sections == NULL || syms == NULL
     || !ReadAt(f, sections, file.NumberOfSections * sizeof(*sections), sectionsOffset)
     || !ReadAt(f, syms, file.NumberOfSymbols * sizeof(*syms), (long)file.PointerToSymbolTable)
     || !ReadAt(f, &stringsSize, sizeof(stringsSize),
                (long)(file.PointerToSymbolTable + file.NumberOfSymbols * sizeof(*syms)))
     || stringsSize < 4)
        goto done;

    /* The string table (long names), then room for the short names, each
     * copied out with its terminator (and without the i386 `_` prefix). */
    sSymbolNames = malloc(stringsSize + file.NumberOfSymbols * 9);
    sSymbols = malloc(file.NumberOfSymbols * sizeof(struct Symbol));
    if (sSymbolNames == NULL || sSymbols == NULL
     || !ReadAt(f, sSymbolNames, stringsSize,
                (long)(file.PointerToSymbolTable + file.NumberOfSymbols * sizeof(*syms))))
        goto done;
    {
        char *shortNames = sSymbolNames + stringsSize;

        for (i = 0; i < file.NumberOfSymbols; i += 1 + syms[i].NumberOfAuxSymbols)
        {
            const IMAGE_SYMBOL *sym = &syms[i];
            const char *name;

            if (sym->SectionNumber <= 0 || sym->SectionNumber > file.NumberOfSections
             || (sym->StorageClass != IMAGE_SYM_CLASS_EXTERNAL && sym->StorageClass != IMAGE_SYM_CLASS_STATIC))
                continue;
            if (sym->N.Name.Short == 0)
            {
                if (sym->N.Name.Long >= stringsSize)
                    continue;
                name = sSymbolNames + sym->N.Name.Long;
            }
            else
            {
                memcpy(shortNames, sym->N.ShortName, 8);
                shortNames[8] = '\0';
                name = shortNames;
                shortNames += 9;
            }
            /* Section names (.text, .data$x) and local labels. */
            if (name[0] == '.' || (sym->StorageClass == IMAGE_SYM_CLASS_STATIC && sym->NumberOfAuxSymbols != 0))
                continue;
            if (name[0] == '_')
                name++;
            sSymbols[sSymbolCount].addr = base + sections[sym->SectionNumber - 1].VirtualAddress + sym->Value;
            sSymbols[sSymbolCount].size = 0;
            sSymbols[sSymbolCount].name = name;
            sSymbolCount++;
        }
    }
    qsort(sSymbols, sSymbolCount, sizeof(struct Symbol), CompareSymbols);
    /* COFF symbols have no size: each one runs to the next. */
    for (i = 0; i + 1 < sSymbolCount; i++)
        sSymbols[i].size = sSymbols[i + 1].addr - sSymbols[i].addr;

done:
    free(syms);
    free(sections);
    fclose(f);
}

/* The symbol containing `addr` (or the closest one below it), or NULL. */
static const struct Symbol *FindSymbol(uintptr_t addr)
{
    size_t lo = 0, hi = sSymbolCount;

    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        if (sSymbols[mid].addr <= addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return NULL;
    /* The last symbol and gaps: only trust nearby. */
    if (sSymbols[lo - 1].size != 0 ? addr >= sSymbols[lo - 1].addr + sSymbols[lo - 1].size
                                   : addr - sSymbols[lo - 1].addr > 0x1000)
        return NULL;
    return &sSymbols[lo - 1];
}

/* --------------------------------------------------------------------- */
/* Report formatting (no allocation: the heap may be what broke)          */
/* --------------------------------------------------------------------- */

static char sReport[32 * 1024];
static size_t sReportLen;

static void Put(const char *s)
{
    while (*s != '\0' && sReportLen < sizeof(sReport) - 1)
        sReport[sReportLen++] = *s++;
}

static void PutHex(uintptr_t value)
{
    char buf[2 + 2 * sizeof(uintptr_t) + 1];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < (int)(2 * sizeof(uintptr_t)); i++)
        buf[2 + i] = "0123456789abcdef"[(value >> (4 * (2 * sizeof(uintptr_t) - 1 - i))) & 0xF];
    buf[2 + 2 * sizeof(uintptr_t)] = '\0';
    Put(buf);
}

static void PutDec(uint32_t value)
{
    char buf[11];
    int i = 10;

    buf[i] = '\0';
    do
    {
        buf[--i] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    Put(buf + i);
}

/* "0x08049abc  FunctionName+0x12" */
static void PutAddress(uintptr_t addr)
{
    const struct Symbol *sym = FindSymbol(addr);

    PutHex(addr);
    Put("  ");
    if (sym == NULL)
    {
        Put("?");
        return;
    }
    Put(sym->name);
    if (addr != sym->addr)
    {
        Put("+");
        PutHex(addr - sym->addr);
    }
}

static const char *ExceptionName(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:      return "EXCEPTION_ACCESS_VIOLATION (invalid memory access)";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "EXCEPTION_ILLEGAL_INSTRUCTION (illegal instruction)";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "EXCEPTION_INT_DIVIDE_BY_ZERO (division by zero)";
    case EXCEPTION_STACK_OVERFLOW:        return "EXCEPTION_STACK_OVERFLOW (stack overflow)";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT (misaligned access)";
    case EXCEPTION_IN_PAGE_ERROR:         return "EXCEPTION_IN_PAGE_ERROR (page could not be read)";
    case EXCEPTION_PRIV_INSTRUCTION:      return "EXCEPTION_PRIV_INSTRUCTION (privileged instruction)";
    default:                              return "fatal exception";
    }
}

/* This thread's stack: the TIB's StackLimit and StackBase. Read directly
 * from fs: (32-bit x86) or gs: (x86-64), as NtCurrentTeb() does: MinGW's
 * NtCurrentTeb() trips GCC's -Warray-bounds. */
static void StackBounds(uintptr_t *low, uintptr_t *high)
{
#if defined(__i386__)
    __asm__("movl %%fs:8, %0" : "=r"(*low));
    __asm__("movl %%fs:4, %0" : "=r"(*high));
#elif defined(__x86_64__)
    __asm__("movq %%gs:16, %0" : "=r"(*low));
    __asm__("movq %%gs:8, %0" : "=r"(*high));
#else
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();

    *low = (uintptr_t)tib->StackLimit;
    *high = (uintptr_t)tib->StackBase;
#endif
}

/* Frame pointer chain: [fp] = caller's fp, [fp + one word] = return
 * address (ebp on 32-bit x86, rbp on x86-64). */
static void PutCallStack(uintptr_t pc, uintptr_t fp)
{
    uintptr_t stackLow, stackHigh;
    int i;

    StackBounds(&stackLow, &stackHigh);

    Put("call stack:\n");
    for (i = 0; i < MAX_FRAMES && pc != 0; i++)
    {
        Put("  #");
        PutDec((uint32_t)i);
        Put("  ");
        PutAddress(pc);
        Put("\n");
        if (fp < stackLow || fp + 2 * sizeof(uintptr_t) > stackHigh || (fp & (sizeof(uintptr_t) - 1)) != 0)
            break;
        pc = ((const uintptr_t *)fp)[1];
        if (((const uintptr_t *)fp)[0] <= fp)
            break;
        fp = ((const uintptr_t *)fp)[0];
    }
}

/* sReportPath = sReportDir\YYYY-MM-DD_HH-MM-SS<suffix>, in local time. */
static void BuildReportPath(void)
{
    SYSTEMTIME now;

    GetLocalTime(&now);
    snprintf(sReportPath, sizeof(sReportPath), "%s\\%04u-%02u-%02u_%02u-%02u-%02u" CRASH_REPORT_SUFFIX,
             sReportDir, (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay,
             (unsigned)now.wHour, (unsigned)now.wMinute, (unsigned)now.wSecond);
}

static void WriteReport(void)
{
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE file;
    DWORD written;

    if (err != NULL && err != INVALID_HANDLE_VALUE)
        WriteFile(err, sReport, (DWORD)sReportLen, &written, NULL);
    BuildReportPath();
    file = CreateFileA(sReportPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteFile(file, sReport, (DWORD)sReportLen, &written, NULL);
        CloseHandle(file);
        if (err != NULL && err != INVALID_HANDLE_VALUE)
        {
            WriteFile(err, "crash report written to ", 24, &written, NULL);
            WriteFile(err, sReportPath, (DWORD)strlen(sReportPath), &written, NULL);
            WriteFile(err, "\n", 1, &written, NULL);
        }
    }
}

static void Report(const char *what, bool hasFaultAddress, uintptr_t faultAddress, uintptr_t pc, uintptr_t fp)
{
    static volatile LONG sInHandler;
    int i;

    if (InterlockedExchange(&sInHandler, 1) != 0)
        return;
    sReportLen = 0;

    Put("\n=== pkmemerald crash ===\n");
    Put("exception: ");
    Put(what);
    Put("\n");
    if (hasFaultAddress)
    {
        Put("fault address: ");
        PutAddress(faultAddress);
        Put("\n");
    }
    Put("crashed at: ");
    PutAddress(pc);
    Put("\n");
    if (sFrameCounter != NULL)
    {
        Put("frame: ");
        PutDec(sFrameCounter());
        Put("\n");
    }
    for (i = 0; i < sWatchCount; i++)
    {
        Put(sWatches[i].label);
        Put(": ");
        PutAddress((uintptr_t)*sWatches[i].slot);
        Put("\n");
    }
    PutCallStack(pc, fp);
    WriteReport();
}

static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    const CONTEXT *context = info->ContextRecord;
    bool hasFaultAddress = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
                        && record->NumberParameters >= 2;

    Report(ExceptionName(record->ExceptionCode), hasFaultAddress,
           hasFaultAddress ? (uintptr_t)record->ExceptionInformation[1] : 0,
#if defined(__i386__)
           (uintptr_t)context->Eip, (uintptr_t)context->Ebp
#else
           (uintptr_t)context->Rip, (uintptr_t)context->Rbp
#endif
           );
    return EXCEPTION_CONTINUE_SEARCH;
}

static void OnAbort(int sig)
{
    (void)sig;
    Report("SIGABRT (abort)", false, 0,
           (uintptr_t)__builtin_return_address(0), (uintptr_t)__builtin_frame_address(0));
    signal(SIGABRT, SIG_DFL);
    raise(SIGABRT);
}

/* --------------------------------------------------------------------- */
/* Public API                                                            */
/* --------------------------------------------------------------------- */

void Crash_Install(const char *reportDir)
{
    strncpy(sReportDir, reportDir, sizeof(sReportDir) - 1);
    LoadSymbols();
    SetUnhandledExceptionFilter(OnUnhandledException);
    signal(SIGABRT, OnAbort);
}

void Crash_SetFrameCounter(uint32_t (*frameCounter)(void))
{
    sFrameCounter = frameCounter;
}

void Crash_WatchFunctionPointer(const char *label, void *const *slot)
{
    if (sWatchCount < MAX_WATCHES)
    {
        sWatches[sWatchCount].label = label;
        sWatches[sWatchCount].slot = slot;
        sWatchCount++;
    }
}
