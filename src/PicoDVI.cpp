#include "PicoDVI.h"
#include <stdlib.h>
#include <string.h>

PicoDVI::PicoDVI(uint16_t w, uint16_t h, vreg_voltage v, const struct dvi_timing &t, const struct dvi_serialiser_cfg &c) {
  framebuf_width = w;
  framebuf_height = h;
  timing = &t;
  voltage = v;
  cfg = &c;
}

PicoDVI::~PicoDVI(void) {
  if (framebuf) free(framebuf);
}

bool PicoDVI::begin(void) {
  if ((framebuf = (uint16_t *)malloc(framebuf_width * framebuf_height * sizeof(uint16_t)))) {
    vreg_set_voltage(voltage);
    sleep_ms(10);
#ifdef RUN_FROM_CRYSTAL
    set_sys_clock_khz(12000, true);
#else
    // Run system at TMDS bit clock
    set_sys_clock_khz(timing->bit_clk_khz, true);
#endif

    dvi0.timing = timing;
    //dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    memcpy(&dvi0.ser_cfg, cfg, sizeof dvi0.ser_cfg);
#if 0
    dvi0.scanline_callback = core1_scanline_callback;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
#endif

    return true;
  }
  return false;
}

#if 0
void core1_main() {
        dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
        dvi_start(&dvi0);
        dvi_scanbuf_main_16bpp(&dvi0);
        __builtin_unreachable();
}

void core1_scanline_callback() {
        // Discard any scanline pointers passed back
        uint16_t *bufptr;
        while (queue_try_remove_u32(&dvi0.q_colour_free, &bufptr))
                ;
        // // Note first two scanlines are pushed before DVI start
        static uint scanline = 2;
        bufptr = &framebuf[FRAME_WIDTH * scanline];
        queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
        scanline = (scanline + 1) % FRAME_HEIGHT;
}
#endif
