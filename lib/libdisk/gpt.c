#include <diskfmt.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define SECTOR 512u
#define ENTRY_SIZE 128u
#define ENTRY_COUNT 128u
#define ENTRY_SECTORS (ENTRY_COUNT * ENTRY_SIZE / SECTOR)

uint32_t crc32_calc(const void *data, size_t n)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int k;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t get64(const uint8_t *p)
{
    return get32(p) | (uint64_t)get32(p + 4) << 32;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int gpt_guid_parse(const char *s, uint8_t out[16])
{
    static const int order[16] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
    unsigned i, k = 0;

    if (strlen(s) != 36)
        return -1;
    for (i = 0; i < 36; i++) {
        int hi, lo;

        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-')
                return -1;
            continue;
        }
        hi = hexval(s[i]);
        lo = hexval(s[i + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[order[k++]] = (uint8_t)(hi << 4 | lo);
        i++;
    }
    return 0;
}

void gpt_guid_format(const uint8_t g[16], char out[37])
{
    static const int order[16] = { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
    static const char hex[] = "0123456789ABCDEF";
    unsigned i, o = 0;

    for (i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out[o++] = '-';
        out[o++] = hex[g[order[i]] >> 4];
        out[o++] = hex[g[order[i]] & 15];
    }
    out[o] = 0;
}

void gpt_random_guid(uint8_t out[16])
{
    static uint64_t state;
    unsigned i;

    if (!state)
        state = (uint64_t)time(NULL) * 6364136223846793005ull + 1442695040888963407ull;
    for (i = 0; i < 16; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out[i] = (uint8_t)(state >> 24);
    }
    out[6] = (uint8_t)((out[6] & 0x0F) | 0x40);
    out[8] = (uint8_t)((out[8] & 0x3F) | 0x80);
}

static int dev_io(int fd, uint64_t lba, void *buf, size_t len, int write_op)
{
    uint8_t *p = buf;

    if (lseek(fd, (off_t)(lba * SECTOR), SEEK_SET) < 0)
        return -1;
    while (len) {
        long r = write_op ? write(fd, p, len) : read(fd, p, len);

        if (r <= 0)
            return -1;
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

static void make_header(uint8_t *h, uint64_t total, uint64_t my, uint64_t alt, uint64_t entries_lba,
                        const uint8_t *guid, uint32_t entries_crc)
{
    memset(h, 0, SECTOR);
    memcpy(h, "EFI PART", 8);
    put32(h + 8, 0x00010000);
    put32(h + 12, 92);
    put64(h + 24, my);
    put64(h + 32, alt);
    put64(h + 40, 2 + ENTRY_SECTORS);
    put64(h + 48, total - 2 - ENTRY_SECTORS);
    memcpy(h + 56, guid, 16);
    put64(h + 72, entries_lba);
    put32(h + 80, ENTRY_COUNT);
    put32(h + 84, ENTRY_SIZE);
    put32(h + 88, entries_crc);
    put32(h + 16, crc32_calc(h, 92));
}

int gpt_write(int fd, uint64_t total, const struct gpt_part *parts, unsigned n)
{
    static uint8_t entries[ENTRY_COUNT * ENTRY_SIZE];
    uint8_t hdr[SECTOR], mbr[SECTOR], guid[16];
    uint64_t first_usable = 2 + ENTRY_SECTORS, last_usable = total - 2 - ENTRY_SECTORS;
    uint32_t ecrc;
    unsigned i, k;

    if (n > ENTRY_COUNT || total < 2 * (2 + ENTRY_SECTORS) + 8)
        return -1;
    memset(entries, 0, sizeof(entries));
    for (i = 0; i < n; i++) {
        uint8_t *e = entries + (size_t)i * ENTRY_SIZE;

        if (parts[i].first < first_usable || parts[i].last > last_usable || parts[i].last < parts[i].first)
            return -1;
        memcpy(e, parts[i].type, 16);
        memcpy(e + 16, parts[i].guid, 16);
        put64(e + 32, parts[i].first);
        put64(e + 40, parts[i].last);
        put64(e + 48, parts[i].attr);
        for (k = 0; k < 36 && parts[i].name[k]; k++)
            put16(e + 56 + 2 * k, (uint8_t)parts[i].name[k]);
    }
    ecrc = crc32_calc(entries, sizeof(entries));
    gpt_random_guid(guid);

    memset(mbr, 0, sizeof(mbr));
    mbr[446 + 2] = 2;
    mbr[446 + 4] = 0xEE;
    mbr[446 + 5] = mbr[446 + 6] = mbr[446 + 7] = 0xFF;
    put32(mbr + 446 + 8, 1);
    put32(mbr + 446 + 12, total - 1 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)(total - 1));
    mbr[510] = 0x55;
    mbr[511] = 0xAA;

    if (dev_io(fd, 2, entries, sizeof(entries), 1) < 0)
        return -1;
    if (dev_io(fd, total - 1 - ENTRY_SECTORS, entries, sizeof(entries), 1) < 0)
        return -1;
    make_header(hdr, total, total - 1, 1, total - 1 - ENTRY_SECTORS, guid, ecrc);
    if (dev_io(fd, total - 1, hdr, SECTOR, 1) < 0)
        return -1;
    make_header(hdr, total, 1, total - 1, 2, guid, ecrc);
    if (dev_io(fd, 1, hdr, SECTOR, 1) < 0)
        return -1;
    if (dev_io(fd, 0, mbr, SECTOR, 1) < 0)
        return -1;
    return 0;
}

int gpt_read(int fd, uint64_t total, struct gpt_part *parts, unsigned max, unsigned *n)
{
    static uint8_t entries[ENTRY_COUNT * ENTRY_SIZE];
    uint8_t hdr[SECTOR];
    uint32_t crc, count, esz;
    uint64_t elba;
    unsigned i, k, out = 0;

    (void)total;
    if (dev_io(fd, 1, hdr, SECTOR, 0) < 0 || memcmp(hdr, "EFI PART", 8))
        return -1;
    crc = get32(hdr + 16);
    put32(hdr + 16, 0);
    if (crc32_calc(hdr, get32(hdr + 12)) != crc)
        return -1;
    elba = get64(hdr + 72);
    count = get32(hdr + 80);
    esz = get32(hdr + 84);
    if (count != ENTRY_COUNT || esz != ENTRY_SIZE)
        return -1;
    if (dev_io(fd, elba, entries, sizeof(entries), 0) < 0)
        return -1;
    if (crc32_calc(entries, sizeof(entries)) != get32(hdr + 88))
        return -1;
    for (i = 0; i < ENTRY_COUNT && out < max; i++) {
        const uint8_t *e = entries + (size_t)i * ENTRY_SIZE;
        uint8_t zero[16] = { 0 };

        if (!memcmp(e, zero, 16))
            continue;
        memset(&parts[out], 0, sizeof(parts[out]));
        memcpy(parts[out].type, e, 16);
        memcpy(parts[out].guid, e + 16, 16);
        parts[out].first = get64(e + 32);
        parts[out].last = get64(e + 40);
        parts[out].attr = get64(e + 48);
        for (k = 0; k < 35 && e[56 + 2 * k]; k++)
            parts[out].name[k] = (char)e[56 + 2 * k];
        out++;
    }
    *n = out;
    return 0;
}
