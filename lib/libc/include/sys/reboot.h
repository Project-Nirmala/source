#ifndef _SYS_REBOOT_H
#define _SYS_REBOOT_H

/* Same command values as Linux. Only uid 0 may reboot. */
#define RB_AUTOBOOT   0x01234567
#define RB_POWER_OFF  0x4321FEDC
#define RB_HALT_SYSTEM 0xCDEF0123

/* Flushes the disks first. Returns -1 with errno set if it fails (EPERM for non-root). */
int reboot(int cmd);

#endif
