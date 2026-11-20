#ifndef _LIBC_DISKFMT_H
#define _LIBC_DISKFMT_H

#include <stdint.h>
#include <stddef.h>

struct gpt_part {
    uint8_t  type[16];
    uint8_t  guid[16];
    uint64_t first, last;
    uint64_t attr;
    char     name[36];
};

#define GPT_MAX_PARTS 128

#define GPT_TYPE_ESP    "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
#define GPT_TYPE_LEMON  "4E52494D-414C-4C45-4D4F-4E4653000001"
#define GPT_TYPE_LINUX  "0FC63DAF-8483-4772-8E79-3D69D8477DE4"

uint32_t crc32_calc(const void *data, size_t n);
int      gpt_guid_parse(const char *s, uint8_t out[16]);
void     gpt_guid_format(const uint8_t g[16], char out[37]);
void     gpt_random_guid(uint8_t out[16]);
int      gpt_write(int fd, uint64_t total_sectors, const struct gpt_part *parts, unsigned n);
int      gpt_read(int fd, uint64_t total_sectors, struct gpt_part *parts, unsigned max, unsigned *n);

struct fat32;

int            fat32_format(int fd, uint64_t first, uint64_t nsect, const char *label);
struct fat32  *fat32_open(int fd, uint64_t first, uint64_t nsect, int writable);
int            fat32_close(struct fat32 *fs);
int            fat32_mkdir(struct fat32 *fs, const char *path);
int            fat32_put_fd(struct fat32 *fs, const char *path, int srcfd);
int            fat32_put_mem(struct fat32 *fs, const char *path, const void *data, size_t size);
int            fat32_cat(struct fat32 *fs, const char *path, int outfd);
int            fat32_ls(struct fat32 *fs, const char *path,
                        void (*cb)(const char *name, uint32_t size, int isdir, void *ctx), void *ctx);
int            fat32_remove(struct fat32 *fs, const char *path);

#endif
