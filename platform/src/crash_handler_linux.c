/*
 * platform/src/crash_handler_linux.c
 *
 * Linux implementation of platform/crash_handler.h.
 *
 * Everything the signal handler needs is prepared in Crash_Install: the
 * symbol table is read from /proc/self/exe and sorted, backtrace() is called
 * once (its first call may load libgcc), and an alternate signal stack is set
 * up so stack overflows are reported too. The handler itself only uses
 * async-signal-safe calls (write, open, close, clock_gettime, sigaction,
 * raise) plus backtrace() and its own formatting into a static buffer.
 *
 * localtime() isn't async-signal-safe, so the report's file name is local
 * time computed from clock_gettime() plus the UTC offset taken at install.
 */

#define _GNU_SOURCE

#include "platform/crash_handler.h"

#include <elf.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

#if UINTPTR_MAX == 0xFFFFFFFFu
typedef Elf32_Ehdr ElfEhdr;
typedef Elf32_Shdr ElfShdr;
typedef Elf32_Sym ElfSym;
#define ELF_ST_TYPE ELF32_ST_TYPE
#else
typedef Elf64_Ehdr ElfEhdr;
typedef Elf64_Shdr ElfShdr;
typedef Elf64_Sym ElfSym;
#define ELF_ST_TYPE ELF64_ST_TYPE
#endif

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
static long sUtcOffset;
static uint32_t (*sFrameCounter)(void);
static struct Watch sWatches[MAX_WATCHES];
static int sWatchCount;
static char sAltStack[64 * 1024];

/* --------------------------------------------------------------------- */
/* Symbols (read at install time, not in the handler)                    */
/* --------------------------------------------------------------------- */

static bool ReadAt(int fd, void *buf, size_t size, off_t offset)
{
    return pread(fd, buf, size, offset) == (ssize_t)size;
}

static int CompareSymbols(const void *a, const void *b)
{
    uintptr_t x = ((const struct Symbol *)a)->addr, y = ((const struct Symbol *)b)->addr;
    return (x > y) - (x < y);
}

static void LoadSymbols(void)
{
    int fd = open("/proc/self/exe", O_RDONLY);
    ElfEhdr ehdr;
    ElfShdr *sections = NULL;
    ElfSym *syms = NULL;
    size_t i, count;

    if (fd < 0)
        return;
    if (!ReadAt(fd, &ehdr, sizeof(ehdr), 0) || memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0
     || ehdr.e_shentsize != sizeof(ElfShdr))
        goto done;
    sections = malloc((size_t)ehdr.e_shnum * sizeof(ElfShdr));
    if (sections == NULL || !ReadAt(fd, sections, (size_t)ehdr.e_shnum * sizeof(ElfShdr), ehdr.e_shoff))
        goto done;

    for (i = 0; i < ehdr.e_shnum; i++)
    {
        const ElfShdr *symtab = &sections[i];
        const ElfShdr *strtab;

        if (symtab->sh_type != SHT_SYMTAB || symtab->sh_link >= ehdr.e_shnum)
            continue;
        strtab = &sections[symtab->sh_link];
        count = symtab->sh_size / sizeof(ElfSym);
        syms = malloc(symtab->sh_size);
        sSymbolNames = malloc(strtab->sh_size);
        sSymbols = malloc(count * sizeof(struct Symbol));
        if (syms == NULL || sSymbolNames == NULL || sSymbols == NULL
         || !ReadAt(fd, syms, symtab->sh_size, symtab->sh_offset)
         || !ReadAt(fd, sSymbolNames, strtab->sh_size, strtab->sh_offset))
            goto done;

        for (size_t j = 0; j < count; j++)
        {
            int type = ELF_ST_TYPE(syms[j].st_info);

            if ((type != STT_FUNC && type != STT_OBJECT) || syms[j].st_value == 0
             || syms[j].st_name >= strtab->sh_size)
                continue;
            sSymbols[sSymbolCount].addr = syms[j].st_value;
            sSymbols[sSymbolCount].size = syms[j].st_size;
            sSymbols[sSymbolCount].name = sSymbolNames + syms[j].st_name;
            sSymbolCount++;
        }
        qsort(sSymbols, sSymbolCount, sizeof(struct Symbol), CompareSymbols);
        break;
    }

done:
    free(syms);
    free(sections);
    close(fd);
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
    /* Unsized symbols (assembly labels) and gaps: only trust nearby. */
    if (sSymbols[lo - 1].size != 0 ? addr >= sSymbols[lo - 1].addr + sSymbols[lo - 1].size
                                   : addr - sSymbols[lo - 1].addr > 0x1000)
        return NULL;
    return &sSymbols[lo - 1];
}

/* --------------------------------------------------------------------- */
/* Report formatting (async-signal-safe)                                 */
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

static const char *SignalName(int sig)
{
    switch (sig)
    {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGBUS:  return "SIGBUS (bus error)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGFPE:  return "SIGFPE (arithmetic error, e.g. division by zero)";
    case SIGABRT: return "SIGABRT (abort)";
    default:      return "fatal signal";
    }
}

