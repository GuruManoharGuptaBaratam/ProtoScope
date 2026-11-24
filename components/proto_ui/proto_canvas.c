#include "proto_canvas.h"

#include <string.h>

extern const uint8_t proto_font5x7[95][5];

void proto_canvas_init(proto_canvas_t *c, uint16_t width, uint16_t height, uint8_t *pixels)
{
    if (!c) { return; }
    c->width = width;
    c->height = height;
    c->pixels = pixels;
}

void proto_canvas_clear(proto_canvas_t *c, uint8_t color)
{
    if (c && c->pixels) { memset(c->pixels, color, (size_t)c->width * c->height); }
}

void proto_canvas_pixel(proto_canvas_t *c, int x, int y, uint8_t color)
{
    if (!c || !c->pixels || x < 0 || y < 0 || x >= c->width || y >= c->height) { return; }
    c->pixels[(size_t)y * c->width + (size_t)x] = color;
}

void proto_canvas_fill_rect(proto_canvas_t *c, int x, int y, int w, int h, uint8_t color)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) { proto_canvas_pixel(c, xx, yy, color); }
    }
}

void proto_canvas_rect(proto_canvas_t *c, int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) { return; }
    for (int xx = x; xx < x + w; ++xx) {
        proto_canvas_pixel(c, xx, y, color);
        proto_canvas_pixel(c, xx, y + h - 1, color);
    }
    for (int yy = y; yy < y + h; ++yy) {
        proto_canvas_pixel(c, x, yy, color);
        proto_canvas_pixel(c, x + w - 1, yy, color);
    }
}

int proto_canvas_text(proto_canvas_t *c, int x, int y, const char *text, uint8_t fg, uint8_t bg)
{
    if (!c || !text) { return x; }
    for (const char *p = text; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 32 || ch > 126) { ch = '?'; }
        const uint8_t *glyph = proto_font5x7[ch - 32];
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                proto_canvas_pixel(c, x + col, y + row, (glyph[col] >> row) & 1 ? fg : bg);
            }
            proto_canvas_pixel(c, x + col, y + 7, bg);
        }
        for (int row = 0; row < 8; ++row) { proto_canvas_pixel(c, x + 5, y + row, bg); }
        x += PROTO_CELL_W;
    }
    return x;
}

uint16_t proto_canvas_columns(const proto_canvas_t *c)
{
    return c ? (uint16_t)(c->width / PROTO_CELL_W) : 0;
}

uint16_t proto_canvas_rows(const proto_canvas_t *c)
{
    return c ? (uint16_t)(c->height / PROTO_CELL_H) : 0;
}

void proto_canvas_text_row(proto_canvas_t *c, uint16_t row, const char *text, bool inverted, bool accent)
{
    if (!c || row >= proto_canvas_rows(c)) { return; }
    const uint8_t on = accent ? PROTO_CANVAS_ACCENT : PROTO_CANVAS_FG;
    const uint8_t fg = inverted ? PROTO_CANVAS_BG : on;
    const uint8_t bg = inverted ? on : PROTO_CANVAS_BG;
    const int y = row * PROTO_CELL_H;
    proto_canvas_fill_rect(c, 0, y, c->width, PROTO_CELL_H, bg);
    if (text) {
        /* Clip to whole columns so a long line never wraps or half-draws a glyph. */
        char clipped[64];
        const size_t max = proto_canvas_columns(c) < sizeof(clipped) - 1 ? proto_canvas_columns(c) : sizeof(clipped) - 1;
        strncpy(clipped, text, max);
        clipped[max] = '\0';
        proto_canvas_text(c, 0, y, clipped, fg, bg);
    }
}

size_t proto_canvas_read_row(const proto_canvas_t *c, uint16_t row, char *out, size_t out_size)
{
    if (!out || out_size == 0) { return 0; }
    out[0] = '\0';
    if (!c || !c->pixels || row >= proto_canvas_rows(c)) { return 0; }
    size_t n = 0;
    for (uint16_t col = 0; col < proto_canvas_columns(c) && n + 1 < out_size; ++col) {
        const int x0 = col * PROTO_CELL_W, y0 = row * PROTO_CELL_H;
        const uint8_t bg = c->pixels[(size_t)(y0 + 7) * c->width + (size_t)x0];   /* bottom line is always background */
        char found = '?';
        for (int ch = 32; ch < 127; ++ch) {
            bool match = true;
            for (int cx = 0; cx < 5 && match; ++cx) {
                for (int ry = 0; ry < 7; ++ry) {
                    const bool on = c->pixels[(size_t)(y0 + ry) * c->width + (size_t)(x0 + cx)] != bg;
                    if (on != (((proto_font5x7[ch - 32][cx] >> ry) & 1) != 0)) { match = false; break; }
                }
            }
            if (match) { found = (char)ch; break; }
        }
        out[n++] = found;
    }
    while (n > 0 && out[n - 1] == ' ') { n--; }
    out[n] = '\0';
    return n;
}

void proto_canvas_pack_page(const proto_canvas_t *c, uint8_t page, uint8_t *out)
{
    if (!c || !c->pixels || !out) { return; }
    for (uint16_t x = 0; x < c->width; ++x) {
        uint8_t b = 0;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const size_t y = (size_t)page * 8 + bit;
            if (y < c->height && c->pixels[y * c->width + x] != PROTO_CANVAS_BG) { b |= (uint8_t)(1u << bit); }
        }
        out[x] = b;
    }
}
