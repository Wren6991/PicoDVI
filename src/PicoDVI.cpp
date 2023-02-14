#include "PicoDVI.h"
#include "libdvi/tmds_encode.h"

// PicoDVI class encapsulates some of the libdvi functionality -------------
// Subclasses then implement specific display types.

static struct {
  const dvi_timing &timing;
  vreg_voltage v;
  uint16_t width;
  uint16_t height;
  uint8_t v_rep;
} dvispec[] = {
    {dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, 320, 240, 2},
    {dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_20, 400, 240, 2},
    {dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, 640, 480, 1},
    {dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_20, 800, 480, 1},
    {dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, 640, 240, 2},
    {dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_20, 800, 240, 2},
    // Additional resolutions might get added here if the overclock issue can
    // be sorted out. Regardless, always keep this list 1:1 in sync with the
    // DVIresolution enum in PicoDVI.h.
    {dvi_timing_1280x720p_30hz, VREG_VOLTAGE_1_30, 1280, 720, 1},
};

static PicoDVI *dviptr = NULL; // For C access to active C++ object

// Runs on core 1 on startup
void setup1(void) {
  delay(1); // Required, perhaps core related
  while (dviptr == NULL)
    ; // Wait for PicoDVI::begin() to start on core 0
  dviptr->_setup();
}

// Runs on core 1 after dviptr set
void PicoDVI::_setup(void) {
  while (wait_begin)
    ; // Wait for DVIGFX*::begin() to set this
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);
  (*mainloop)(&dvi0);
}

PicoDVI::PicoDVI(const struct dvi_timing &t, const struct dvi_serialiser_cfg &c,
                 vreg_voltage v)
    : voltage(v) {
  dvi0.timing = &t;
  memcpy(&dvi0.ser_cfg, &c, sizeof dvi0.ser_cfg);
};

PicoDVI::~PicoDVI(void) { dviptr = NULL; }

void PicoDVI::begin(void) {
  dviptr = this;
  vreg_set_voltage(voltage);
  delay(10);
  set_sys_clock_khz(dvi0.timing->bit_clk_khz, true); // Run at TMDS bit clock
  dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
}

// DVIGFX16 class provides GFX-compatible 16-bit color framebuffer ---------

static void *gfxptr = NULL; // For C access to active C++ object

