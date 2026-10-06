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
#include <string.h>
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
            struct tm parsed;
            time_t base;
            size_t i;

            /* Exact-width decimal fields keep overflow, signs and trailing
             * text out before sscanf; the cartridge year is 2000..2099. */
            if (strlen(value) != 19)
                goto invalid;
            for (i = 0; i < 19; i++)
            {
                char separator = i == 4 || i == 7 ? '-' : i == 10 ? ' ' : i == 13 || i == 16 ? ':' : 0;
                if (separator ? value[i] != separator : value[i] < '0' || value[i] > '9')
                    goto invalid;
            }
            if (sscanf(value, "%4d-%2d-%2d %2d:%2d:%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6
                || tm.tm_year < 2000 || tm.tm_year > 2099
                || tm.tm_mon < 1 || tm.tm_mon > 12 || tm.tm_mday < 1 || tm.tm_mday > 31
                || tm.tm_hour > 23 || tm.tm_min > 59 || tm.tm_sec > 59)
                goto invalid;
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            parsed = tm;
#ifdef _WIN32
            base = _mkgmtime(&tm);
#else
            base = timegm(&tm);
#endif
            /* Conversion normalizes February 30 etc. Refuse any changed field
             * and times that this platform's time_t cannot represent. */
            if (base == (time_t)-1 || tm.tm_year != parsed.tm_year || tm.tm_mon != parsed.tm_mon
                || tm.tm_mday != parsed.tm_mday || tm.tm_hour != parsed.tm_hour
                || tm.tm_min != parsed.tm_min || tm.tm_sec != parsed.tm_sec)
                goto invalid;
            sBase = base;

        }
    }
    return sBase;

invalid:
    fprintf(stderr, "rtc: PKM_FIXED_TIME must be a valid UTC date in 2000..2099, \"YYYY-MM-DD HH:MM:SS\"; using the PC's clock\n");
    return -1;
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
