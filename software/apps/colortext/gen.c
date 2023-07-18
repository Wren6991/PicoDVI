#include <stdint.h>
#include "dvi.h"
#include "gen.h"

uint32_t expand_1_to_4_lut[256];

void init_expand_lut() {
    for (uint i = 0; i < 256; i++) {
        uint32_t out = 0;
        for (uint j = 0; j < 8; j++) {
            if (i & (1 << j)) {
                out |= 15 << (j * 4);
            }
        }
        expand_1_to_4_lut[i] = out;
    }
}

// n is number of bytes of input (number of pixels / 8)
void __not_in_flash("main") expand_1_to_4(uint8_t *src, uint32_t *dst, uint n) {
    for (unsigned int i = 0; i < n; i++) {
        *dst++ = expand_1_to_4_lut[*src++];
    }
}

void __not_in_flash("main") apply_colors(uint32_t *src, uint32_t *dst, uint32_t fg, uint32_t bg, uint n) {
    fg *= 0x11111111;
    bg *= 0x11111111;
    uint32_t mask = fg ^ bg;
    for (uint i = 0; i < n; i++) {
        *dst++ = (*src++ & mask) ^ bg;
    }
}

void __not_in_flash("main") process_scanline(const Run *runs, uint32_t y, uint32_t *outp) {
    uint32_t outw = 0;
    uint32_t outbitix = 0;
    for (;;) {
        const uint32_t *inp = runs->input;
        if (inp == 0) {
            break;
        }
        inp = (const uint32_t *)((const uint8_t *)inp + y * runs->stride);
        int count = runs->count;
        int inbitix = runs->bit_offset;
        runs++;
        uint32_t inw = *inp++;
        outw |= (inw >> inbitix) << outbitix;
        if (inbitix > outbitix) {
            count -= 32 - inbitix;
            outbitix += 32 - inbitix;
            if (count <= 0) {
                outbitix += count;
                outw = (outw << (32 - outbitix)) >> (32 - outbitix);
                continue;
            }
        } else {
            count -= 32 - outbitix;
            if (count >= 0) {
                *outp++ = outw;
                if (count == 0) {
                    outw = 0;
                    outbitix = 0;
                    continue;
                }
                outbitix -= inbitix;
                if (outbitix != 0) {
                    outw = inw >> (32 - outbitix);
                    count -= outbitix;
                    if (count <= 0) {
                        outbitix += count;
                        outw = (outw << (32 - outbitix)) >> (32 - outbitix);
                        continue;
                    }
                } else {
                    outw = 0;
                }
            } else {
                outbitix = 32 + count;
                outw = (outw << (-count)) >> (-count);
                continue;
            }
        }
        if (outbitix == 0) {
            if ((count & 31) == 0) {
                for (;;) {
                    *outp++ = *inp++;
                    count -= 32;
                    if (count == 0) {
                        break;
                    }
                }
                outw = 0;
            } else {
                for (;;) {
                    outw = *inp++;
                    if (count < 32) {
                        outbitix = count;
                        outw = (outw << (32 - outbitix)) >> (32 - outbitix);
                        break;
                    }
                    count -= 32;
                    *outp++ = outw;
                }
            }
        } else {
            uint32_t frac = (count & 31) + outbitix;
            if (frac < 32) {
                for (;;) {
                    inw = *inp++;
                    outw |= inw << outbitix;
                    if (count < 32) {
                        outbitix += count;
                        outw = (outw << (32 - outbitix)) >> (32 - outbitix);
                        break;
                    }
                    count -= 32;
                    *outp++ = outw;
                    outw = inw >> (32 - outbitix);
                }
            } else if (frac == 32) {
                for (;;) {
                    inw = *inp++;
                    outw |= inw << outbitix;
                    *outp++ = outw;
                    count -= 32;
                    if (count < 0) {
                        break;
                    }
                    outw = inw >> (32 - outbitix);
                }
                outw = 0;
                outbitix = 0;
            } else {
                for (;;) {
                    inw = *inp++;
                    outw |= inw << outbitix;
                    *outp++ = outw;
                    outw = inw >> (32 - outbitix);
                    count -= 32;
                    if (count <= 0) {
                        outbitix += count;
                        outw = (outw << (32 - outbitix)) >> (32 - outbitix);
                        break;
                    }
                }
            }
        }
    }
}

extern const uint32_t tmds_table_4bpp[];

void __not_in_flash("main") make_1bpp_pal(uint32_t pal[16], uint bg, uint fg) {
    for (uint chan = 0; chan < 3; chan++) {
        uint bg_g = (bg >> (chan * 4)) & 0xf;
        uint fg_g = (fg >> (chan * 4)) & 0xf;
        pal[chan] = tmds_table_4bpp[bg_g * 0x11];
        pal[chan + 4] = tmds_table_4bpp[bg_g * 0x10 + fg_g];
        pal[chan + 8] = tmds_table_4bpp[fg_g * 0x10 + bg_g];
        pal[chan + 12] = tmds_table_4bpp[fg_g * 0x11];
    }
}
