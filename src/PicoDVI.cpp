#include "PicoDVI.h"

// Some elements of the PicoDVI object must be accessed in interrupts or
// outside the class context, so a pointer to the active PicoDVI object is
// kept. This does mean only one instance can be active, but that's the
// typical use case anyway so we should be OK.
static PicoDVI *dviptr = NULL;
static volatile bool wait_begin = true;

// This runs at startup on core 1
void setup1() {
  while (wait_begin)
    ; // Wait for PicoDVI::begin() to do its thing
  dviptr->_setup();
}

void PicoDVI::_setup(void) {
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);
  dvi_scanbuf_main_16bpp(&dvi0);
}

static void core1_scanline_callback(void) { dviptr->_scanline_callback(); }

void PicoDVI::_scanline_callback(void) {
  // Discard any scanline pointers passed back
  uint16_t *bufptr;
  while (queue_try_remove_u32(&dvi0.q_colour_free, &bufptr))
    ;
  // Note first two scanlines are pushed before DVI start
  static uint scanline = 2;
  bufptr = &getBuffer()[WIDTH * scanline];
  queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
  scanline = (scanline + 1) % HEIGHT;
}

PicoDVI::PicoDVI(uint16_t w, uint16_t h, vreg_voltage v,
                 const struct dvi_timing &t, const struct dvi_serialiser_cfg &c)
    : GFXcanvas16(w, h), timing(&t), voltage(v), cfg(&c) {}

PicoDVI::~PicoDVI(void) { dviptr = NULL; }

bool PicoDVI::begin(void) {
  if (getBuffer()) { // Canvas alloc'd OK?
    vreg_set_voltage(voltage);
    delay(10);
    set_sys_clock_khz(timing->bit_clk_khz, true); // Run at TMDS bit clock

    dvi0.timing = timing;
    memcpy(&dvi0.ser_cfg, cfg, sizeof dvi0.ser_cfg);
    dvi0.scanline_callback = core1_scanline_callback;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    uint16_t *bufptr = getBuffer();
    queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
    bufptr += WIDTH;
    queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);

    dviptr = this;
    wait_begin = false; // Set core 1 free

    return true;
  }
  return false;
}
