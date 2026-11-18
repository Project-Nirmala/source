#include <fbcon.h>
#include <limine.h>
#include <mm.h>
#include <kern.h>

#define EARLY_SIZE 4096

static uint32_t *fb;
static uint32_t *shadow;
static uint32_t  stride;
static uint32_t  width, height;
static int       cols, rows, cx, cy, saved_cx, saved_cy;
static uint8_t   rshift, gshift, bshift, rsize, gsize, bsize;
static bool      ready, cursor_on = true, cursor_drawn, reverse, bold;
static uint32_t  fg_idx = 7, bg_idx = 0;

static int      esc_state;
static int      params[4], nparams;
static bool     private_csi;

static char     early[EARLY_SIZE];
static size_t   early_len;

static const uint8_t palette[16][3] = {
    { 0, 0, 0 }, { 170, 0, 0 }, { 0, 170, 0 }, { 170, 85, 0 },
    { 0, 0, 170 }, { 170, 0, 170 }, { 0, 170, 170 }, { 170, 170, 170 },
    { 85, 85, 85 }, { 255, 85, 85 }, { 85, 255, 85 }, { 255, 255, 85 },
    { 85, 85, 255 }, { 255, 85, 255 }, { 85, 255, 255 }, { 255, 255, 255 },
};

static uint32_t pack(uint32_t idx)
{
    const uint8_t *c = palette[idx & 15];

    return (uint32_t)(c[0] >> (8 - rsize)) << rshift |
           (uint32_t)(c[1] >> (8 - gsize)) << gshift |
           (uint32_t)(c[2] >> (8 - bsize)) << bshift;
}

static uint32_t cur_fg(void)
{
    uint32_t i = fg_idx;

    if (bold && i < 8)
        i += 8;
    return pack(reverse ? bg_idx : i);
}

static uint32_t cur_bg(void)
{
    return pack(reverse ? (bold && fg_idx < 8 ? fg_idx + 8 : fg_idx) : bg_idx);
}

static void blit(uint32_t y0, uint32_t y1)
{
    memcpy(fb + (size_t)y0 * stride, shadow + (size_t)y0 * stride,
           (size_t)(y1 - y0) * stride * 4);
}

static void fill_cells(int x0, int y0, int x1, int y1)
{
    uint32_t bg = cur_bg(), y, x;

    if (x0 >= x1 || y0 >= y1)
        return;
    for (y = (uint32_t)y0 * FONT_HEIGHT; y < (uint32_t)y1 * FONT_HEIGHT; y++) {
        uint32_t *row = shadow + (size_t)y * stride;

        for (x = (uint32_t)x0 * FONT_WIDTH; x < (uint32_t)x1 * FONT_WIDTH; x++)
            row[x] = bg;
        memcpy(fb + (size_t)y * stride + (uint32_t)x0 * FONT_WIDTH, row + (uint32_t)x0 * FONT_WIDTH,
               (size_t)(x1 - x0) * FONT_WIDTH * 4);
    }
}

static void draw_glyph(int col, int row, char c)
{
    const uint8_t *g = font8x16[(uint8_t)c >= 0x20 && (uint8_t)c < 0x7F ? (uint8_t)c - 0x20 : '?' - 0x20];
    uint32_t fgc = cur_fg(), bgc = cur_bg(), y, x;

    for (y = 0; y < FONT_HEIGHT; y++) {
        size_t off = (size_t)((uint32_t)row * FONT_HEIGHT + y) * stride + (uint32_t)col * FONT_WIDTH;

        for (x = 0; x < FONT_WIDTH; x++)
            shadow[off + x] = (g[y] & (0x80 >> x)) ? fgc : bgc;
        memcpy(fb + off, shadow + off, FONT_WIDTH * 4);
    }
}

