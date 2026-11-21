#ifndef _LIBC_TZ_H
#define _LIBC_TZ_H

/*
 * Time zones without a tz database: a fixed table of zones that do not use
 * daylight saving time, each a name, a standard abbreviation and an offset
 * east of UTC in minutes.
 */
struct tzinfo {
    const char *name;
    const char *abbr;
    int         minutes;
};

extern const struct tzinfo tz_table[];
extern const int tz_count;

/* Index into tz_table, or -1. */
int tz_find(const char *name);

/*
 * The active zone: $TZ, else the first line of /etc/timezone, else UTC. The
 * answer is cached for the life of the process.
 */
const struct tzinfo *tz_current(void);

#endif
