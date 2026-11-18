#include <tty.h>
#include <cpu.h>
#include <io.h>

#define KBD_DATA   0x60
#define KBD_STATUS 0x64
#define KBD_CMD    0x64

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CTRL   0x1D
#define SC_CAPS   0x3A
#define SC_COUNT  58

static const char normal_map[SC_COUNT + 1] =
    "\0\033" "1234567890-=\177\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
static const char shift_map[SC_COUNT + 1] =
    "\0\033" "!@#$%^&*()_+\177\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static bool shift, ctrl, caps, extended;

static bool wait_input_clear(void)
{
    int i;

    for (i = 0; i < 100000; i++)
        if (!(inb(KBD_STATUS) & 2))
            return true;
    return false;
}

static bool wait_output_full(void)
{
    int i;

    for (i = 0; i < 100000; i++)
        if (inb(KBD_STATUS) & 1)
            return true;
    return false;
}

static void kbd_irq(struct trapframe *tf)
{
    (void)tf;

    while (inb(KBD_STATUS) & 1) {
        uint8_t sc = inb(KBD_DATA);
        uint8_t code = sc & 0x7F;
        bool release = sc & 0x80;
        char c;

        if (sc == 0xE0) {
            extended = true;
            continue;
        }
        if (extended) {
            extended = false;
            continue;
        }
        if (code == SC_LSHIFT || code == SC_RSHIFT) {
            shift = !release;
            continue;
        }
        if (code == SC_CTRL) {
            ctrl = !release;
            continue;
        }
        if (release)
            continue;
        if (code == SC_CAPS) {
            caps = !caps;
            continue;
        }
        if (code >= SC_COUNT)
            continue;

        c = normal_map[code];
        if (c >= 'a' && c <= 'z' && caps)
            c = shift ? normal_map[code] : shift_map[code];
        else if (shift)
            c = shift_map[code];
        if (!c)
            continue;
        if (ctrl) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
                c &= 0x1F;
            else if (c == ' ')
                c = 0;
        }
        tty_input((uint8_t)c);
    }
}

void kbd_init(void)
{
    uint8_t cfg;

    while (inb(KBD_STATUS) & 1)
        inb(KBD_DATA);

    if (wait_input_clear()) {
        outb(KBD_CMD, 0x20);
        if (wait_output_full()) {
            cfg = inb(KBD_DATA);
            cfg |= 1;
            if (wait_input_clear()) {
                outb(KBD_CMD, 0x60);
                if (wait_input_clear())
                    outb(KBD_DATA, cfg);
            }
        }
    }
    irq_register(1, kbd_irq);
}
