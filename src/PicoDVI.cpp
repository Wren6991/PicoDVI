#include <stdlib.h>
#include <string.h>
#include "PicoDVI.h"
#include "pico/multicore.h"

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

static struct dvi_inst *dviptr = NULL;
static uint16_t *fb = NULL;
static uint16_t fbw = 0, fbh = 0;

static volatile bool foo = true;

//void core1_main() {
void setup1() {
    while(foo); // Wait for begin() to do its thing

    dvi_register_irqs_this_core(dviptr, DMA_IRQ_0);
    dvi_start(dviptr);
    dvi_scanbuf_main_16bpp(dviptr);
    __builtin_unreachable();
}

void core1_scanline_callback() {
    // Discard any scanline pointers passed back
    uint16_t *bufptr;
    while (queue_try_remove_u32(&dviptr->q_colour_free, &bufptr));
    // Note first two scanlines are pushed before DVI start
    static uint scanline = 2;
    bufptr = &fb[fbw * scanline];
    queue_add_blocking_u32(&dviptr->q_colour_valid, &bufptr);
    scanline = (scanline + 1) % fbh;
}


bool PicoDVI::begin(void) {
  if ((framebuf = (uint16_t *)calloc(framebuf_width * framebuf_height, sizeof(uint16_t)))) {
    vreg_set_voltage(voltage);
    sleep_ms(10);
#ifdef RUN_FROM_CRYSTAL
    set_sys_clock_khz(12000, true);
#else
    // Run system at TMDS bit clock
    set_sys_clock_khz(timing->bit_clk_khz, true);
#endif

    dvi0.timing = timing;
    memcpy(&dvi0.ser_cfg, cfg, sizeof dvi0.ser_cfg);
    dvi0.scanline_callback = core1_scanline_callback;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    uint16_t *bufptr = framebuf;
    queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
    bufptr += framebuf_width;
    queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);

#if 0
// This first line is where things go south
    *dviptr = dvi0;
    fb = framebuf;
    fbw = framebuf_width;
    fbh = framebuf_height;
//    multicore_launch_core1(core1_main);
foo = false; // Set core 1 free
#endif

    for (int i=10; i<20; i++) {
        framebuf[i * framebuf_width + i] = 0xF800;
    }

    return true;
  }
  return false;
}

