#include <uaccess.h>
#include <proc.h>
#include <mm.h>
#include <kern.h>
#include <sys/errno.h>

static int check_range(uint64_t uva, size_t n, bool write)
{
    uint64_t pg, end;

    if (uva + n < uva || uva + n > USER_TOP)
        return -EFAULT;
    if (n == 0)
        return 0;
    end = uva + n;
    for (pg = PAGE_ALIGN_DOWN(uva); pg < end; pg += PAGE_SIZE) {
        uint64_t fl;
        if (!vmm_user_probe(curproc->pml4, pg, &fl) || !(fl & PTE_US))
            return -EFAULT;
        if (write && !(fl & PTE_RW))
            return -EFAULT;
    }
    return 0;
}

int copy_from_user(void *dst, uint64_t uva, size_t n)
{
    int r = check_range(uva, n, false);

    if (r < 0)
        return r;
    memcpy(dst, (const void *)uva, n);
    return 0;
}

int copy_to_user(uint64_t uva, const void *src, size_t n)
{
    int r = check_range(uva, n, true);

    if (r < 0)
        return r;
    memcpy((void *)uva, src, n);
    return 0;
}

long copy_str_from_user(char *dst, uint64_t uva, size_t max)
{
    size_t i;

    for (i = 0; i < max; i++) {
        uint64_t a = uva + i, fl;

        if (a >= USER_TOP)
            return -EFAULT;
        if ((i == 0 || !(a & (PAGE_SIZE - 1))) &&
            (!vmm_user_probe(curproc->pml4, a, &fl) || !(fl & PTE_US)))
            return -EFAULT;
        dst[i] = *(const char *)a;
        if (!dst[i])
            return (long)i;
    }
    return -ENAMETOOLONG;
}