static void toggle_cursor(void)
{
    int col = cx < cols ? cx : cols - 1;
    uint32_t y, x, mask = pack(15);

    for (y = (uint32_t)cy * FONT_HEIGHT + FONT_HEIGHT - 2; y < (uint32_t)(cy + 1) * FONT_HEIGHT; y++) {
        size_t off = (size_t)y * stride + (uint32_t)col * FONT_WIDTH;

        for (x = 0; x < FONT_WIDTH; x++)
            shadow[off + x] ^= mask;
        memcpy(fb + off, shadow + off, FONT_WIDTH * 4);
    }
    cursor_drawn = !cursor_drawn;
}

static void scroll_up(void)
{
    uint32_t used = (uint32_t)rows * FONT_HEIGHT;

    memmove(shadow, shadow + (size_t)FONT_HEIGHT * stride, (size_t)(used - FONT_HEIGHT) * stride * 4);
    {
        uint32_t bg = cur_bg(), y, x;

        for (y = used - FONT_HEIGHT; y < used; y++)
            for (x = 0; x < width; x++)
                shadow[(size_t)y * stride + x] = bg;
    }
    blit(0, used);
}

static void newline(void)
{
    if (++cy >= rows) {
        cy = rows - 1;
        scroll_up();
    }
}

static void clamp(void)
{
    if (cx < 0)
        cx = 0;
    if (cx > cols)
        cx = cols;
    if (cy < 0)
        cy = 0;
    if (cy >= rows)
        cy = rows - 1;
}

static int param(int i, int def)
{
    return i < nparams && params[i] > 0 ? params[i] : def;
}

static void erase_display(int mode)
{
    if (mode == 0) {
        fill_cells(cx < cols ? cx : cols, cy, cols, cy + 1);
        fill_cells(0, cy + 1, cols, rows);
    } else if (mode == 1) {
        fill_cells(0, 0, cols, cy);
        fill_cells(0, cy, (cx < cols ? cx : cols - 1) + 1, cy + 1);
    } else {
        fill_cells(0, 0, cols, rows);
    }
}

static void erase_line(int mode)
{
    if (mode == 0)
        fill_cells(cx < cols ? cx : cols, cy, cols, cy + 1);
    else if (mode == 1)
        fill_cells(0, cy, (cx < cols ? cx : cols - 1) + 1, cy + 1);
    else
        fill_cells(0, cy, cols, cy + 1);
}

static void sgr(void)
{
    int i;

    if (nparams == 0) {
        reverse = bold = false;
        fg_idx = 7;
        bg_idx = 0;
        return;
    }
    for (i = 0; i < nparams; i++) {
        int p = params[i];

        if (p == 0) {
            reverse = bold = false;
            fg_idx = 7;
            bg_idx = 0;
        } else if (p == 1) {
            bold = true;
        } else if (p == 22) {
            bold = false;
        } else if (p == 7) {
            reverse = true;
        } else if (p == 27) {
            reverse = false;
        } else if (p >= 30 && p <= 37) {
            fg_idx = (uint32_t)(p - 30);
        } else if (p == 39) {
            fg_idx = 7;
        } else if (p >= 40 && p <= 47) {
            bg_idx = (uint32_t)(p - 40);
        } else if (p == 49) {
            bg_idx = 0;
        } else if (p >= 90 && p <= 97) {
            fg_idx = (uint32_t)(p - 90 + 8);
        }
    }
}

static void csi(char final)
{
    int n = param(0, 1);

    if (private_csi) {
        int mode = nparams ? params[0] : 0;

        if (final == 'h' || final == 'l') {
            if (mode == 25) {
                cursor_on = final == 'h';
            } else if (mode == 1049 || mode == 1047 || mode == 47) {
                erase_display(2);
                cx = cy = 0;
            }
        }
        return;
    }
    switch (final) {
    case 'H':
    case 'f':
        cy = param(0, 1) - 1;
        cx = param(1, 1) - 1;
        break;
    case 'A': cy -= n; break;
    case 'B': cy += n; break;
    case 'C': cx += n; break;
    case 'D': cx -= n; break;
    case 'G': cx = n - 1; break;
    case 'd': cy = n - 1; break;
    case 'J': erase_display(nparams ? params[0] : 0); break;
    case 'K': erase_line(nparams ? params[0] : 0); break;
    case 'm': sgr(); break;
    case 's': saved_cx = cx; saved_cy = cy; break;
    case 'u': cx = saved_cx; cy = saved_cy; break;
    default: break;
    }
    clamp();
}

