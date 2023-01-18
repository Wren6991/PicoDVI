// PicoDVI-based "virtual SPITFT" display. Receives graphics commands/data
// over 4-wire SPI interface, mimicking functionality of displays such as
// ILI9341, but shown on monitor instead.

// Quick-n-dirty port without regard to screen size and stuff, and
// some pins are hardcoded and stuff. Just proof-of-concept stuff,
// getting the Pico SDK version of this code adapted to Arduino and
// the PicoDVI library.

// Output of pioasm ----

#define fourwire_wrap_target 2
#define fourwire_wrap 5

static const uint16_t fourwire_program_instructions[] = {
    0xa0c3, //  0: mov    isr, null                  
    0x0005, //  1: jmp    5                          
            //     .wrap_target
    0x2022, //  2: wait   0 pin, 2                   
    0x20a2, //  3: wait   1 pin, 2                   
    0x4002, //  4: in     pins, 2                    
    0x00c0, //  5: jmp    pin, 0                     
            //     .wrap
};

static const struct pio_program fourwire_program = {
    .instructions = fourwire_program_instructions,
    .length = 6,
    .origin = -1,
};

static inline pio_sm_config fourwire_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + fourwire_wrap_target, offset + fourwire_wrap);
    return c;
}

// end pioasm output ----

#include <PicoDVI.h> // Core display & graphics library

// Your basic 320x240 16-bit color display:
DVIGFX16 display(320, 240, dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, pimoroni_demo_hdmi_cfg);
// Not all RP2040s can deal with the 295 MHz overclock this requires, but if you'd like to try:
//DVIGFX16 display(400, 240, dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_30, pimoroni_demo_hdmi_cfg);

PIO pio = pio1;
uint sm;

uint8_t decode[256];
#define DECODE(w) (decode[(w & 0x55) | ((w >> 7) & 0xaa)])

uint16_t *framebuf = display.getBuffer();

#define BIT_DEPOSIT(b, i) ((b) ? (1<<(i)) : 0)
#define BIT_EXTRACT(b, i) (((b) >> (i)) & 1)
#define BIT_MOVE(b, src, dest) BIT_DEPOSIT(BIT_EXTRACT(b, src), dest)
#define ENCODED_COMMAND(x) ( \
    (BIT_MOVE(x, 0,  0)) | \
    (BIT_MOVE(x, 1,  2)) | \
    (BIT_MOVE(x, 2,  4)) | \
    (BIT_MOVE(x, 3,  6)) | \
    (BIT_MOVE(x, 4,  8)) | \
    (BIT_MOVE(x, 5, 10)) | \
    (BIT_MOVE(x, 6, 12)) | \
    (BIT_MOVE(x, 7, 14)) \
)


#define COMMAND_NOP     (0x00)
#define COMMAND_SWRESET (0x01)
#define COMMAND_CASET   (0x2a)
#define COMMAND_PASET   (0x2b)
#define COMMAND_RAMWR   (0x2c)
#define COMMAND_MADCTL  (0x36)

#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20
#define MADCTL_ML 0x10

