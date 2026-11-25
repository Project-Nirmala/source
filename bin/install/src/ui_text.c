#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include "engine.h"
#include "ui.h"

static int readline(char *buf, size_t size)
{
    size_t n = 0;
    char c;
    long r = 1;

    while (n + 1 < size && (r = read(0, &c, 1)) == 1 && c != '\n')
        buf[n++] = c;
    buf[n] = 0;
    return r <= 0 && n == 0 ? -1 : (int)n;
}

static int readsecret(char *buf, size_t size)
{
    struct termios t, raw;
    int n;

    tcgetattr(0, &t);
    raw = t;
    raw.c_lflag &= ~(unsigned)ECHO;
    tcsetattr(0, TCSANOW, &raw);
    n = readline(buf, size);
    tcsetattr(0, TCSANOW, &t);
    printf("\n");
    return n;
}

static int ask_password(const char *who, char *out, size_t size)
{
    char again[128];
    int tries;

    for (tries = 0; tries < 3; tries++) {
        printf("Password for %s: ", who);
        if (readsecret(out, size) < 0)
            return -1;
        if (!out[0]) {
            printf("The password cannot be empty.\n");
            continue;
        }
        printf("Retype the password: ");
        if (readsecret(again, sizeof(again)) < 0)
            return -1;
        if (!strcmp(out, again)) {
            memset(again, 0, sizeof(again));
            return 0;
        }
        printf("The passwords do not match.\n");
    }
    memset(again, 0, sizeof(again));
    return -1;
}

static int collect_accounts(struct account *acc)
{
    char name[64];
    int tries;

    memset(acc, 0, sizeof(*acc) * MKROOT_MAX_ACCOUNTS);
    snprintf(acc[0].name, sizeof(acc[0].name), "root");
    snprintf(acc[0].home, sizeof(acc[0].home), "/root");
    snprintf(acc[0].shell, sizeof(acc[0].shell), "/bin/dash");
    printf("\nSet up the administrator account.\n");
    if (ask_password("root", acc[0].password, sizeof(acc[0].password)) < 0)
        return -1;

    for (tries = 0; tries < 3; tries++) {
        printf("\nUsername for a regular account (leave empty to skip): ");
        if (readline(name, sizeof(name)) < 0)
            return -1;
        if (!name[0])
            return 1;
        if (inst_valid_username(name))
            break;
        printf("Use 1-31 characters from a-z, 0-9, '_' and '-', starting with a letter or '_' (not \"root\").\n");
    }
    if (tries == 3)
        return -1;
    snprintf(acc[1].name, sizeof(acc[1].name), "%s", name);
    snprintf(acc[1].home, sizeof(acc[1].home), "/home/%s", name);
    snprintf(acc[1].shell, sizeof(acc[1].shell), "/bin/dash");
    acc[1].uid = acc[1].gid = 1000;
    if (ask_password(name, acc[1].password, sizeof(acc[1].password)) < 0)
        return -1;
    return 2;
}

static unsigned ask_mib(const char *prompt, unsigned def)
{
    char buf[32];
    int tries;

    for (tries = 0; tries < 3; tries++) {
        char *e;
        unsigned long v;

        printf("%s [%u]: ", prompt, def);
        if (readline(buf, sizeof(buf)) < 0)
            return def;
        if (!buf[0])
            return def;
        v = strtoul(buf, &e, 10);
        if (*e == 0 && v <= 0x7FFFFFFFul)
            return (unsigned)v;
        printf("Enter a whole number of MiB.\n");
    }
    return def;
}

static void progress(int step, void *ctx)
{
    (void)ctx;
    if (step < INST_STEPS)
        printf("[%d/%d] %s\n", step + 1, INST_STEPS, inst_step_names[step]);
}

int ui_text_run(const char *target)
{
    struct inst_disk disks[INST_MAX_DISKS];
    struct account acc[MKROOT_MAX_ACCOUNTS];
    struct inst_layout lay;
    struct sysconfig cfg;
    char answer[64], choice[16], err[128];
    unsigned esp_mib = 0, swap_mib = 0, root_mib = 0;
    int count, nacc, i, r;

    printf("NirmalaSYS installer\n\n");
    count = inst_list(disks, INST_MAX_DISKS);
    if (count == 0) {
        dprintf(2, "install: no disks found\n");
        return 1;
    }
    for (i = 0; i < count; i++)
        printf("  %d) %-8s %8lu MiB  %s%s\n", i + 1, disks[i].name, (unsigned long)disks[i].mib, disks[i].model,
               disks[i].busy ? "  (in use, cannot be installed to)" : "");
    if (!target) {
        printf("\nInstall to which disk (number or name)? ");
        if (readline(answer, sizeof(answer)) < 0)
            return 1;
        if (answer[0] >= '1' && answer[0] <= '9' && atoi(answer) <= count)
            snprintf(choice, sizeof(choice), "%s", disks[atoi(answer) - 1].name);
        else
            snprintf(choice, sizeof(choice), "%s", answer);
        target = choice;
    }
    printf("\nPartitioning\n  1) Automatic (%u MiB boot, the rest root, no swap)\n  2) Manual (choose sizes, optional swap)\n"
           "Choice [1]: ", INST_ESP_MIB);
    if (readline(answer, sizeof(answer)) < 0)
        return 1;
    if (answer[0] == '2') {
        esp_mib = ask_mib("Boot (EFI) partition size in MiB", INST_ESP_MIB);
        swap_mib = ask_mib("Swap partition size in MiB (0 = none)", 0);
        root_mib = ask_mib("Root partition size in MiB (0 = use the rest of the disk)", 0);
    }
    if (inst_plan_ex(target, esp_mib, swap_mib, root_mib, &lay, err, sizeof(err)) < 0) {
        dprintf(2, "install: %s\n", err);
        return 1;
    }

    nacc = collect_accounts(acc);
    if (nacc < 0) {
        dprintf(2, "install: account setup failed\n");
        memset(acc, 0, sizeof(acc));
        return 1;
    }

    printf("\nInstallation summary\n"
           "  Disk:         %s (%lu MiB, GPT)\n"
           "  Partition 1:  EFI System, %lu MiB, FAT32 (boot loader, kernel)\n",
           target, (unsigned long)(lay.total / 2048), (unsigned long)(lay.esp_sectors / 2048));
    if (lay.swap_sectors)
        printf("  Partition 2:  nirmala-swap, %lu MiB, swap\n", (unsigned long)(lay.swap_sectors / 2048));
    printf("  Partition %d:  nirmala-root, %lu MiB, lemonfs (/)\n", lay.swap_sectors ? 3 : 2,
           (unsigned long)(lay.root_sectors / 2048));
    printf("  Accounts:     %s%s%s\n", acc[0].name, nacc > 1 ? ", " : "", nacc > 1 ? acc[1].name : "");
    printf("\nAll data on %s will be erased. Type yes to continue: ", target);
    if (readline(answer, sizeof(answer)) < 0 || strcmp(answer, "yes")) {
        printf("Cancelled.\n");
        memset(acc, 0, sizeof(acc));
        return 1;
    }
    inst_config_defaults(&cfg);
    r = inst_run(target, &lay, acc, (unsigned)nacc, &cfg, progress, NULL, err, sizeof(err));
    memset(acc, 0, sizeof(acc));
    if (r < 0) {
        dprintf(2, "install: %s\n", err);
        return 1;
    }
    printf("\nInstallation finished.\n"
           "Shut down, remove the install media and boot this disk in UEFI mode.\n");
    return 0;
}
