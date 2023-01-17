#pragma once

#include "../software/include/common_dvi_pin_configs.h"
#include "hardware/vreg.h" // In Pico SDK
#include "libdvi/dvi.h"
#include "pico/stdlib.h" // In Pico SDK
#include <Adafruit_GFX.h>

class PicoDVI {
public:
  PicoDVI(const struct dvi_timing &t = dvi_timing_640x480p_60hz,
          vreg_voltage v = VREG_VOLTAGE_1_10,
          const struct dvi_serialiser_cfg &c = pimoroni_demo_hdmi_cfg);
  ~PicoDVI(void);
  void _setup(void);

protected:
  void begin(void);
  vreg_voltage voltage;
  struct dvi_inst dvi0;
  void (*mainloop)(dvi_inst *) = NULL;
};

class DVIGFX16 : public PicoDVI, public GFXcanvas16 {
public:
  DVIGFX16(const uint16_t w = 320, const uint16_t h = 240,
           const struct dvi_timing &t = dvi_timing_640x480p_60hz,
           vreg_voltage v = VREG_VOLTAGE_1_10,
           const struct dvi_serialiser_cfg &c = pimoroni_demo_hdmi_cfg);
  ~DVIGFX16(void);
  bool begin(void);
  uint16_t color565(uint8_t red, uint8_t green, uint8_t blue) {
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
  }
  void _scanline_callback(void);

protected:
  uint16_t scanline = 2; // First 2 scanlines are set up before DVI start
};

class DVIGFX8 : public PicoDVI, public GFXcanvas8 {
public:
  DVIGFX8(const uint16_t w = 320, const uint16_t h = 240,
          const struct dvi_timing &t = dvi_timing_640x480p_60hz,
          vreg_voltage v = VREG_VOLTAGE_1_10,
          const struct dvi_serialiser_cfg &c = pimoroni_demo_hdmi_cfg);
  ~DVIGFX8(void);
  bool begin(void);
  uint16_t *getPalette(void) { return palette; }
  void setColor(uint8_t idx, uint16_t color) { palette[idx] = color; }
  void setColor(uint8_t idx, uint8_t red, uint8_t green, uint8_t blue) {
    palette[idx] = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
  }
  uint16_t getColor(uint8_t idx) { return palette[idx]; }

  void _scanline_callback(void);

protected:
  uint16_t palette[256];
  uint16_t *row565[2];   // 2 scanlines of 16-bit RGB565 data
  uint16_t scanline = 2; // First 2 scanlines are set up before DVI start
  uint8_t rowidx = 1;    // Alternate 0/1 for which row565[] is active
};

class DVIGFX8x2 : public PicoDVI, public GFXcanvas8 {
public:
  DVIGFX8x2(const uint16_t w = 320, const uint16_t h = 240,
            const struct dvi_timing &t = dvi_timing_640x480p_60hz,
            vreg_voltage v = VREG_VOLTAGE_1_10,
            const struct dvi_serialiser_cfg &c = pimoroni_demo_hdmi_cfg);
  ~DVIGFX8x2(void);
  bool begin(void);
  uint16_t *getPalette(void) { return palette[back_index]; }
  void setColor(uint8_t idx, uint16_t color) {
    palette[back_index][idx] = color;
  }
  void setColor(uint8_t idx, uint8_t red, uint8_t green, uint8_t blue) {
    palette[back_index][idx] =
        ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3);
  }
  uint16_t getColor(uint8_t idx) { return palette[back_index][idx]; }
  void swap(bool copy_framebuffer = false, bool copy_palette = false);

  void _scanline_callback(void);

protected:
  uint16_t palette[2][256];    // Double-buffered palette
  uint16_t *row565[2];         // 2 scanlines of 16-bit RGB565 data
  uint16_t scanline = 2;       // First 2 scanlines are set up before DVI start
  uint8_t rowidx = 1;          // Alternate 0/1 for which row565[] is active
  uint8_t *buffer_save;        // Original canvas buffer pointer
  uint8_t back_index = 0;      // Which of 2 buffers receives draw ops
  volatile bool swap_wait = 0; // For syncronizing front/back buffer swap
};
