void init_expand_lut();
void expand_1_to_4(uint8_t *src, uint32_t *dst, uint n);
void apply_colors(uint32_t *src, uint32_t *dst, uint32_t fg, uint32_t bg, uint n);

void make_1bpp_pal(uint32_t pal[16], uint bg, uint fg);

typedef struct Run {
    // NULL for sentinel
    const uint32_t *input;
    // stride in bytes (must be multiple of 4)
    uint32_t stride;
    uint16_t bit_offset;
    uint16_t count;
} Run;

void process_scanline(const Run *runs, uint32_t y, uint32_t *outp);
