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

PIO pio = pio1;

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

uint8_t decode[256];

struct {
    uint8_t x0h, x0l, x1h, x1l, y0h, y0l, y1h, y1l;
} bounds;

#define COMMAND_CASET (0x2a)
#define COMMAND_PASET (0x2b)
#define COMMAND_RAMWR (0x2c)

static int clamped(int x, int lo, int hi) {
    if(x < lo) { return lo; }
    if(x > hi) { return hi; }
    return x;
}

#define GET_WORD() (word = pio_sm_get_blocking(pio, sm))
#define GET_DATA() do { GET_WORD(); if(IS_COMMAND()) goto next_command; } while(0)
#define IS_COMMAND() (!(word & 0x2))
#define DECODED() (decode[(word & 0x55) | ((word >> 7) & 0xaa)])

uint16_t *framebuf;
uint sm;


#include <PicoDVI.h> // Core display & graphics library

// Your basic 320x240 16-bit color display:
DVIGFX16 display(320, 240, dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, pimoroni_demo_hdmi_cfg);
// Not all RP2040s can deal with the 295 MHz overclock this requires, but if you'd like to try:
//DVIGFX16 display(400, 240, dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_30, pimoroni_demo_hdmi_cfg);

void setup() {
  Serial.begin(115200);
  //while(!Serial);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }

  framebuf = display.getBuffer();

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
  pio_sm_set_consecutive_pindirs(pio, sm, 1, 1, false);
  pio_sm_set_consecutive_pindirs(pio, sm, jmp_pin, 1, false);
  // Connect these GPIOs to this PIO block
  pio_gpio_init(pio, pin);
  pio_gpio_init(pio, pin + 1);
  pio_gpio_init(pio, pin + 2);
  pio_gpio_init(pio, jmp_pin);
  // set pulls
  gpio_set_pulls(pin, true, false);
  gpio_set_pulls(pin + 1, true, false);
  gpio_set_pulls(pin + 2, true, false);
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
}

void loop() {
uint16_t word = 0;

    int x=0, y=0;
    while(true) {
        GET_WORD();
next_command:
        switch(word) {
        case ENCODED_COMMAND(COMMAND_CASET):
            GET_DATA();
            bounds.x0h = DECODED();

            GET_DATA();
            bounds.x0l = DECODED();

            GET_DATA();
            bounds.x1h = DECODED();

            GET_DATA();
            bounds.x1l = DECODED();
            break;

        case ENCODED_COMMAND(COMMAND_PASET):
            GET_DATA();
            bounds.y0h = DECODED();

            GET_DATA();
            bounds.y0l = DECODED();

            GET_DATA();
            bounds.y1h = DECODED();

            GET_DATA();
            bounds.y1l = DECODED();
            break;

        case ENCODED_COMMAND(COMMAND_RAMWR):
            {
                int X0 = bounds.x0l | (bounds.x0h << 8);
                X0 = clamped(X0, 0, display.width());
                int X1 = bounds.x1l | (bounds.x1h << 8);
                X1 = clamped(X1, X0, display.width()) + 1;

                int Y0 = bounds.y0l | (bounds.y0h << 8);
                Y0 = clamped(Y0, 0, display.height());
                int Y1 = bounds.y1l | (bounds.y1h << 8);
                Y1 = clamped(Y1, Y0, display.height()) + 1;
                while(1) {
                    for(y=Y0; y<Y1; y++) {
                        for(x=X0; x<X1; x++) {
                            GET_DATA();
                            uint16_t pixel = DECODED() << 8;
                            GET_DATA();
                            pixel |= DECODED();
                            framebuf[x + y * display.width()] = pixel;
                        }
                    }
                }
            }
        }
    }
}

#if 0

// Orig code:

.program fourwire
; Sample bits using an external clock, and push groups of bits into the RX FIFO.
; - IN pin 0 is the data pin   (GPIO18)
; - IN pin 1 is the dc pin     (GPIO19)
; - IN pin 2 is the clock pin  (GPIO20)
; - JMP pin is the chip select (GPIO21)
; - Autopush is enabled, threshold 8
;
; This program waits for chip select to be asserted (low) before it begins
; clocking in data. Whilst chip select is low, data is clocked continuously. If
; chip select is deasserted part way through a data byte, the partial data is
; discarded. This makes use of the fact a mov to isr clears the input shift
; counter.
flush:
    mov isr, null         ; Clear ISR and input shift counter
    jmp check_chip_select ; Poll chip select again
.wrap_target
do_bit:
    wait 0 pin 2          ; Detect rising edge and sample input data
    wait 1 pin 2          ; (autopush takes care of moving each complete
    in pins, 2            ; data word to the FIFO)
check_chip_select:
    jmp pin, flush        ; Bail out if we see chip select high
.wrap

% c-sdk {
static inline void fourwire_program_init(PIO pio, uint sm, uint offset, uint pin, uint jmp_pin) {
    pio_sm_config c = fourwire_program_get_default_config(offset);

    // Set the IN base pin to the provided `pin` parameter. This is the data
    // pin, and the next-numbered GPIO is used as the clock pin.
    sm_config_set_in_pins(&c, pin);
    sm_config_set_jmp_pin(&c, jmp_pin);
    // Set the pin directions to input at the PIO
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 3, false);
    pio_sm_set_consecutive_pindirs(pio, sm, 1, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm, jmp_pin, 1, false);
    // Connect these GPIOs to this PIO block
    pio_gpio_init(pio, pin);
    pio_gpio_init(pio, pin + 1);
    pio_gpio_init(pio, pin + 2);
    pio_gpio_init(pio, jmp_pin);
    // set pulls
    gpio_set_pulls(pin, true, false);
    gpio_set_pulls(pin + 1, true, false);
    gpio_set_pulls(pin + 2, true, false);
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
}
%}
#endif
