#include "PicoDVI.h"

// PicoDVI class encapsulates some of the libdvi functionality -------------
// Subclasses then implement specific display types.

static PicoDVI *dviptr = NULL; // For C access to active C++ object
static volatile bool wait_begin = true;

// Runs on core 1 on startup
void setup1(void) {
  while (wait_begin)
    ; // Wait for DVIGFX*::begin() to do its thing on core 0
  dviptr->_setup();
}

// Runs on core 1 after wait_begin released
void PicoDVI::_setup(void) {
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);
  (*mainloop)(&dvi0);
}

PicoDVI::PicoDVI(const struct dvi_timing &t, vreg_voltage v,
         const struct dvi_serialiser_cfg &c)
    : voltage(v) {
  dvi0.timing = &t;
  memcpy(&dvi0.ser_cfg, &c, sizeof dvi0.ser_cfg);
};

PicoDVI::~PicoDVI(void) {
  dviptr = NULL;
  wait_begin = true;
}

void PicoDVI::begin(void) {
  dviptr = this;
  vreg_set_voltage(voltage);
  delay(10);
  set_sys_clock_khz(dvi0.timing->bit_clk_khz, true); // Run at TMDS bit clock
  dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
}

// DVIGFX16 class provides GFX-compatible 16-bit color framebuffer ---------

static void *gfxptr = NULL; // For C access to active C++ object

DVIGFX16::DVIGFX16(const uint16_t w, const uint16_t h,
                   const struct dvi_timing &t, vreg_voltage v,
                   const struct dvi_serialiser_cfg &c)
    : PicoDVI(t, v, c), GFXcanvas16(w, h) {}

static void scanline_callback_GFX16(void) {
  ((DVIGFX16 *)gfxptr)->_scanline_callback();
}

DVIGFX16::~DVIGFX16(void) {
  gfxptr = NULL;
}

void DVIGFX16::_scanline_callback(void) {
  // Discard any scanline pointers passed back
  uint16_t *bufptr;
  while (queue_try_remove_u32(&dvi0.q_colour_free, &bufptr))
    ;
  bufptr = &getBuffer()[WIDTH * scanline];
  queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
  scanline = (scanline + 1) % HEIGHT;
}

bool DVIGFX16::begin(void) {
  uint16_t *bufptr = getBuffer();
  if ((bufptr)) {
    gfxptr = this;
    mainloop = dvi_scanbuf_main_16bpp; // in libdvi
    dvi0.scanline_callback = scanline_callback_GFX16;
    PicoDVI::begin();
    queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
    bufptr += WIDTH;
    queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}
