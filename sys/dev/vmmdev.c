#include <vmmdev.h>
#include <pci.h>
#include <io.h>
#include <mm.h>
#include <kern.h>

#define VMM_VENDOR 0x80EE
#define VMM_DEVICE 0xCAFE

#define REQ_VERSION        0x00010001u
#define REQ_GET_MOUSE      1u
#define REQ_SET_MOUSE      2u
#define REQ_REPORT_GUEST   50u
#define GUEST_IFACE_VER    0x00010004u

#define MOUSE_GUEST_CAN_ABSOLUTE 0x01u
#define MOUSE_HOST_WANTS_ABS     0x02u

struct req_header {
    uint32_t size;
    uint32_t version;
    uint32_t type;
    int32_t  rc;
    uint32_t reserved1;
    uint32_t reserved2;
};

struct req_guest_info {
    struct req_header hdr;
    uint32_t          interface_version;
    uint32_t          os_type;
};

struct req_mouse {
    struct req_header hdr;
    uint32_t          features;
    int32_t           x;
    int32_t           y;
};

static struct {
    bool     present;
    uint16_t port;
    paddr_t  page;
    void    *req;
} vm;

static void fill(struct req_header *h, uint32_t size, uint32_t type)
{
    memset(h, 0, size);
    h->size = size;
    h->version = REQ_VERSION;
    h->type = type;
}

static int submit(void)
{
    struct req_header *h = vm.req;

    h->rc = -1;
    outl(vm.port, (uint32_t)vm.page);
    __asm__ volatile("" ::: "memory");
    return ((volatile struct req_header *)h)->rc;
}

static bool set_mouse(uint32_t features)
{
    struct req_mouse *r = vm.req;

    fill(&r->hdr, sizeof(*r), REQ_SET_MOUSE);
    r->features = features;
    return submit() >= 0;
}

void vmmdev_init(void)
{
    struct pci_dev d;
    struct req_guest_info *g;

    if (!pci_find(VMM_VENDOR, VMM_DEVICE, &d) || !(d.bar[0] & 1))
        return;
    vm.page = pmm_alloc();
    if (!vm.page)
        return;
    if (vm.page >> 32) {
        pmm_free(vm.page);
        return;
    }
    pci_enable_io_master(&d);
    vm.port = (uint16_t)(d.bar[0] & ~3u);
    vm.req = P2V(vm.page);
    g = vm.req;
    fill(&g->hdr, sizeof(*g), REQ_REPORT_GUEST);
    g->interface_version = GUEST_IFACE_VER;
    g->os_type = 0;
    submit();
    if (!set_mouse(MOUSE_GUEST_CAN_ABSOLUTE)) {
        pmm_free(vm.page);
        vm.page = 0;
        return;
    }
    vm.present = true;
    kprintf("[ok] vmmdev: absolute pointer\n");
}

bool vmmdev_pointer(uint32_t *x, uint32_t *y)
{
    struct req_mouse *r = vm.req;

    if (!vm.present)
        return false;
    fill(&r->hdr, sizeof(*r), REQ_GET_MOUSE);
    r->features = 0;
    if (submit() < 0 || !(r->features & MOUSE_HOST_WANTS_ABS))
        return false;
    *x = (uint32_t)r->x & 0xFFFF;
    *y = (uint32_t)r->y & 0xFFFF;
    return true;
}
