#ifndef INSTALL_ENGINE_H
#define INSTALL_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include "mkroot.h"

#define INST_MAX_DISKS 16
#define INST_STEPS 7
#define INST_ESP_MIB 100u
#define INST_MIN_ROOT_MIB 64u

struct inst_disk {
    char     name[8];
    uint64_t mib;
    char     model[48];
    int      busy;
};

struct inst_layout {
    uint64_t total, esp_first, esp_sectors, root_first, root_sectors;
};

typedef void (*inst_progress)(int step, void *ctx);

extern const char *const inst_step_names[INST_STEPS];

int inst_check_system(void);
int inst_list(struct inst_disk *out, int max);
int inst_valid_username(const char *s);
int inst_plan(const char *name, struct inst_layout *lay, char *err, size_t errsz);
int inst_run(const char *name, const struct inst_layout *lay, const struct account *acc, unsigned nacc,
             inst_progress cb, void *ctx, char *err, size_t errsz);

#endif
