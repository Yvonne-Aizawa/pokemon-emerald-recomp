/*
 * platform/src/host_rtc.c
 *
 * Host replacement for reference/src/siirtc.c, the driver for the Seiko
 * S-3511 real-time clock on the cartridge. The original bit-bangs the chip
 * through GPIO registers at fixed ROM addresses (0x80000C4..C8), which don't
 * exist on the host, so siirtc.c is excluded and this file implements the
 * same API on top of the PC's local clock.
 *
 * The chip has no "set date/time" command in this driver (rtc.c keeps the
 * player's clock as an offset from the RTC), so the PC clock maps straight
 * through. Values are BCD, as the chip reports them; the year counts from
 * 2000. The chip always appears present, powered and in 24-hour mode.
 *
 * PKM_FIXED_TIME="YYYY-MM-DD HH:MM:SS" (environment), for reproducible test
 * runs: the clock starts at that time and advances with game frames (60 per
 * second) instead of following the PC's clock.
 */

/* libc first: global.h defines function-like macros that clash with it. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "global.h"
#include "siirtc.h"

#include "platform/main_loop.h"

static u8 sStatus = SIIRTCINFO_24HOUR;

static u8 ToBcd(int value)
{
    return (u8)(((value / 10) % 10) << 4 | (value % 10));
}

/* PKM_FIXED_TIME, as seconds (UTC), or -1 if it isn't set. */
static time_t FixedTimeBase(void)
{
    static int sParsed;
    static time_t sBase = -1;

    if (!sParsed)
    {
        const char *value = getenv("PKM_FIXED_TIME");
        struct tm tm = {0};

        sParsed = 1;
        if (value != NULL && *value != '\0')
        {
            if (sscanf(value, "%d-%d-%d %d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
            {
                fprintf(stderr, "rtc: PKM_FIXED_TIME should be \"YYYY-MM-DD HH:MM:SS\", not \"%s\"; using the PC's clock\n", value);
                return -1;
            }
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
#ifdef _WIN32
            sBase = _mkgmtime(&tm);
#else
            sBase = timegm(&tm);
#endif
        }
    }
    return sBase;
}

static void ReadLocalTime(struct SiiRtcInfo *rtc)
{
    time_t base = FixedTimeBase();
    time_t now;
    struct tm tm;

    if (base != -1)
    {
        now = base + Host_GetFrameCount() / 60;
#ifdef _WIN32
        gmtime_s(&tm, &now);
#else
        gmtime_r(&now, &tm);
#endif
    }
    else
    {
        now = time(NULL);
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
    }
    rtc->year = ToBcd((tm.tm_year + 1900 - 2000) % 100);
    rtc->month = ToBcd(tm.tm_mon + 1);
    rtc->day = ToBcd(tm.tm_mday);
    rtc->dayOfWeek = ToBcd(tm.tm_wday);
    rtc->hour = ToBcd(tm.tm_hour);
    rtc->minute = ToBcd(tm.tm_min);
    rtc->second = ToBcd(tm.tm_sec > 59 ? 59 : tm.tm_sec);  /* leap second */
}

void SiiRtcUnprotect(void) { }
void SiiRtcProtect(void) { }

/* Low nibble 1 = chip found; high nibble = number of resets needed (none). */
u8 SiiRtcProbe(void)
{
    return 1;
}

bool8 SiiRtcReset(void)
{
    sStatus = SIIRTCINFO_24HOUR;
    return TRUE;
}

bool8 SiiRtcGetStatus(struct SiiRtcInfo *rtc)
{
    rtc->status = sStatus;
    return TRUE;
}

bool8 SiiRtcSetStatus(struct SiiRtcInfo *rtc)
{
    /* Only the interrupt-enable and 24-hour bits are writable on the chip. */
    sStatus = rtc->status & (SIIRTCINFO_INTFE | SIIRTCINFO_INTME | SIIRTCINFO_INTAE | SIIRTCINFO_24HOUR);
    return TRUE;
}

bool8 SiiRtcGetDateTime(struct SiiRtcInfo *rtc)
{
    ReadLocalTime(rtc);
    return TRUE;
}

bool8 SiiRtcGetTime(struct SiiRtcInfo *rtc)
{
    struct SiiRtcInfo now;

    ReadLocalTime(&now);
    rtc->hour = now.hour;
    rtc->minute = now.minute;
    rtc->second = now.second;
    return TRUE;
}
