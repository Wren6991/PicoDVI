/* Changes to use IBM VGA 8x16 font (replacing 8x8 font):
 * - Font Source: IBM_VGA_8x16.bin (4096 bytes, 256 chars) from https://github.com/spacerace/romfont
 * - Conversion: Python script /home/noneya/pico/PicoDVI/software/fonts/convert_font.py
 *   - Generates font_8x16.h (1520 bytes, ASCII 32-126)
 * - Font Include: Changed #include "font_8x8.h" to #include "font_8x16.h"
 * - Font Height: Set FONT_CHAR_HEIGHT=16 (was 8)
 * - Grid Size: CHAR_ROWS=FRAME_HEIGHT/FONT_CHAR_HEIGHT (480/16=30, was 60); CHAR_COLS=80 (unchanged)
 * - Char Buffer: Adjusted charbuf[CHAR_ROWS * CHAR_COLS] to 80x30=2400 bytes (was 80x60=4800)
 * - prepare_scanline(): Updated to font_8x16[(c - FONT_FIRST_ASCII) * FONT_CHAR_HEIGHT + (y % FONT_CHAR_HEIGHT)]
 * - Bit Reversal: Added reverse_byte() to fix mirrored text; applied in prepare_scanline()
 * - Text: Displays "Noneya's Terminal" on first row
 * Setup: 640x480, adafruit_hdmi_sock_cfg, DVI_MONOCHROME_TMDS=1
 */
 /* Build Instructions
 cd /home/noneya/pico/PicoDVI/software/build
 cmake -DPICO_SDK_PATH=/home/noneya/pico/pico-sdk \
      -DPICO_EXTRAS_PATH=/home/noneya/pico/pico-extras \
      -DPICO_PLATFORM=rp2040 \
      -DPICO_COPY_TO_RAM=1 \
      -DDVI_DEFAULT_SERIAL_CONFIG=adafruit_hdmi_sock_cfg ..
 make -j$(nproc)
 cp apps/my_terminal/my_terminal.uf2 /media/noneya/RPI-RP2/
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/dma.h"
#include "pico/sem.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"

#include "hardware/i2c.h"
#define I2C_SLAVE i2c1
#define I2C_SDA_PIN 26
#define I2C_SCL_PIN 27
#define I2C_SLAVE_ADDR 0x55
//#define I2C_SLAVE_ADDR 0x42


#include "font_8x16.h"
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 16
#define FONT_N_CHARS 95
#define FONT_FIRST_ASCII 32

#define MODE_640x480_60Hz
#if defined(MODE_640x480_60Hz)
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz
#else
#error "Select a video mode!"
#endif

#define LED_PIN 16

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;

#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)
char charbuf[CHAR_ROWS * CHAR_COLS];

// DON ADD
volatile uint cursor_pos = CHAR_COLS; // Start on second row
void i2c1_irq_handler() {
    while (i2c_get_read_available(I2C_SLAVE)) {
        uint8_t c;
        i2c_read_raw_blocking(I2C_SLAVE, &c, 1);
        if (c >= 32 && c <= 126) { // Printable ASCII
            charbuf[cursor_pos++] = c;
            if (cursor_pos >= CHAR_ROWS * CHAR_COLS)
                cursor_pos = CHAR_COLS; // Wrap to second row
        }
    }
    // Clear I2C interrupts by reading the raw interrupt status register
    (void)i2c_get_hw(I2C_SLAVE)->raw_intr_stat;
}


// Helper function to reverse bits in a byte (fixes mirrored text)
static inline uint8_t reverse_byte(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static inline void prepare_scanline(const char *chars, uint y) {
    static uint8_t scanbuf[FRAME_WIDTH / 8];
    for (uint i = 0; i < CHAR_COLS; ++i) {
        uint c = chars[i + y / FONT_CHAR_HEIGHT * CHAR_COLS];
        scanbuf[i] = reverse_byte(font_8x16[(c - FONT_FIRST_ASCII) * FONT_CHAR_HEIGHT + (y % FONT_CHAR_HEIGHT)]);
    }
    uint32_t *tmdsbuf;
    queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
    tmds_encode_1bpp((const uint32_t*)scanbuf, tmdsbuf, FRAME_WIDTH);
    queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
}

void core1_scanline_callback() {
    static uint y = 1;
    prepare_scanline(charbuf, y);
    y = (y + 1) % FRAME_HEIGHT;
}

void __not_in_flash("main") core1_main() {
    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    sem_acquire_blocking(&dvi_start_sem);
    dvi_start(&dvi0);
    while (1) 
        __wfi();
    __builtin_unreachable();
}

int __not_in_flash("main") main() {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
#ifdef RUN_FROM_CRYSTAL
    set_sys_clock_khz(12000, true);
#else
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
#endif

    setup_default_uart();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    printf("Configuring DVI\n");

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi0.scanline_callback = core1_scanline_callback;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    printf("Prepare first scanline\n");
    for (int i = 0; i < CHAR_ROWS * CHAR_COLS; ++i)
        charbuf[i] = ' ';
    const char *custom_text = "Noneya's Terminal";
    for (int i = 0; i < strlen(custom_text) && i < CHAR_COLS; ++i)
        charbuf[i] = custom_text[i];
    prepare_scanline(charbuf, 0);

    printf("Core 1 start\n");
    sem_init(&dvi_start_sem, 0, 1);
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);

    sem_release(&dvi_start_sem);

    // DON ADD
    i2c_init(I2C_SLAVE, 100 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    
    i2c_set_slave_mode(I2C_SLAVE, true, I2C_SLAVE_ADDR);
    irq_set_exclusive_handler(I2C1_IRQ, i2c1_irq_handler);
    irq_set_enabled(I2C1_IRQ, true);

    while (1)
        __wfi();
    __builtin_unreachable();
}
