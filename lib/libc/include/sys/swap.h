#ifndef _SYS_SWAP_H
#define _SYS_SWAP_H

int swapon(const char *path, int flags);
int swapoff(const char *path);

#endif
