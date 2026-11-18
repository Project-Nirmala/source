#include <stdio.h>
#include <time.h>

int main(void)
{
    static const char *const day[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const month[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    time_t now = time(0);
    struct tm tm;

    gmtime_r(&now, &tm);
    printf("%s %s %2d %02d:%02d:%02d UTC %d\n", day[tm.tm_wday], month[tm.tm_mon], tm.tm_mday,
           tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900);
    return 0;
}
