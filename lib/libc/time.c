#include <time.h>
#include <tz.h>

static long days_from_civil(long y, unsigned m, unsigned d)
{
    long era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    long days = *t / 86400, secs = *t % 86400, z, era, y;
    unsigned doe, yoe, doy, mp, d, m;

    if (secs < 0) {
        secs += 86400;
        days--;
    }
    out->tm_hour = (int)(secs / 3600);
    out->tm_min = (int)(secs % 3600 / 60);
    out->tm_sec = (int)(secs % 60);
    out->tm_wday = (int)((days + 4) % 7);
    if (out->tm_wday < 0)
        out->tm_wday += 7;
    z = days + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (long)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2)
        y++;
    out->tm_year = (int)(y - 1900);
    out->tm_mon = (int)m - 1;
    out->tm_mday = (int)d;
    out->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    out->tm_isdst = 0;
    return out;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm result;

    return gmtime_r(t, &result);
}

struct tm *localtime_r(const time_t *t, struct tm *out)
{
    time_t shifted = *t + (time_t)tz_current()->minutes * 60;

    return gmtime_r(&shifted, out);
}

struct tm *localtime(const time_t *t)
{
    static struct tm result;

    return localtime_r(t, &result);
}

time_t timegm(struct tm *tm)
{
    long y = tm->tm_year + 1900L, m = tm->tm_mon;

    y += m / 12;
    m %= 12;
    if (m < 0) {
        m += 12;
        y--;
    }
    return (time_t)(days_from_civil(y, (unsigned)m + 1, 1) * 86400 + (tm->tm_mday - 1) * 86400L +
                    tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec);
}
