#include <mouse.h>
#include <vmmdev.h>
#include <cpu.h>
#include <io.h>
#include <kern.h>
#include <proc.h>
#include <uaccess.h>
#include <sys/errno.h>
#include <sys/mouse.h>
#include <sys/termios.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define ST_OUT 0x01
#define ST_IN  0x02
#define ST_AUX 0x20

#define CMD_READ_CFG    0x20
#define CMD_WRITE_CFG   0x60
#define CMD_AUX_DISABLE 0xA7
#define CMD_AUX_ENABLE  0xA8
#define CMD_AUX_PREFIX  0xD4

#define CFG_AUX_IRQ       0x02
#define CFG_AUX_CLOCK_OFF 0x20

#define MS_RESET    0xFF
#define MS_RESEND   0xFE
#define MS_ACK      0xFA
#define MS_DEFAULTS 0xF6
#define MS_ENABLE   0xF4
#define MS_SAMPLE   0xF3
#define MS_GETID    0xF2
#define MS_BAT_OK   0xAA

#define MS_ID_WHEEL 3

#define SPIN_SHORT 200000u
#define SPIN_RESET 3000000u
#define SPIN_FLUSH 64u
#define RING_SIZE  128u
#define PACKET_MAX 4u
#define MOUSE_IRQ  12

static struct {
    bool               present;
    bool               wheel;
    bool               overrun_pending;
    uint8_t            id;
    uint8_t            buttons;
    unsigned           size;
    unsigned           len;
    unsigned           head;
    unsigned           count;
    uint32_t           overruns;
    uint64_t           last_tick;
    uint8_t            pkt[PACKET_MAX];
    struct mouse_event ring[RING_SIZE];
} ms;

static bool wait_in_clear(void)
{
    unsigned i;

    for (i = 0; i < SPIN_SHORT; i++)
        if (!(inb(PS2_STATUS) & ST_IN))
            return true;
    return false;
}

static bool ctrl_cmd(uint8_t cmd)
{
    if (!wait_in_clear())
        return false;
    outb(PS2_CMD, cmd);
    return true;
}

static bool ctrl_data(uint8_t data)
{
    if (!wait_in_clear())
        return false;
    outb(PS2_DATA, data);
    return true;
}

static bool read_byte(unsigned spins, uint8_t *out)
{
    unsigned i;

    for (i = 0; i < spins; i++)
        if (inb(PS2_STATUS) & ST_OUT) {
            *out = inb(PS2_DATA);
            return true;
        }
    return false;
}

static void flush_output(void)
{
    unsigned i;

    for (i = 0; i < SPIN_FLUSH && (inb(PS2_STATUS) & ST_OUT); i++)
        inb(PS2_DATA);
}

static bool read_cfg(uint8_t *cfg)
{
    return ctrl_cmd(CMD_READ_CFG) && read_byte(SPIN_SHORT, cfg);
}

static bool write_cfg(uint8_t cfg)
{
    return ctrl_cmd(CMD_WRITE_CFG) && ctrl_data(cfg);
}

static bool mouse_send(uint8_t byte)
{
    unsigned tries;
    uint8_t reply;

    for (tries = 0; tries < 3; tries++) {
        if (!ctrl_cmd(CMD_AUX_PREFIX) || !ctrl_data(byte))
            return false;
        if (!read_byte(SPIN_SHORT, &reply))
            return false;
        if (reply == MS_ACK)
            return true;
        if (reply != MS_RESEND)
            return false;
    }
    return false;
}

static bool mouse_send_arg(uint8_t cmd, uint8_t arg)
{
    return mouse_send(cmd) && mouse_send(arg);
}

static bool mouse_reset(void)
{
    uint8_t reply;

    if (!mouse_send(MS_RESET))
        return false;
    if (!read_byte(SPIN_RESET, &reply) || reply != MS_BAT_OK)
        return false;
    return read_byte(SPIN_SHORT, &reply);
}

static bool mouse_read_id(uint8_t *id)
{
    return mouse_send(MS_GETID) && read_byte(SPIN_SHORT, id);
}

static bool mouse_enter_wheel_mode(void)
{
    uint8_t id;

    if (!mouse_send_arg(MS_SAMPLE, 200) || !mouse_send_arg(MS_SAMPLE, 100) || !mouse_send_arg(MS_SAMPLE, 80))
        return false;
    return mouse_read_id(&id) && id == MS_ID_WHEEL;
}

static short clamp16(int v)
{
    if (v > 32767)
        return 32767;
    if (v < -32768)
        return -32768;
    return (short)v;
}

static void push(int dx, int dy, int dz, uint8_t buttons)
{
    struct mouse_event *ev;

    if (!dx && !dy && !dz && buttons == ms.buttons)
        return;
    ms.buttons = buttons;
    if (ms.count) {
        ev = &ms.ring[(ms.head + ms.count - 1) % RING_SIZE];
        if (ev->buttons == buttons && !ev->dz && !dz) {
            ev->dx = clamp16(ev->dx + dx);
            ev->dy = clamp16(ev->dy + dy);
            proc_wakeup(&ms);
            return;
        }
    }
    if (ms.count == RING_SIZE) {
        ms.overruns++;
        ms.overrun_pending = true;
        return;
    }
    ev = &ms.ring[(ms.head + ms.count) % RING_SIZE];
    ev->dx = clamp16(dx);
    ev->dy = clamp16(dy);
    ev->dz = clamp16(dz);
    ev->buttons = buttons;
    ev->flags = ms.overrun_pending ? MOUSE_FLAG_OVERRUN : 0;
    ms.overrun_pending = false;
    ms.count++;
    proc_wakeup(&ms);
}

