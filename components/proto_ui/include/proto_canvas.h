#pragma once

/* Tiny framebuffer and text renderer (Phase 9). Pure C. One byte per pixel holding
 * a colour INDEX so the same UI can drive a colour or a monochrome display (the 1-bit OLED
 * shows every non-background index as lit):
 * 0 = background, 1 = foreground, 2 = accent (warnings). Text uses a 5x7 font in a
 * 6x8 cell, so a 128x64 panel is 21 columns x 8 rows and 160x80 is 26 x 10. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_CANVAS_BG 0
#define PROTO_CANVAS_FG 1
#define PROTO_CANVAS_ACCENT 2
#define PROTO_CELL_W 6
#define PROTO_CELL_H 8

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t *pixels;       /* width * height bytes, owned by the caller */
} proto_canvas_t;

void proto_canvas_init(proto_canvas_t *canvas, uint16_t width, uint16_t height, uint8_t *pixels);
void proto_canvas_clear(proto_canvas_t *canvas, uint8_t color);
void proto_canvas_pixel(proto_canvas_t *canvas, int x, int y, uint8_t color);
void proto_canvas_fill_rect(proto_canvas_t *canvas, int x, int y, int w, int h, uint8_t color);
void proto_canvas_rect(proto_canvas_t *canvas, int x, int y, int w, int h, uint8_t color);
/* Draws text at pixel (x, y); returns the x after the last glyph. Unknown characters
 * draw as '?'. Text is clipped at the canvas edge. */
int proto_canvas_text(proto_canvas_t *canvas, int x, int y, const char *text, uint8_t fg, uint8_t bg);
uint16_t proto_canvas_columns(const proto_canvas_t *canvas);
uint16_t proto_canvas_rows(const proto_canvas_t *canvas);
/* Draws one full text row, padded with background to the right edge. inverted swaps
 * the colours (menu highlight). accent selects the accent colour for the foreground. */
/* Reads a rendered text row back by matching glyph shapes (used by the console 'ui screen'
 * command and the host tests). Trailing spaces are trimmed; unknown cells read as '?'.
 * Returns the string length. */
/* Packs one 8-pixel-high page into SSD1306 format: one byte per column, bit 0 = top pixel,
 * any non-background colour index = lit. Writes `width` bytes. */
void proto_canvas_pack_page(const proto_canvas_t *canvas, uint8_t page, uint8_t *out);
size_t proto_canvas_read_row(const proto_canvas_t *canvas, uint16_t row, char *out, size_t out_size);
void proto_canvas_text_row(proto_canvas_t *canvas, uint16_t row, const char *text, bool inverted,
                           bool accent);
