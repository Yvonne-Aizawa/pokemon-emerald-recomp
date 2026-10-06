/* Separate invocations cover the cached RTC environment setting. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "global.h"
#include "siirtc.h"

static unsigned Bcd(unsigned value)
{
    return (value / 10 << 4) | (value % 10);
}

int main(int argc, char **argv)
{
    const char *value = argc > 1 ? argv[1] : "2024-02-29 12:34:56";
    struct SiiRtcInfo rtc;
    time_t before, after;
    struct tm tm;
#ifdef _WIN32
    _putenv_s("PKM_FIXED_TIME", value);
#else
    setenv("PKM_FIXED_TIME", value, 1);
#endif
    before = time(NULL);
    SiiRtcGetDateTime(&rtc);
    after = time(NULL);
    if (argc > 2 && strcmp(argv[2], "--invalid") == 0)
    {
        /* Either endpoint handles a call straddling midnight. */
        for (time_t now = before; now <= after; now++)
        {
#ifdef _WIN32
            localtime_s(&tm, &now);
#else
            localtime_r(&now, &tm);
#endif
            if (rtc.year == Bcd((tm.tm_year + 1900 - 2000) % 100)
                && rtc.month == Bcd(tm.tm_mon + 1) && rtc.day == Bcd(tm.tm_mday)
                && rtc.hour == Bcd(tm.tm_hour) && rtc.minute == Bcd(tm.tm_min)
                && rtc.second == Bcd(tm.tm_sec))
                return 0;
        }
        fprintf(stderr, "invalid fixed timestamp did not fall back to local time\n");
        return 1;
    }
    if (rtc.year != 0x24 || rtc.month != 0x02 || rtc.day != 0x29
        || rtc.hour != 0x12 || rtc.minute != 0x34 || rtc.second != 0x56)
        return 1;
    return 0;
}
