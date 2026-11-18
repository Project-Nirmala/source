#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <proc.h>
#include <mm.h>
#include <wallclock.h>

static uint8_t cmos_reg[128], cmos_sel;
uint8_t inb(uint16_t p) { return p == 0x71 ? cmos_reg[cmos_sel & 127] : 0; }
void outb(uint16_t p, uint8_t v) { if (p == 0x70) cmos_sel = v; }
uint64_t pit_ticks(void) { return 12345; }
uint32_t pit_hz(void) { return 100; }

static uint8_t bcd(unsigned v) { return (uint8_t)((v / 10) << 4 | (v % 10)); }
static void set_rtc(int y, int mo, int d, int h, int mi, int s, int binary, int mode24, int with_century)
{
    uint8_t (*enc)(unsigned) = binary ? (uint8_t (*)(unsigned))0 : bcd;
    #define E(v) (binary ? (uint8_t)(v) : enc(v))
    int pm = 0, hh = h;
    memset(cmos_reg, 0, sizeof cmos_reg);
    if (!mode24) { pm = h >= 12; hh = h % 12; if (hh == 0) hh = 12; }
    cmos_reg[0x00] = E(s); cmos_reg[0x02] = E(mi); cmos_reg[0x04] = (uint8_t)(E(hh) | (pm ? 0x80 : 0));
    cmos_reg[0x07] = E(d); cmos_reg[0x08] = E(mo); cmos_reg[0x09] = E(y % 100);
    cmos_reg[0x32] = with_century ? E(y / 100) : 0;
    cmos_reg[0x0B] = (uint8_t)((binary ? 4 : 0) | (mode24 ? 2 : 0));
    #undef E
}
static time_t host_epoch(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t; memset(&t, 0, sizeof t); t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
    return timegm(&t);
}

#define MAXPG 4096
static uint64_t mapped[MAXPG]; static int nmapped; static int pages_out; static int fail_after = -1;
paddr_t pmm_alloc_zero(void) { if (fail_after == 0) return 0; if (fail_after > 0) fail_after--; pages_out++; return 0x1000 * (paddr_t)(pages_out + 100); }
void pmm_free(paddr_t pa) { (void)pa; pages_out--; }
int vmm_map(uint64_t pml4, uint64_t va, paddr_t pa, uint64_t flags) { (void)pml4; (void)pa; if (flags != (PTE_RW | PTE_US | PTE_NX)) { printf("FAIL: heap pages must be user, writable, no-exec\n"); exit(1); } for (int i = 0; i < nmapped; i++) if (mapped[i] == va) { printf("FAIL: double map of %lx\n", va); exit(1); } mapped[nmapped++] = va; return 0; }
uint64_t vmm_unmap(uint64_t pml4, uint64_t va) { (void)pml4; for (int i = 0; i < nmapped; i++) if (mapped[i] == va) { mapped[i] = mapped[--nmapped]; return 0x1000; } return 0; }

static int fails;
#define T(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)

int main(void)
{
    set_rtc(2026, 11, 15, 12, 34, 56, 0, 1, 0); T(rtc_read_epoch() == (uint64_t)host_epoch(2026, 11, 15, 12, 34, 56), "BCD, 24-hour");
    set_rtc(2026, 11, 15, 12, 34, 56, 1, 1, 0); T(rtc_read_epoch() == (uint64_t)host_epoch(2026, 11, 15, 12, 34, 56), "binary, 24-hour");
    set_rtc(2024, 2, 29, 23, 59, 59, 0, 1, 1); T(rtc_read_epoch() == (uint64_t)host_epoch(2024, 2, 29, 23, 59, 59), "leap day with century register");
    set_rtc(2026, 11, 15, 0, 5, 0, 0, 0, 0); T(rtc_read_epoch() == (uint64_t)host_epoch(2026, 11, 15, 0, 5, 0), "12-hour clock, midnight");
    set_rtc(2026, 11, 15, 12, 0, 0, 0, 0, 0); T(rtc_read_epoch() == (uint64_t)host_epoch(2026, 11, 15, 12, 0, 0), "12-hour clock, noon");
    set_rtc(2026, 11, 15, 15, 30, 0, 1, 0, 0); T(rtc_read_epoch() == (uint64_t)host_epoch(2026, 11, 15, 15, 30, 0), "12-hour clock, binary, 3 pm");
    memset(cmos_reg, 0, sizeof cmos_reg); T(rtc_read_epoch() == 0, "garbage RTC yields 0 rather than a wild date");
    srand(7); int bad = 0;
    for (int i = 0; i < 20000; i++) {
        int y = 2000 + rand() % 100, mo = 1 + rand() % 12, d = 1 + rand() % 28, h = rand() % 24, mi = rand() % 60, s = rand() % 60, bin = rand() % 2, m24 = rand() % 2;
        set_rtc(y, mo, d, h, mi, s, bin, m24, rand() % 2);
        if (rtc_read_epoch() != (uint64_t)host_epoch(y, mo, d, h, mi, s)) { if (bad++ < 3) printf("  mismatch %d-%02d-%02d %02d:%02d:%02d bin=%d 24h=%d\n", y, mo, d, h, mi, s, bin, m24); }
    }
    T(bad == 0, "20000 random dates match glibc timegm");
    set_rtc(2026, 11, 15, 12, 0, 0, 0, 1, 0); wallclock_init();
    T(wall_seconds() == (uint64_t)host_epoch(2026, 11, 15, 12, 0, 0), "wall clock = RTC at boot plus uptime");
    uint64_t s; uint32_t ns; wall_time(&s, &ns); T(s == wall_seconds() && ns == 45 * 10000000u, "sub-second part comes from ticks");

    struct proc p; memset(&p, 0, sizeof p); p.brk_start = p.brk = 0x500000;
    T(proc_brk(&p, 0) == 0x500000, "brk(0) reports the current break");
    T(proc_brk(&p, 0x500010) == 0x500010 && nmapped == 1, "growing inside a page maps exactly one page");
    T(proc_brk(&p, 0x500FF0) == 0x500FF0 && nmapped == 1, "growing within the same page maps nothing more");
    T(proc_brk(&p, 0x503001) == 0x503001 && nmapped == 4, "crossing page boundaries maps the pages needed");
    T(proc_brk(&p, 0x500800) == 0x500800 && nmapped == 1 && pages_out == 1, "shrinking returns pages to the allocator");
    T(proc_brk(&p, 0x4FF000) == 0x500800, "below the start of the heap is refused");
    T(proc_brk(&p, 0x500000 + 0x10000000UL + 1) == 0x500800, "past the heap limit is refused");
    fail_after = 2; long before = p.brk;
    T(proc_brk(&p, 0x520000) == before && nmapped == 1 && pages_out == 1, "out of memory half-way: everything is rolled back");
    fail_after = -1;
    T(proc_brk(&p, 0x500000) == 0x500000 && nmapped == 0 && pages_out == 0, "shrinking to the start frees the whole heap");
    printf(fails ? "kern: %d FAILURES\n" : "kern: all tests passed\n", fails);
    return fails != 0;
}
