#include <wallclock.h>
#include <io.h>
#include <cpu.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint64_t boot_epoch;

static uint8_t cmos(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    int64_t era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

struct rtc_regs {
    uint8_t sec, min, hour, day, mon, year, century;
};

static void rtc_snapshot(struct rtc_regs *r)
{
    while (cmos(0x0A) & 0x80)
        ;
    r->sec = cmos(0x00);
    r->min = cmos(0x02);
    r->hour = cmos(0x04);
    r->day = cmos(0x07);
    r->mon = cmos(0x08);
    r->year = cmos(0x09);
    r->century = cmos(0x32);
}

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

uint64_t rtc_read_epoch(void)
{
    struct rtc_regs a, b;
    uint8_t regb;
    unsigned year, hour;
    bool bcd, pm;

    do {
        rtc_snapshot(&a);
        rtc_snapshot(&b);
    } while (a.sec != b.sec || a.min != b.min || a.hour != b.hour || a.day != b.day ||
             a.mon != b.mon || a.year != b.year);
    regb = cmos(0x0B);
    bcd = !(regb & 4);
    pm = !(regb & 2) && (b.hour & 0x80);
    b.hour &= 0x7F;
    if (bcd) {
        b.sec = from_bcd(b.sec);
        b.min = from_bcd(b.min);
        b.hour = from_bcd(b.hour);
        b.day = from_bcd(b.day);
        b.mon = from_bcd(b.mon);
        b.year = from_bcd(b.year);
        b.century = from_bcd(b.century);
    }
    hour = b.hour;
    if (!(regb & 2))
        hour = (hour % 12) + (pm ? 12 : 0);
    year = (b.century >= 19 && b.century <= 30) ? b.century * 100u + b.year : 2000u + b.year;
    if (b.mon < 1 || b.mon > 12 || b.day < 1 || b.day > 31)
        return 0;
    return (uint64_t)days_from_civil(year, b.mon, b.day) * 86400 + hour * 3600u + b.min * 60u + b.sec;
}

void wallclock_init(void)
{
    uint64_t now = rtc_read_epoch();
    uint64_t up = pit_ticks() / pit_hz();

    boot_epoch = now > up ? now - up : 0;
}

uint64_t wall_seconds(void)
{
    return boot_epoch + pit_ticks() / pit_hz();
}

void wall_time(uint64_t *sec, uint32_t *nsec)
{
    uint64_t t = pit_ticks(), hz = pit_hz();

    *sec = boot_epoch + t / hz;
    *nsec = (uint32_t)((t % hz) * (1000000000UL / hz));
}
