#include <tty.h>
#include <cpu.h>
#include <io.h>
#include <kbd.h>
#include <sys/errno.h>

#define KBD_DATA   0x60
#define KBD_STATUS 0x64
#define KBD_CMD    0x64

#define SC_LSHIFT 0x2A
#define SC_RSHIFT 0x36
#define SC_CTRL   0x1D
#define SC_CAPS   0x3A
#define SC_COUNT  58

struct keymap {
    const char *name;
    char normal[SC_COUNT + 1], shift[SC_COUNT + 1];
};

/*
 * Scancode set 1, one character per scancode up to SC_COUNT. Layouts are
 * ASCII-only because the console font is.
 */
static const struct keymap keymaps[] = {
    { "us",
      "\0\033" "1234567890-=\177\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ",
      "\0\033" "!@#$%^&*()_+\177\tQWERTYUIOP{}\r\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 " },
    { "dvorak",
      "\0\033" "1234567890[]\177\t',.pyfgcrl/=\r\0aoeuidhtns-`\0\\;qjkxbmwvz\0*\0 ",
      "\0\033" "!@#$%^&*(){}\177\t\"<>PYFGCRL?+\r\0AOEUIDHTNS_~\0|:QJKXBMWVZ\0*\0 " },
    { "colemak",
      "\0\033" "1234567890-=\177\tqwfpgjluy;[]\r\0arstdhneio'`\0\\zxcvbkm,./\0*\0 ",
      "\0\033" "!@#$%^&*()_+\177\tQWFPGJLUY:{}\r\0ARSTDHNEIO\"~\0|ZXCVBKM<>?\0*\0 " },
};

#define NKEYMAPS ((int)(sizeof(keymaps) / sizeof(keymaps[0])))

static const struct keymap *cur_map = &keymaps[0];

int kbd_set_map(const char *name)
{
    int i, j;

    for (i = 0; i < NKEYMAPS; i++) {
        for (j = 0; keymaps[i].name[j] && keymaps[i].name[j] == name[j]; j++)
            ;
        if (!keymaps[i].name[j] && !name[j]) {
            cur_map = &keymaps[i];
            return 0;
        }
    }
    return -ENOENT;
}

static bool shift, ctrl, caps, extended;
static int skip_bytes;

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

static const char *ext_seq(uint8_t code)
{
    switch (code) {
    case 0x48: return "\033[A";
    case 0x50: return "\033[B";
    case 0x4D: return "\033[C";
    case 0x4B: return "\033[D";
    case 0x47: return "\033[1~";
    case 0x4F: return "\033[4~";
    case 0x49: return "\033[5~";
    case 0x51: return "\033[6~";
    case 0x53: return "\033[3~";
    case 0x52: return "\033[2~";
    case 0x1C: return "\r";
    case 0x35: return "/";
    }
    return NULL;
}

static void emit_seq(const char *seq)
{
    while (seq && *seq)
        tty_input((uint8_t)*seq++);
}

static void kbd_irq(struct trapframe *tf)
{
    uint8_t st;

    (void)tf;

    while ((st = inb(KBD_STATUS)) & 1) {
        uint8_t sc = inb(KBD_DATA);
        uint8_t code = sc & 0x7F;
        bool release = sc & 0x80;
        char c;

        if (st & 0x20)
            continue;
        if (skip_bytes) {
            skip_bytes--;
            continue;
        }
        if (sc == 0xE1) {
            skip_bytes = 5;
            extended = false;
            continue;
        }
        if (sc == 0xE0) {
            extended = true;
            continue;
        }
        if (sc == 0xFA || sc == 0xFE || sc == 0xFF)
            continue;
        if (extended) {
            extended = false;
            if (code == SC_CTRL) {
                ctrl = !release;
                continue;
            }
            if (code == SC_LSHIFT || code == SC_RSHIFT)
                continue;
            if (!release)
                emit_seq(ext_seq(code));
            continue;
        }
        if (code >= 0x47 && code <= 0x53 && code != 0x4A && code != 0x4E) {
            if (!release)
                emit_seq(ext_seq(code));
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

        c = cur_map->normal[code];
        if (c >= 'a' && c <= 'z' && caps)
            c = shift ? cur_map->normal[code] : cur_map->shift[code];
        else if (shift)
            c = cur_map->shift[code];
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

    if (wait_input_clear())
        outb(KBD_CMD, 0xAE);

    if (wait_input_clear()) {
        outb(KBD_CMD, 0x20);
        if (wait_output_full()) {
            cfg = inb(KBD_DATA);
            cfg |= 0x41;
            cfg &= (uint8_t)~0x10;
            if (wait_input_clear()) {
                outb(KBD_CMD, 0x60);
                if (wait_input_clear())
                    outb(KBD_DATA, cfg);
            }
        }
    }
    if (wait_input_clear())
        outb(KBD_DATA, 0xF4);
    if (wait_output_full())
        inb(KBD_DATA);
    irq_register(1, kbd_irq);
}
