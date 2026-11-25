#ifndef INSTALL_ENGINE_H
#define INSTALL_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include "mkroot.h"

#define INST_MAX_DISKS 16
#define INST_NAME_MAX 16
#define INST_LETTER_DISKS 8
#define INST_NVME_CTRLS 4
#define INST_NVME_NS 4
#define INST_STEPS 7
#define INST_ESP_MIB 100u
#define INST_MIN_ROOT_MIB 64u
#define INST_MIN_ESP_MIB 64u
#define INST_MAX_ESP_MIB 1024u

struct inst_disk {
    char     name[INST_NAME_MAX];
    uint64_t mib;
    char     model[48];
    int      busy;
};

struct inst_layout {
    uint64_t total, esp_first, esp_sectors, swap_first, swap_sectors, root_first, root_sectors;
};

typedef void (*inst_progress)(int step, void *ctx);

extern const char *const inst_step_names[INST_STEPS];

/* Choices offered for the settings in struct sysconfig (time zones come from <tz.h>). */
extern const char *const inst_keymaps[];
extern const int inst_nkeymaps;
extern const char *const inst_locales[];
extern const int inst_nlocales;

void inst_config_defaults(struct sysconfig *cfg);
int  inst_valid_hostname(const char *s);
int  inst_valid_ipv4(const char *s);
int  inst_config_check(const struct sysconfig *cfg, char *err, size_t errsz);
/* off=0: restart, off=1: power off. Needs root; only returns on failure. */
int  inst_power(int off);

int inst_check_system(void);
int inst_list(struct inst_disk *out, int max);
int inst_valid_username(const char *s);
int inst_plan(const char *name, struct inst_layout *lay, char *err, size_t errsz);
/* Sizes in MiB. esp_mib 0 = default, swap_mib 0 = no swap, root_mib 0 = all remaining space. */
int inst_plan_ex(const char *name, unsigned esp_mib, unsigned swap_mib, unsigned root_mib, struct inst_layout *lay,
                 char *err, size_t errsz);
int inst_run(const char *name, const struct inst_layout *lay, const struct account *acc, unsigned nacc,
             const struct sysconfig *cfg, inst_progress cb, void *ctx, char *err, size_t errsz);

#endif