static uintptr_t FaultingPc(void *context)
{
    const ucontext_t *uc = context;
#if defined(__i386__)
    return (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
#elif defined(__x86_64__)
    return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#else
    (void)uc;
    return 0;
#endif
}

static void WriteAll(int fd, const char *buf, size_t len)
{
    while (len > 0)
    {
        ssize_t n = write(fd, buf, len);
        if (n <= 0)
            return;
        buf += n;
        len -= (size_t)n;
    }
}

/* Writes `value` at `out` as exactly `digits` decimal digits. */
static char *PutDigits(char *out, uint32_t value, int digits)
{
    for (int i = digits - 1; i >= 0; i--, value /= 10)
        out[i] = (char)('0' + value % 10);
    return out + digits;
}

/* sReportPath = sReportDir/YYYY-MM-DD_HH-MM-SS<suffix>, in local time. */
static void BuildReportPath(void)
{
    struct timespec now;
    long long days, secs;
    long long era, doe, yoe, doy, mp, year, month, day;
    char *out = sReportPath;
    size_t dirLen = strlen(sReportDir);

    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        now.tv_sec = 0;
    secs = (long long)now.tv_sec + sUtcOffset;
    days = secs / 86400;
    secs %= 86400;
    if (secs < 0)
    {
        secs += 86400;
        days--;
    }

    /* Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm). */
    days += 719468;
    era = (days >= 0 ? days : days - 146096) / 146097;
    doe = days - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = yoe + era * 400 + (month <= 2);

    memcpy(out, sReportDir, dirLen);
    out += dirLen;
    *out++ = '/';
    out = PutDigits(out, (uint32_t)year, 4);
    *out++ = '-';
    out = PutDigits(out, (uint32_t)month, 2);
    *out++ = '-';
    out = PutDigits(out, (uint32_t)day, 2);
    *out++ = '_';
    out = PutDigits(out, (uint32_t)(secs / 3600), 2);
    *out++ = '-';
    out = PutDigits(out, (uint32_t)(secs / 60 % 60), 2);
    *out++ = '-';
    out = PutDigits(out, (uint32_t)(secs % 60), 2);
    memcpy(out, CRASH_REPORT_SUFFIX, sizeof(CRASH_REPORT_SUFFIX));
}

static void OnFatalSignal(int sig, siginfo_t *info, void *context)
{
    static volatile sig_atomic_t sInHandler;
    void *frames[MAX_FRAMES];
    struct sigaction dfl;
    int frameCount, i, fd;

    if (!sInHandler)
    {
        sInHandler = 1;
        sReportLen = 0;

        Put("\n=== pkmemerald crash ===\n");
        Put("signal: ");
        Put(SignalName(sig));
        Put("\n");
        if (sig != SIGABRT)
        {
            Put("fault address: ");
            PutAddress((uintptr_t)info->si_addr);
            Put("\n");
        }
        Put("crashed at: ");
        PutAddress(FaultingPc(context));
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

        Put("call stack:\n");
        frameCount = backtrace(frames, MAX_FRAMES);
        for (i = 0; i < frameCount; i++)
        {
            Put("  #");
            PutDec((uint32_t)i);
            Put("  ");
            PutAddress((uintptr_t)frames[i]);
            Put("\n");
        }
        Put("(frames above the crash location are the crash handler itself)\n");

        WriteAll(STDERR_FILENO, sReport, sReportLen);
        BuildReportPath();
        fd = open(sReportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
            WriteAll(fd, sReport, sReportLen);
            close(fd);
            WriteAll(STDERR_FILENO, "crash report written to ", 24);
            WriteAll(STDERR_FILENO, sReportPath, strlen(sReportPath));
            WriteAll(STDERR_FILENO, "\n", 1);
        }
    }

    /* Let the signal do what it would have done: core dump / debugger. */
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, NULL);
    raise(sig);
}

/* --------------------------------------------------------------------- */
/* Public API                                                            */
/* --------------------------------------------------------------------- */

void Crash_Install(const char *reportDir)
{
#if PKM_SANITIZE
    /* The sanitizer reports crashes itself, with more detail; taking over
     * its signals would hide that. */
    (void)reportDir;
    return;
#endif
    static const int sSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    void *warmup[1];
    stack_t altStack;
    struct sigaction sa;
    struct tm local;
    time_t now = time(NULL);
    size_t i;

    strncpy(sReportDir, reportDir, sizeof(sReportDir) - 1);
    if (localtime_r(&now, &local) != NULL)
        sUtcOffset = local.tm_gmtoff;
    LoadSymbols();
    backtrace(warmup, 1);

    altStack.ss_sp = sAltStack;
    altStack.ss_size = sizeof(sAltStack);
    altStack.ss_flags = 0;
    sigaltstack(&altStack, NULL);

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnFatalSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof(sSignals) / sizeof(sSignals[0]); i++)
        sigaction(sSignals[i], &sa, NULL);
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