static void feed(char c)
{
    if (esc_state == 1) {
        if (c == '[') {
            esc_state = 2;
            nparams = 0;
            params[0] = params[1] = params[2] = params[3] = 0;
            private_csi = false;
            return;
        }
        esc_state = 0;
        if (c == '7') {
            saved_cx = cx;
            saved_cy = cy;
        } else if (c == '8') {
            cx = saved_cx;
            cy = saved_cy;
        }
        return;
    }
    if (esc_state == 2) {
        if (c == '?') {
            private_csi = true;
        } else if (c >= '0' && c <= '9') {
            if (nparams == 0)
                nparams = 1;
            if (nparams <= 4)
                params[nparams - 1] = params[nparams - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (nparams == 0)
                nparams = 1;
            if (nparams < 4)
                nparams++;
        } else if (c >= 0x40 && c <= 0x7E) {
            esc_state = 0;
            csi(c);
        } else if (c < 0x20 || c > 0x7E) {
            esc_state = 0;
        }
        return;
    }
    switch (c) {
    case 0x1B:
        esc_state = 1;
        break;
    case '\n':
        newline();
        break;
    case '\r':
        cx = 0;
        break;
    case '\b':
        if (cx > 0)
            cx--;
        break;
    case '\t':
        cx = (cx + 8) & ~7;
        if (cx >= cols)
            cx = cols - 1;
        break;
    default:
        if ((uint8_t)c < 0x20 || (uint8_t)c == 0x7F)
            break;
        if (cx >= cols) {
            cx = 0;
            newline();
        }
        draw_glyph(cx, cy, c);
        cx++;
        break;
    }
}

void fbcon_putc(char c)
{
    if (!ready) {
        if (early_len < EARLY_SIZE)
            early[early_len++] = c;
        return;
    }
    if (cursor_drawn)
        toggle_cursor();
    feed(c);
    if (cursor_on)
        toggle_cursor();
    __asm__ volatile("sfence" ::: "memory");
}

void fbcon_init(struct limine_framebuffer_response *resp)
{
    struct limine_framebuffer *f;
    uint64_t bytes, pa;
    size_t i, n;
    char saved[EARLY_SIZE];

    if (!resp || resp->framebuffer_count == 0)
        return;
    f = resp->framebuffers[0];
    if (f->bpp != 32 || f->width < FONT_WIDTH * 20 || f->height < FONT_HEIGHT * 5)
        return;
    bytes = f->pitch * f->height;
    pa = (uint64_t)(uintptr_t)f->address - hhdm_offset;
    fb = vmm_map_mmio_wc(pa, bytes);
    shadow = kzalloc(bytes);
    if (!shadow)
        return;
    stride = (uint32_t)(f->pitch / 4);
    width = (uint32_t)f->width;
    height = (uint32_t)f->height;
    rsize = f->red_mask_size;
    rshift = f->red_mask_shift;
    gsize = f->green_mask_size;
    gshift = f->green_mask_shift;
    bsize = f->blue_mask_size;
    bshift = f->blue_mask_shift;
    cols = (int)(width / FONT_WIDTH);
    rows = (int)(height / FONT_HEIGHT);
    cx = cy = 0;
    fill_cells(0, 0, cols, rows);
    ready = true;

    n = early_len;
    memcpy(saved, early, n);
    early_len = 0;
    for (i = 0; i < n; i++)
        fbcon_putc(saved[i]);
}
