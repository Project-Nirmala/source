#ifndef _LIBC_SYS_SYSMACROS_H
#define _LIBC_SYS_SYSMACROS_H

#define major(d) ((unsigned)((((d) >> 8) & 0xfff) | (((d) >> 32) & ~0xfffUL)))
#define minor(d) ((unsigned)(((d) & 0xff) | (((d) >> 12) & ~0xffUL)))
#define makedev(ma, mi) ((((unsigned long)(ma) & 0xfff) << 8) | ((unsigned long)(mi) & 0xff) | (((unsigned long)(mi) & ~0xffUL) << 12) | (((unsigned long)(ma) & ~0xfffUL) << 32))

#endif
