#include <stdint.h>

// This include is dodgy, but brings in enough of the runtime.
#include "dvi.h"

#include "gen.h"
#include "text.h"
#include "font.h"

Run run_heap[8192];
uint run_heap_ix = 0;

void setup_text_line(struct text_line *line, const char *text, uint max_w) {
    line->runs = &run_heap[run_heap_ix];
    uint x = 0;
    char c;
    for (uint j = 0; x < max_w && (c = text[j]) != 0; j++) {
        uint glyph = c - 32;
        uint width = font_widths[glyph];
        if (x + width > max_w) {
            width = max_w - x;
        }
        uint offset = font_x_offsets[glyph];
        Run run = {
            .input = font_bits + offset / 32,
            .stride = FONT_STRIDE,
            .bit_offset = offset & 31,
            .count = width
        };
        run_heap[run_heap_ix++] = run;
        x += width;
    }
    // space pad the end; this will be better when we have
    // a virtual machine
    while (x & 31) {
        uint glyph = 0;
        uint width = font_widths[glyph];
        if ((x & 31) + width > 32) {
            width = 32 - (x & 31);
        }
        uint offset = font_x_offsets[glyph];
        Run run = {
            .input = font_bits + offset / 32,
            .stride = FONT_STRIDE,
            .bit_offset = offset & 31,
            .count = width
        };
        run_heap[run_heap_ix++] = run;
        x += width;
    }
    line->width = x;
    Run empty = { .input = NULL };
    run_heap[run_heap_ix++] = empty;
}

#define LEADING 5
#define LINE_SPACING (FONT_HEIGHT + LEADING)

void render_text_scanline(uint32_t *outp, Run *runs, uint y) {
    uint y_line = y % LINE_SPACING;
    if (y_line < FONT_HEIGHT) {
        process_scanline(runs, y_line, outp);
    }
}
