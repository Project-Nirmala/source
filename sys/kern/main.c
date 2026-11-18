#include <limine.h>
#include <kern.h>
#include <cpu.h>
#include <mm.h>
#include <proc.h>
#include <io.h>
#include <vfs.h>
#include <serial.h>
#include <tty.h>
#include <fbcon.h>
#include <fpu.h>
#include <wallclock.h>

#define HZ 100

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(5);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0, .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request exec_req = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID, .revision = 0, .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0, .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_req = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0, .response = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0, .response = 0
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

uint64_t hhdm_offset;

static void kheap_selftest(void)
{
    void *a = kmalloc(100), *b = kmalloc(5000), *c = kmalloc(40), *d;
    size_t i;

    KASSERT(a && b && c);
    memset(b, 0xAB, 5000);
    kfree(b);
    kfree(a);
    d = kmalloc(200000);
    KASSERT(d);
    for (i = 0; i < 200000; i += 4096)
        ((volatile uint8_t *)d)[i] = 1;
    kfree(c);
    kfree(d);
    d = kmalloc(5000);
    KASSERT(d);
    kfree(d);
}

void kmain(void)
{
    static char *const init_argv[] = { "init", NULL };
    static char *const init_env[] = { "PATH=/bin:/sbin", "HOME=/", NULL };

    serial_init();

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        kprintf("Limine base revision not supported\n");
        for (;;) hlt();
    }
    if (!hhdm_req.response || !exec_req.response || !memmap_req.response)
        panic("Limine: missing HHDM/executable/memmap response");
    hhdm_offset = hhdm_req.response->offset;

    kprintf("NirmalaSYS Starting!\n");

    gdt_init();
    kprintf("[ok] gdt + tss\n");
    idt_init();
    kprintf("[ok] idt\n");
    cpu_init();
    fpu_init();
    kprintf("[ok] cpu: gs base + syscall MSR\n");

    pmm_init(memmap_req.response);
    kprintf("[ok] pmm: %lu MiB free of %lu MiB\n",
            pmm_free_pages() * PAGE_SIZE >> 20, pmm_total_pages() * PAGE_SIZE >> 20);

    vmm_init(memmap_req.response, exec_req.response->virtual_base,
             exec_req.response->physical_base);
    kprintf("[ok] vmm: kernel page tables active (CR3=%lx)\n", kernel_pml4);

    kheap_init();
    kheap_selftest();
    fbcon_init(framebuffer_req.response);
    kprintf("[ok] kheap\n");

    pic_init();
    lapic_init();
    tty_init();
    serial_enable_rx();
    kbd_init();
    proc_init();
    pit_init(HZ);
    wallclock_init();
    kprintf("[ok] pic + lapic + pit @ %u Hz, scheduler\n", pit_hz());
    kprintf("[ok] tty: serial rx + ps/2 keyboard\n");

#ifdef CONFIG_SELFTEST
    __asm__ volatile("int3");
#endif

    if (!module_req.response || module_req.response->module_count < 1)
        panic("no initrd module provided by the bootloader");
    vfs_init(module_req.response->modules[0]->address, module_req.response->modules[0]->size);
    kprintf("[ok] vfs: initrd %lu bytes\n", module_req.response->modules[0]->size);

    if (!proc_create_exec("/sbin/init", init_argv, init_env))
        panic("cannot start /sbin/init");

#ifdef CONFIG_SELFTEST
    {
        static char *const t_sleep[] = { "utest", "sleep", NULL };
        static char *const t_spin[]  = { "utest", "spin", NULL };
        static char *const t_fault[] = { "utest", "fault", NULL };
        proc_create_exec("/bin/utest", t_sleep, init_env);
        proc_create_exec("/bin/utest", t_spin, init_env);
        proc_create_exec("/bin/utest", t_spin, init_env);
        proc_create_exec("/bin/utest", t_fault, init_env);
    }
#endif

    for (;;) {
        sti();
        hlt();
    }
}
