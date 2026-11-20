#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fbcon.h>
#include <limine.h>
#include <mm.h>

uint64_t hhdm_offset;
static uint32_t *screen;
void *vmm_map_mmio(uint64_t pa, size_t len) { (void)pa; (void)len; return screen; }
void *vmm_map_mmio_wc(uint64_t pa, size_t len) { (void)pa; (void)len; return screen; }
void *kzalloc(size_t n) { return calloc(1, n); }
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void kprintf(const char *f, ...) { (void)f; }
void panic(const char *f, ...) { (void)f; abort(); }

#define W 640
#define H 400
#define FG 0x00AAAAAAu
#define BG 0x00000000u
static int fails;
#define T(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); fails++; } } while (0)
static void puts_(const char *s) { while (*s) fbcon_putc(*s++); }

static int cell_matches(int col, int row, char c, uint32_t fg, uint32_t bg)
{
    const uint8_t *g = font8x16[c - 0x20];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 8; x++) {
            uint32_t want = (g[y] & (0x80 >> x)) ? fg : bg;
            if (screen[(row * 16 + y) * W + col * 8 + x] != want) return 0;
        }
    return 1;
}
static int cell_blank(int col, int row)
{
    for (int y = 0; y < 14; y++) for (int x = 0; x < 8; x++) if (screen[(row * 16 + y) * W + col * 8 + x] != BG) return 0;
    return 1;
}
static void dump(int rows, int cols)
{
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int on = 0; for (int y = 0; y < 14; y++) for (int x = 0; x < 8; x++) if (screen[(r * 16 + y) * W + c * 8 + x] != BG) on++;
            putchar(on == 0 ? ' ' : on > 40 ? '#' : '+');
        }
        putchar('\n');
    }
}

int main(void)
{
    screen = calloc(W * H, 4);
    static struct limine_framebuffer f = { .width = W, .height = H, .pitch = W * 4, .bpp = 32, .red_mask_size = 8, .red_mask_shift = 16,
                                           .green_mask_size = 8, .green_mask_shift = 8, .blue_mask_size = 8, .blue_mask_shift = 0 };
    static struct limine_framebuffer *list[1] = { &f };
    static struct limine_framebuffer_response resp = { .framebuffer_count = 1 };
    f.address = screen; resp.framebuffers = list;

    puts_("early boot line\r\n");
    fbcon_init(&resp);
    T(cell_matches(0, 0, 'e', FG, BG) && cell_matches(14, 0, 'e', FG, BG), "early output replayed after init");

    puts_("\x1b[2J\x1b[H");
    T(cell_blank(0, 0) && cell_blank(14, 0), "ESC[2J clears the screen");
    puts_("Hi!");
    T(cell_matches(0, 0, 'H', FG, BG) && cell_matches(1, 0, 'i', FG, BG) && cell_matches(2, 0, '!', FG, BG), "text drawn from the font");
    puts_("\x1b[3;5HX");
    T(cell_matches(4, 2, 'X', FG, BG), "ESC[3;5H positions the cursor (row 3, col 5)");
    puts_("\x1b[7mR\x1b[0mN");
    T(cell_matches(5, 2, 'R', BG, FG) && cell_matches(6, 2, 'N', FG, BG), "reverse video swaps colours until reset");
    puts_("\x1b[1;5HZZZZ\x1b[1;7H\x1b[K");
    T(cell_matches(4, 0, 'Z', FG, BG) && cell_matches(5, 0, 'Z', FG, BG) && cell_blank(6, 0) && cell_blank(7, 0), "ESC[K erases to end of line");
    puts_("\x1b[2;1Hab\b\b \b");
    T(cell_blank(0, 1), "backspace-space-backspace erases");
    puts_("\x1b[31mred\x1b[0m");
    puts_("\x1b[?25l");
    puts_("\x1b[?25h");

    puts_("\x1b[2J\x1b[H");
    for (int i = 0; i < 30; i++) { char b[8]; snprintf(b, sizeof b, "%02d\r\n", i); puts_(b); }
    T(cell_matches(0, 0, '0', FG, BG) && cell_matches(1, 0, '6', FG, BG), "after scrolling the top row shows line 06");
    T(cell_matches(0, 23, '2', FG, BG) && cell_matches(1, 23, '9', FG, BG), "last printed line is just above the cursor row");
    T(cell_blank(0, 24), "bottom row is blank after the final newline");

    puts_("\x1b[2J\x1b[H");
    for (int i = 0; i < 82; i++) fbcon_putc('a' + i % 26);
    T(cell_matches(79, 0, 'a' + 79 % 26, FG, BG) && cell_matches(0, 1, 'a' + 80 % 26, FG, BG) && cell_matches(1, 1, 'a' + 81 % 26, FG, BG), "text wraps at the right edge");

    puts_("junk\x1b[?1049h");
    T(cell_blank(0, 0) && cell_blank(0, 1), "ESC[?1049h clears the screen");

    puts_("\x1b[2J\x1b[HNirmalaSYS Starting!\r\n[ok] disk: none\r\n");
    dump(3, 36);
    printf(fails ? "fbcon: %d FAILURES\n" : "fbcon: all tests passed\n", fails);
    return fails != 0;
}