static void decode(void)
{
    uint8_t b0 = ms.pkt[0];
    int dx, dy, dz = 0;

    if (b0 & 0xC0)
        return;
    dx = ms.pkt[1] - (int)((b0 << 4) & 0x100);
    dy = ms.pkt[2] - (int)((b0 << 3) & 0x100);
    if (ms.size == 4) {
        dz = ms.pkt[3] & 0x0F;
        if (dz & 8)
            dz -= 16;
    }
    push(dx, -dy, dz, b0 & 7);
}

void mouse_byte(uint8_t b)
{
    uint64_t now;

    if (!ms.present)
        return;
    now = pit_ticks();
    if (ms.len && now - ms.last_tick > pit_hz() / 10)
        ms.len = 0;
    ms.last_tick = now;
    if (ms.len == 0 && !(b & 0x08))
        return;
    ms.pkt[ms.len++] = b;
    if (ms.len == ms.size) {
        ms.len = 0;
        decode();
    }
}

static void mouse_irq(struct trapframe *tf)
{
    (void)tf;

    while ((inb(PS2_STATUS) & (ST_OUT | ST_AUX)) == (ST_OUT | ST_AUX))
        mouse_byte(inb(PS2_DATA));
}

bool mouse_present(void)
{
    return ms.present;
}

static void aux_off(uint8_t cfg)
{
    write_cfg((uint8_t)((cfg | CFG_AUX_CLOCK_OFF) & ~CFG_AUX_IRQ));
    ctrl_cmd(CMD_AUX_DISABLE);
}

void mouse_init(void)
{
    uint8_t cfg;

    flush_output();
    if (!ctrl_cmd(CMD_AUX_ENABLE) || !read_cfg(&cfg))
        return;
    if (!write_cfg((uint8_t)(cfg & ~(CFG_AUX_CLOCK_OFF | CFG_AUX_IRQ)))) {
        aux_off(cfg);
        return;
    }
    if (!mouse_reset() || !mouse_send(MS_DEFAULTS)) {
        aux_off(cfg);
        return;
    }
    ms.wheel = mouse_enter_wheel_mode();
    ms.id = ms.wheel ? MS_ID_WHEEL : 0;
    ms.size = ms.wheel ? 4 : 3;
    if (!mouse_send_arg(MS_SAMPLE, 100) || !mouse_send(MS_ENABLE)) {
        aux_off(cfg);
        return;
    }
    flush_output();
    ms.len = 0;
    ms.present = true;
    irq_register(MOUSE_IRQ, mouse_irq);
    write_cfg((uint8_t)((cfg & ~CFG_AUX_CLOCK_OFF) | CFG_AUX_IRQ));
    kprintf("[ok] mouse: ps/2%s\n", ms.wheel ? " + wheel" : "");
}

static long mouse_read(struct vnode *vn, uint64_t off, void *buf, size_t n)
{
    size_t want = n / sizeof(struct mouse_event), done = 0;
    struct mouse_event *out = buf;

    (void)vn;
    (void)off;
    if (!want)
        return -EINVAL;
    while (!ms.count) {
        if (curproc->pending_sig)
            return -EINTR;
        proc_wait_on(&ms);
    }
    while (done < want && ms.count) {
        out[done++] = ms.ring[ms.head];
        ms.head = (ms.head + 1) % RING_SIZE;
        ms.count--;
    }
    return (long)(done * sizeof(struct mouse_event));
}

static long mouse_ioctl(struct vnode *vn, uint64_t req, uint64_t arg)
{
    struct mouse_info mi;
    struct mouse_abs ab;
    uint32_t ax, ay;
    int avail;

    (void)vn;
    switch (req) {
    case FIONREAD:
        avail = (int)(ms.count * sizeof(struct mouse_event));
        return copy_to_user(arg, &avail, sizeof(avail));
    case NIR_MOUSEINFO:
        mi.present = ms.present;
        mi.id = ms.id;
        mi.wheel = ms.wheel;
        mi.overruns = ms.overruns;
        return copy_to_user(arg, &mi, sizeof(mi));
    case NIR_MOUSEABS:
        ab.valid = vmmdev_pointer(&ax, &ay);
        ab.x = ab.valid ? ax : 0;
        ab.y = ab.valid ? ay : 0;
        return copy_to_user(arg, &ab, sizeof(ab));
    default:
        return -ENOTTY;
    }
}

static const struct vnops mouse_ops = {
    .read = mouse_read,
    .ioctl = mouse_ioctl,
};

struct vnode mouse_vnode = {
    .type = VCHR,
    .ops = &mouse_ops,
    .mode = 020600,
    .name = "mouse",
};