DVIGFX16::DVIGFX16(const DVIresolution r, const struct dvi_serialiser_cfg &c,
                   vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas16(dvispec[r].width, dvispec[r].height) {
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = false;
}

DVIGFX16::~DVIGFX16(void) { gfxptr = NULL; }

static void scanline_callback_GFX16(void) {
  ((DVIGFX16 *)gfxptr)->_scanline_callback();
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

// DVIGFX8 (8-bit, color-indexed framebuffer) is all manner of dirty pool.
// PicoDVI seems to have some palette support but I couldn't grasp the DMA
// stuff going on, so just doing a brute force thing here for now: in
// addition to the 8-bit framebuffer, two 16-bit (RGB565) scanlines are
// allocated...then, in the scanline callback, pixels are mapped from the
// 8-bit framebuffer through the palette into one of these buffers, allowing
// use of the same dvi_scanbuf_main_16bpp handler as DVIGFX16 above. Not
// optimal, sure...but not pessimal either. The allocation of those 16-bit
// scanlines is weird(tm) though. Rather than a separate malloc (which
// creates a nasty can of worms if that fails after a successful framebuffer
// allocation...unlikely but not impossible), the framebuffer size is
// tweaked so that W*H is always an even number, plus 4 extra rows are
// added: thus two 16-bit scanlines, word-aligned. That extra memory is for
// us, but allocated by GFX as part of the framebuffer all at once. The
// HEIGHT value is de-tweaked to the original value so clipping won't allow
// any drawing operations to spill into the 16-bit scanlines.

DVIGFX8::DVIGFX8(const DVIresolution r, const struct dvi_serialiser_cfg &c,
                 vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas8(dvispec[r].width, ((dvispec[r].height + 1) & ~1) + 4) {
  HEIGHT = _height = dvispec[r].height;
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = false;
}

DVIGFX8::~DVIGFX8(void) { gfxptr = NULL; }

static void scanline_callback_GFX8(void) {
  ((DVIGFX8 *)gfxptr)->_scanline_callback();
}

void __not_in_flash_func(DVIGFX8::_scanline_callback)(void) {
  uint16_t *b16;
  while (queue_try_remove_u32(&dvi0.q_colour_free, &b16))
    ;                   // Discard returned pointer(s)
  b16 = row565[rowidx]; // Next row to send
  queue_add_blocking_u32(&dvi0.q_colour_valid, &b16); // Send it

  scanline = (scanline + 1) % HEIGHT;           // Next scanline
  uint8_t *b8 = &getBuffer()[WIDTH * scanline]; // New src
  rowidx = (rowidx + 1) & 1;                    // Swap row565[] bufs
  b16 = row565[rowidx];                         // New dest
  for (int i = 0; i < WIDTH; i++)
    b16[i] = palette[b8[i]];
}

bool DVIGFX8::begin(void) {
  uint8_t *bufptr = getBuffer();
  if ((bufptr)) {
    gfxptr = this;
    row565[0] = (uint16_t *)&bufptr[(WIDTH * HEIGHT + 1) & ~1];
    row565[1] = row565[0] + WIDTH;
    memset(palette, 0, sizeof palette);
    // mainloop = mainloop8;
    mainloop = dvi_scanbuf_main_16bpp; // in libdvi
    dvi0.scanline_callback = scanline_callback_GFX8;
    PicoDVI::begin();
    // No need to initialize the row565 buffer contents as that memory is
    // cleared on canvas alloc, and the initial palette state is also all 0.
    uint16_t *b16 = row565[0];
    queue_add_blocking_u32(&dvi0.q_colour_valid, &b16);
    b16 = row565[1];
    queue_add_blocking_u32(&dvi0.q_colour_valid, &b16);
    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}

// DVIGFX8x2 (8-bit, color-indexed, double-buffered for animation)
// requires latest Adafruit_GFX as it plays games with the canvas pointer,
// wasn't possible until that was made protected (vs private). This is very
// similar to DVIGFX8 but effectively has two canvases and palettes ("front"
// and "back"). Drawing and palette-setting operations ONLY apply to the
// "back" state. Call swap() to switch the front/back buffers at the next
// vertical sync, for flicker-free and tear-free animation.

DVIGFX8x2::DVIGFX8x2(const DVIresolution r, const struct dvi_serialiser_cfg &c,
                     vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas8(dvispec[r].width, dvispec[r].height * 2 + 4) {
  HEIGHT = _height = dvispec[r].height;
  buffer_save = buffer;
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = false;
}

DVIGFX8x2::~DVIGFX8x2(void) {
  buffer = buffer_save; // Restore pointer so canvas destructor works
  gfxptr = NULL;
}

static void scanline_callback_GFX8x2(void) {
  ((DVIGFX8x2 *)gfxptr)->_scanline_callback();
}

void __not_in_flash_func(DVIGFX8x2::_scanline_callback)(void) {
  uint16_t *b16;
  while (queue_try_remove_u32(&dvi0.q_colour_free, &b16))
    ;                   // Discard returned pointer(s)
  b16 = row565[rowidx]; // Next row to send
  queue_add_blocking_u32(&dvi0.q_colour_valid, &b16); // Send it

  if (++scanline >= HEIGHT) {      // Next scanline...end of screen reached?
    if (swap_wait) {               // Swap buffers?
      back_index = 1 - back_index; // Yes plz
      buffer = buffer_save + WIDTH * HEIGHT * back_index;
      swap_wait = 0;
    }
    scanline = 0;
  }
  // Refresh from front buffer
  uint8_t *b8 = buffer_save + WIDTH * HEIGHT * (1 - back_index) +
                WIDTH * scanline; // New src
  rowidx = (rowidx + 1) & 1;      // Swap row565[] bufs
  b16 = row565[rowidx];           // New dest
  for (int i = 0; i < WIDTH; i++)
    b16[i] = palette[1 - back_index][b8[i]];
}

bool DVIGFX8x2::begin(void) {
  uint8_t *bufptr = getBuffer();
  if ((bufptr)) {
    gfxptr = this;
    row565[0] = (uint16_t *)&bufptr[WIDTH * HEIGHT * 2];
    row565[1] = row565[0] + WIDTH;
    memset(palette, 0, sizeof palette);
    // mainloop = mainloop8;
    mainloop = dvi_scanbuf_main_16bpp; // in libdvi
    dvi0.scanline_callback = scanline_callback_GFX8x2;
    PicoDVI::begin();
    bufptr += WIDTH * HEIGHT; // Initial front buffer is index 1
    // No need to initialize the row565 buffer contents as that memory is
    // cleared on canvas alloc, and the initial palette state is also all 0.
    uint16_t *b16 = row565[0];
    queue_add_blocking_u32(&dvi0.q_colour_valid, &b16);
    b16 = row565[1];
    queue_add_blocking_u32(&dvi0.q_colour_valid, &b16);
    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}

void DVIGFX8x2::swap(bool copy_framebuffer, bool copy_palette) {

  // Request buffer swap at next frame end, wait for it to happen.
  for (swap_wait = 1; swap_wait;)
    ;

  if ((copy_framebuffer)) {
    uint32_t bufsize = WIDTH * HEIGHT;
    memcpy(buffer_save + bufsize * back_index,
           buffer_save + bufsize * (1 - back_index), bufsize);
  }

  if ((copy_palette)) {
    memcpy(palette[back_index], palette[1 - back_index], sizeof(palette[0]));
  }
}

// 1-bit WIP --------

DVIGFX1::DVIGFX1(const DVIresolution r, const bool d,
                 const struct dvi_serialiser_cfg &c, vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas1(dvispec[r].width,
                 d ? (dvispec[r].height * 2) : dvispec[r].height),
      dbuf(d) {
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = true;
  HEIGHT = _height = dvispec[r].height;
  buffer_save = buffer;
}

DVIGFX1::~DVIGFX1(void) {
  buffer = buffer_save; // Restore pointer so canvas destructor works
  gfxptr = NULL;
}

static void mainloop1(struct dvi_inst *inst) {
  ((DVIGFX1 *)gfxptr)->_mainloop();
}

void __not_in_flash_func(DVIGFX1::_mainloop)(void) {
  for (;;) {
    uint8_t *b8 = buffer_save;
    if (dbuf)
      b8 += ((WIDTH + 7) / 8) * HEIGHT * (1 - back_index);
    for (int y = 0; y < HEIGHT; y++) {
      const uint32_t *colourbuf =
          (const uint32_t *)(b8 + y * ((WIDTH + 7) / 8));
      uint32_t *tmdsbuf;
      queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
      tmds_encode_1bpp(colourbuf, tmdsbuf, WIDTH);
      queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
    }
    if (swap_wait) {               // Swap buffers?
      back_index = 1 - back_index; // Yes plz
      buffer = buffer_save + ((WIDTH + 7) / 8) * HEIGHT * back_index;
      swap_wait = 0;
    }
  }
}

bool DVIGFX1::begin(void) {
  if ((getBuffer())) {
    gfxptr = this;
    mainloop = mainloop1;
    PicoDVI::begin();
    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}

void DVIGFX1::swap(bool copy_framebuffer) {
  if (dbuf) {
    // Request buffer swap at next frame end, wait for it to happen.
    for (swap_wait = 1; swap_wait;)
      ;

    if ((copy_framebuffer)) {
      uint32_t bufsize = ((WIDTH + 7) / 8) * HEIGHT;
      memcpy(buffer_save + bufsize * back_index,
             buffer_save + bufsize * (1 - back_index), bufsize);
    }
  }
}

// TEXT MODE IS A WORK IN PROGRESS

#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 8
#define FONT_N_CHARS 256
#define FONT_FIRST_ASCII 0
#include "font_8x8.h"

DVIterm1::DVIterm1(const DVIresolution r, const struct dvi_serialiser_cfg &c,
                   vreg_voltage v)
    : PicoDVI(dvispec[r].timing, c, v),
      GFXcanvas16(dvispec[r].width / 8, dvispec[r].height / 8) {
  dvi_vertical_repeat = dvispec[r].v_rep;
  dvi_monochrome_tmds = true;
}

DVIterm1::~DVIterm1(void) { gfxptr = NULL; }

// Character framebuffer is actually a small GFXcanvas16, so...
size_t DVIterm1::write(uint8_t c) {
  if (c == '\r') { // Carriage return
    cursor_x = 0;
  } else if ((c == '\n') || (cursor_x >= WIDTH)) { // Newline OR right edge
    cursor_x = 0;
    if (cursor_y >= HEIGHT) { // Vert scroll?
      memmove(getBuffer(), getBuffer() + WIDTH, WIDTH * (HEIGHT - 1) * 2);
      drawFastHLine(0, HEIGHT - 1, WIDTH, ' '); // Clear bottom line
      cursor_y = HEIGHT - 1;
    } else {
      cursor_y++;
    }
  }
  if ((c != '\r') && (c != '\n')) {
    drawPixel(cursor_x, cursor_y, c);
    cursor_x++;
  }
  return 1;
}

// TO DO: alloc this dynamically as part of object (maybe part of canvas)
static uint8_t scanbuf[1280 / 8] __attribute__((aligned(4)));

#ifdef TERM_USE_INTERRUPT

void inline __not_in_flash_func(DVIterm1::_prepare_scanline)(uint16_t y) {
  uint16_t *row = getBuffer() + (y / FONT_CHAR_HEIGHT) * WIDTH;
  uint32_t offset = (y & 7) * FONT_N_CHARS;

  // Blit font into 1bpp scanline buffer, then encode scanbuf into tmdsbuf
  for (uint16_t x = 0; x < WIDTH; x++) {
    uint8_t mask = row[x] >> 8;
    uint8_t c = (row[x] & 255) - FONT_FIRST_ASCII;
    scanbuf[x] = font_8x8[offset + c] ^ mask;
  }
  uint32_t *tmdsbuf;
  queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
  tmds_encode_1bpp((const uint32_t *)scanbuf, tmdsbuf, WIDTH * 8);
  queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
}

void __not_in_flash_func(term1_scanline_callback)(void) {
  static uint y = 1;
  ((DVIterm1 *)gfxptr)->_prepare_scanline(y);
  y = (y + 1) % (((DVIterm1 *)gfxptr)->height() * 8);
}

static void mainloopterm1(struct dvi_inst *inst) {
  // Idle func, everything happens in interrupt
  for (;;)
    delay(1000);
}

#else

// Old way, without interrupt
// This is a little simpler and might stick with it
// since nothing important to do in idle func above.

static void mainloopterm1(struct dvi_inst *inst) {
  ((DVIterm1 *)gfxptr)->_mainloop();
}

void __not_in_flash_func(DVIterm1::_mainloop)(void) {
  for (;;) {
    for (uint16_t y = 0; y < HEIGHT; y++) {
      uint16_t *row = getBuffer() + y * WIDTH;
      for (uint8_t y1 = 0; y1 < 8; y1++) {
        uint32_t offset = y1 * FONT_N_CHARS;
        for (uint16_t x = 0; x < WIDTH; x++) {
          uint8_t mask = row[x] >> 8;
          uint8_t c = (row[x] & 255) - FONT_FIRST_ASCII;
          scanbuf[x] = font_8x8[offset + c] ^ mask;
        }
        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        tmds_encode_1bpp((const uint32_t *)scanbuf, tmdsbuf, WIDTH * 8);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
      }
    }
  }
}

#endif // end TERM_USE_INTERRUPT

bool DVIterm1::begin(void) {
  if ((getBuffer())) {
    fillScreen(' ');
    gfxptr = this;
#ifdef TERM_USE_INTERRUPT
    dvi0.scanline_callback = term1_scanline_callback;
#endif
    mainloop = mainloopterm1;
    PicoDVI::begin();

    // Must do this AFTER begin because tmdsbuf (accessed in func)
    // doesn't exist yet until dvi_init (in begin) is called.
#ifdef TERM_USE_INTERRUPT
    _prepare_scanline(0);
#endif

    wait_begin = false; // Set core 1 in motion
    return true;
  }
  return false;
}