void setup() {
  Serial.begin(115200);
  while(!Serial);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }

  for(int i=0; i<256; i++) {
    int j = (BIT_MOVE(i,  0, 0)) |
            (BIT_MOVE(i,  2, 1)) |
            (BIT_MOVE(i,  4, 2)) |
            (BIT_MOVE(i,  6, 3)) |
            (BIT_MOVE(i,  1, 4)) |
            (BIT_MOVE(i,  3, 5)) |
            (BIT_MOVE(i,  5, 6)) |
            (BIT_MOVE(i,  7, 7));
    decode[i] = j;
  }
    
  uint offset = pio_add_program(pio, &fourwire_program);
  sm = pio_claim_unused_sm(pio, true);

  uint pin = 18;
  uint jmp_pin = 21;

  pio_sm_config c = fourwire_program_get_default_config(offset);

  // Set the IN base pin to the provided `pin` parameter. This is the data
  // pin, and the next-numbered GPIO is used as the clock pin.
  sm_config_set_in_pins(&c, pin);
  sm_config_set_jmp_pin(&c, jmp_pin);
  // Set the pin directions to input at the PIO
  pio_sm_set_consecutive_pindirs(pio, sm, pin, 3, false);
  pio_sm_set_consecutive_pindirs(pio, sm, jmp_pin, 1, false);
  // Connect GPIOs to PIO block, set pulls
  for (uint8_t i=0; i<3; i++) {
    pio_gpio_init(pio, pin + i);
    gpio_set_pulls(pin + i, true, false);
  }
  pio_gpio_init(pio, jmp_pin);
  gpio_set_pulls(jmp_pin, true, false);

  // Shifting to left matches the customary MSB-first ordering of SPI.
  sm_config_set_in_shift(
      &c,
      false, // Shift-to-right = false (i.e. shift to left)
      true,  // Autopush enabled
      16     // Autopush threshold
  );

  // We only receive, so disable the TX FIFO to make the RX FIFO deeper.
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

  // Load our configuration, and start the program from the beginning
  pio_sm_init(pio, sm, offset, &c);
  pio_sm_set_enabled(pio, sm, true);

  // State machine should handle malformed requests somewhat,
  // e.g. RAMWR writes that wrap around or drop mid-data.

  uint8_t cmd = COMMAND_NOP; // Last received command
  // Address window and current pixel position:
  uint16_t X0 = 0, X1 = display.width() - 1, Y0 = 0, Y1 = display.height() - 1;
  uint16_t x = 0, y = 0;
  union { // Data receive buffer sufficient for implemented commands
    uint8_t  b[4];
    uint16_t w[2];
    uint32_t l;
  } buf;
  uint8_t buflen = 0; // Number of bytes expected following command
  uint8_t bufidx = 0; // Current position in buf.b[] array

  for (;;) {
    uint16_t ww = pio_sm_get_blocking(pio, sm); // Read next word (data & DC interleaved)
    if ((ww & 0x2)) { // DC bit is set, that means it's data (most common case, hence 1st)
      if (bufidx < buflen) { // Decode & process only if recv buffer isn't full
        buf.b[bufidx++] = DECODE(ww);
        if (bufidx >= buflen) { // Receive threshold reached?
          switch (cmd) {
           case COMMAND_CASET:
            // Clipping is not performed here because framebuffer
            // may be a different size than implied SPI device.
            // That occurs in the RAMWR condition later.
            X0 = __builtin_bswap16(buf.w[0]);
            X1 = __builtin_bswap16(buf.w[1]);
            if (X0 > X1) {
              uint16_t tmp = X0;
              X0 = X1;
              X1 = tmp;
            }
            buflen = 0; // Disregard any further data
            break;
           case COMMAND_PASET:
            Y0 = __builtin_bswap16(buf.w[0]);
            Y1 = __builtin_bswap16(buf.w[1]);
            if (Y0 > Y1) {
              uint16_t tmp = Y0;
              Y0 = Y1;
              Y1 = tmp;
            }
            buflen = 0; // Disregard any further data
            break;
           case COMMAND_RAMWR:
            // Write pixel to screen, increment X/Y, wrap around as needed.
            // drawPixel() is used as it handles both clipping & rotation,
            // saves a lot of bother here. However, this only handles rotation,
            // NOT full MADCTL mapping, but the latter is super rare, I think
            // it's only used in some eye code to mirror one of two screens.
            // If it's required, then rotation, mirroring and clipping will
            // all need to be handled in this code...but, can write direct to
            // framebuffer then, might save some cycles.
            display.drawPixel(x, y, __builtin_bswap16(buf.w[0]));
            if (++x > X1) {
              x = X0;
              if (++y > Y1) {
                y = Y0;
              }
            }
            bufidx = 0; // Reset for next pixel
            // Buflen is left as-is, so more pixels can be processed
            break;
           case COMMAND_MADCTL:
            switch (buf.b[0] & 0xF0) {
             case MADCTL_MX | MADCTL_MV:             // ST77XX
             case MADCTL_MX | MADCTL_MY | MADCTL_MV: // ILI9341
              display.setRotation(0);
              break;
             case MADCTL_MX | MADCTL_MY: // ST77XX
             case MADCTL_MX:             // ILI9341
              display.setRotation(1);
              break;
             case MADCTL_MY | MADCTL_MV: // ST77XX
             case MADCTL_MV:             // ILI9341
              display.setRotation(2);
              break;
             case 0:         // ST77XX
             case MADCTL_MY: // ILI9341
              display.setRotation(3);
              break;
            }
            //madctl = buf.b[0];
            buflen = 0; // Disregard any further data
            break;
          }
        }
      }
    } else { // Is command
      bufidx = 0; // Reset data recv buffer
      cmd = DECODE(ww);
      switch (cmd) {
       case COMMAND_SWRESET:
        display.setRotation(0);
        x = y = X0 = Y0 = buflen = 0;
        X1 = display.width() - 1;
        Y1 = display.height() - 1;
        break;
       case COMMAND_CASET:
        buflen = 4; // Expecting 2 16-bit values (X0, X1)
        break;
       case COMMAND_PASET:
        buflen = 4; // Expecting 2 16-bit values (Y0, Y1)
        break;
       case COMMAND_RAMWR:
        buflen = 2; // Expecting 1+ 16-bit values
        x = X0;     // Start at UL of address window
        y = Y0;
        break;
       case COMMAND_MADCTL:
        buflen = 1; // Expecting 1 8-bit value
        break;
       default:
        // Unknown or unimplemented command, discard any data that follows
        buflen = 0;
      }
    }
  }
}

void loop() {
}
