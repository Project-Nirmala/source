#ifndef _LIBC_SETJMP_H
#define _LIBC_SETJMP_H

typedef long jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#define sigjmp_buf jmp_buf
#define sigsetjmp(e, m) setjmp(e)
#define siglongjmp(e, v) longjmp(e, v)

#endif
