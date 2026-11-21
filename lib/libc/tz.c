#include <tz.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

const struct tzinfo tz_table[] = {
    { "UTC",                 "UTC",   0 },
    { "Asia/Jakarta",        "WIB",   7 * 60 },
    { "Asia/Makassar",       "WITA",  8 * 60 },
    { "Asia/Jayapura",       "WIT",   9 * 60 },
    { "Asia/Singapore",      "+08",   8 * 60 },
    { "Asia/Kuala_Lumpur",   "+08",   8 * 60 },
    { "Asia/Manila",         "PST",   8 * 60 },
    { "Asia/Bangkok",        "+07",   7 * 60 },
    { "Asia/Ho_Chi_Minh",    "+07",   7 * 60 },
    { "Asia/Shanghai",       "CST",   8 * 60 },
    { "Asia/Hong_Kong",      "HKT",   8 * 60 },
    { "Asia/Tokyo",          "JST",   9 * 60 },
    { "Asia/Seoul",          "KST",   9 * 60 },
    { "Asia/Kolkata",        "IST",   5 * 60 + 30 },
    { "Asia/Dubai",          "+04",   4 * 60 },
    { "Asia/Riyadh",         "+03",   3 * 60 },
    { "Africa/Johannesburg", "SAST",  2 * 60 },
    { "Africa/Lagos",        "WAT",   1 * 60 },
    { "Africa/Nairobi",      "EAT",   3 * 60 },
    { "America/Sao_Paulo",   "-03",  -3 * 60 },
    { "America/Bogota",      "-05",  -5 * 60 },
    { "Pacific/Honolulu",    "HST", -10 * 60 },
};

const int tz_count = (int)(sizeof(tz_table) / sizeof(tz_table[0]));

int tz_find(const char *name)
{
    int i;

    for (i = 0; i < tz_count; i++)
        if (!strcmp(tz_table[i].name, name))
            return i;
    return -1;
}

const struct tzinfo *tz_current(void)
{
    static const struct tzinfo *cached;
    char buf[64];
    const char *name = getenv("TZ");
    int i, fd;

    if (cached)
        return cached;
    if (!name || !*name) {
        long n = 0;

        buf[0] = 0;
        fd = open("/etc/timezone", O_RDONLY);
        if (fd >= 0) {
            n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
        }
        if (n < 0)
            n = 0;
        buf[n] = 0;
        for (i = 0; buf[i] && buf[i] != '\n'; i++)
            ;
        buf[i] = 0;
        name = buf;
    }
    i = tz_find(name);
    cached = &tz_table[i < 0 ? 0 : i];
    return cached;
}
