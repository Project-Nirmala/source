#include <kern.h>
#include <io.h>

__noreturn void panic(const char *fmt, ...)
{
    va_list ap;

    cli();
    kputs("\n*** KERNEL PANIC: ");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kputs("\nSystem halted.\n");
    for (;;)
        hlt();
}
