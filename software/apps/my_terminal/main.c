/*
* Project: Noneya's Terminal
* Description:
*   A monochrome 640x480 HDMI terminal displaying an 80x30 character grid (8x16 font),
*   built using an Adafruit HDMI sock on a Raspberry Pi Pico (RP2040). Receives keyboard
*   input via I2C slave (GPIO 26 SDA, 27 SCL, address 0x55) from a second Pico acting as
*   I2C master and serial bridge. Displays a persistent title ("Noneya's Terminal") on
*   the first row, with real-time rendering of subsequent characters and full scrolling
*   behavior once the screen fills.
*
* Font:
* - Source: IBM VGA 8x16 font (IBM_VGA_8x16.bin, 4096 bytes, 256 chars) from:
*     https://github.com/spacerace/romfont
*     (Alternate fonts available at https://int10h.org/oldschool-pc-fonts/)
* - Conversion: Using convert_font.py from PicoDVI
*     → Generates font_8x16.h (1520 bytes, ASCII 32–126 only)
* - Each glyph is 8x16 pixels, monospaced, 1-bit per pixel (packed as 16 bytes per char)
*
* Key Features:
* - HDMI Output: 640x480@60Hz, monochrome, using PicoDVI (commit 7af20b2…)
* - I2C Input:
*   - Receives printable ASCII (32–126) from master Pico over I2C
*   - Carriage return (\r), line feed (\n), and \r\n pairs handled properly (skip_next_lf)
*   - Backspace (ASCII 8 or 127) erases characters without affecting title row
* - ANSI Escape Sequence Support (core VT100 subset):
*   - Cursor Movement:
*     - `\x1B[A`, `\x1B[B`, `\x1B[C`, `\x1B[D` — Move up/down/left/right
*     - `\x1B[H` — Move to start of row 2
*     - `\x1B[row;colH` — Position cursor absolutely (1-based)
*   - Clearing:
*     - `\x1B[K`, `\x1B[0K`, `\x1B[1K`, `\x1B[2K` — Erase partial/full line
*     - `\x1B[2J` — Clear screen (preserving title row) and home cursor
*   - Text Attributes (SGR codes):
*     - `\x1B[1m` — Bold
*     - `\x1B[7m` — Reverse video
*     - `\x1B[0m` — Reset attributes
* - CSI Parser: Handles parameterized ANSI control sequences with up to 4 parameters
* - Scrolling: Auto-scrolls rows 2–29 upward when cursor reaches row 30
* - Cursor: Blinks every 500ms via prepare_scanline(), shown as a solid block
*
* ANSI Text Attributes:
*   - CSI-based attribute parsing (`ESC[...m`) is fully implemented and recognizes:
*     - `ESC[1m` (bold), `ESC[7m` (reverse video), and `ESC[0m` (reset)
*   - Attribute state is tracked internally but currently not visually rendered,
*     as earlier bold/reverse attempts triggered HDMI flicker
*   - Future work: Implement visual effects in prepare_scanline() once rendering is stable
*
* Memory Notes:
* - charbuf: 80 cols × 30 rows = 2,400 bytes
* - font_8x16.h: ~1.5 KB for ASCII 32–126 glyphs
* - scanbuf: 80 bytes per scanline (reused each frame)
* - No framebuffer: avoids 307,200-byte VRAM; uses scanline buffer to stay SRAM-efficient
* - Total memory usage remains well under 264 KB SRAM budget
*
* I2C Master:
* - Second Pico (serial-to-I2C bridge) receives characters over USB serial and sends via I2C
* - Wiring: GPIO 26 (SDA), 27 (SCL), 4.7k pull-ups, common GND
* - Terminal input via: minicom -D /dev/ttyACM0
*
* Debugging:
* - Uses stdio_init_all(); debug messages printed to /dev/ttyACM1
* - Startup logs: "Configuring DVI", "Prepare first scanline", "Core 1 start", "Received: <ascii>"
*
* Build Instructions:
* cd /home/noneya/pico/PicoDVI/software/build
* rm -rf *
* cmake -DPICO_SDK_PATH=/home/noneya/pico/pico-sdk \
*        -DPICO_EXTRAS_PATH=/home/noneya/pico/pico-extras \
*        -DPICO_PLATFORM=rp2040 \
*        -DPICO_COPY_TO_RAM=1 \
*        -DDVI_DEFAULT_SERIAL_CONFIG=adafruit_hdmi_sock_cfg \
*        -DDVI_MONOCHROME_TMDS=1 \
*        -DCMAKE_BUILD_TYPE=Debug ..
* make -j$(nproc)
* cd /home/noneya/pico/PicoDVI/software/build/apps/my_terminal
* picotool info -F
* picotool load my_terminal.uf2 -f
* echo "Waiting for 5 seconds..."
* sleep 5
* echo "Done waiting!"
* picotool reboot
* or
* # Flash: Hold BOOTSEL, plug in, release
* cp apps/my_terminal/my_terminal.uf2 /media/noneya/RPI-RP2/
*
* Dependencies:
* - Pico SDK v2.1.0
* - PicoDVI (commit 7af20b2742c3dd0d7e7d3224078085ddea04a85f)
* - font_8x16.h in /home/noneya/pico/PicoDVI/software/include
*
* Notes:
* - Uses scanline rendering (dvi_scanbuf_main_8bpp) for memory efficiency
* - Manual I2C slave fallback available if needed; current implementation uses pico/i2c_slave
* - Designed for minimal SRAM usage and maximum retro-style functionality
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
#include "pico/i2c_slave.h"
#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"
#include "font_8x16.h"
#include "hardware/timer.h"

#define I2C_SLAVE i2c1
#define I2C_SDA_PIN 26
#define I2C_SCL_PIN 27
#define I2C_SLAVE_ADDR 0x55
#define I2C_BAUD_RATE 100000
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 16
#define FONT_N_CHARS 95
#define FONT_FIRST_ASCII 32
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz
#define LED_PIN 16
#define CURSOR_BLINK_MS 500

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;
#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)  // 80
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT) // 30
static char charbuf[CHAR_ROWS * CHAR_COLS]; // 2400 bytes
static volatile uint cursor_pos = CHAR_COLS; // Start on second row
static volatile bool skip_next_lf = false; // Handle \r\n
static volatile bool cursor_visible = true;
static absolute_time_t last_blink_time;
static volatile uint8_t escape_seq[3]; // Buffer for \x1B[? sequences
static volatile uint escape_len = 0;


// Escape sequence handling
//static volatile char esc_buffer[8];
//static volatile int esc_index = 0;
//static volatile bool in_escape = false;

// Escape state
static volatile char esc_buffer[16];
static volatile int esc_index = 0;
static volatile bool in_escape = false;

// Text attributes
static volatile bool attr_bold = false;
static volatile bool attr_reverse = false;

// Simple CSI parser
typedef struct {
    int params[4];
    int count;
} csi_params_t;

static bool parse_csi(const char *buf, int len, csi_params_t *out, char *final) {
    out->count = 0;
    int val = 0;
    bool in_param = false;

    for (int i = 2; i < len; ++i) {
        char c = buf[i];

        if (c >= '0' && c <= '9') {
            val = val * 10 + (c - '0');
            in_param = true;
        } else if (c == ';') {
            if (in_param && out->count < 4) {
                out->params[out->count++] = val;
                val = 0;
                in_param = false;
            }
        } else {
            if (in_param && out->count < 4)
                out->params[out->count++] = val;

            *final = c;
            return true;
        }
    }

    return false;
}

static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    if (event == I2C_SLAVE_RECEIVE) {
        while (i2c_get_read_available(i2c)) {
            uint8_t c = i2c_read_byte_raw(i2c);

            if (!in_escape && c == 0x1B) {
                in_escape = true;
                esc_index = 0;
                esc_buffer[esc_index++] = c;
                continue;
            } else if (in_escape) {
                if (esc_index < sizeof(esc_buffer) - 1) {
                    esc_buffer[esc_index++] = c;
                }

                if (esc_index >= 2 && esc_buffer[0] == 0x1B && esc_buffer[1] == '[') {
                    csi_params_t csi;
                    char final;
                    if (parse_csi((const char *)esc_buffer, esc_index, &csi, &final))
 {
                        uint row = cursor_pos / CHAR_COLS;
                        uint col = cursor_pos % CHAR_COLS;

                        switch (final) {
                            case 'A': // Up
                                if (row > 1) cursor_pos -= CHAR_COLS;
                                break;
                            case 'B': // Down
                                if (row < CHAR_ROWS - 1) cursor_pos += CHAR_COLS;
                                break;
                            case 'C': // Right
                                if (col < CHAR_COLS - 1) cursor_pos++;
                                break;
                            case 'D': // Left
                                if (col > 0) cursor_pos--;
                                break;
                            case 'H': { // Cursor position
                                int r = (csi.count >= 1 ? csi.params[0] : 1) - 1;
                                int c = (csi.count >= 2 ? csi.params[1] : 1) - 1;
                                if (r >= 1 && r < CHAR_ROWS && c >= 0 && c < CHAR_COLS)
                                    cursor_pos = r * CHAR_COLS + c;
                                break;
                            }
                            case 'J': { // Clear screen
                                int mode = (csi.count >= 1 ? csi.params[0] : 0);
                                if (mode == 2) {
                                    memset(&charbuf[CHAR_COLS], ' ', (CHAR_ROWS - 1) * CHAR_COLS);
                                    cursor_pos = CHAR_COLS;
                                }
                                break;
                            }
                            case 'K': { // Erase in Line
                                int mode = (csi.count >= 1 ? csi.params[0] : 0);
                                switch (mode) {
                                    case 0: // cursor to end
                                        memset(&charbuf[cursor_pos], ' ', CHAR_COLS - col);
                                        break;
                                    case 1: // start to cursor
                                        memset(&charbuf[row * CHAR_COLS], ' ', col + 1);
                                        break;
                                    case 2: // entire line
                                        memset(&charbuf[row * CHAR_COLS], ' ', CHAR_COLS);
                                        break;
                                }
                                break;
                            }
                            case 'm': { // SGR attributes
                                for (int i = 0; i < csi.count; ++i) {
                                    int code = csi.params[i];
                                    switch (code) {
                                        case 0: attr_bold = false; attr_reverse = false; break;
                                        case 1: attr_bold = true; break;
                                        case 7: attr_reverse = true; break;
                                    }
                                }
                                break;
                            }
                        }

                        in_escape = false;
                        esc_index = 0;
                        continue;
                    }
                }

                if (esc_index >= sizeof(esc_buffer) - 1) {
                    in_escape = false;
                    esc_index = 0;
                }

                continue;
            }

            // Backspace or DEL
            if (c == '\b' || c == 127) {
                if (cursor_pos > CHAR_COLS) {
                    cursor_pos--;
                    charbuf[cursor_pos] = ' ';
                }
                continue;
            }

            // Printable
            if (c >= 32 && c <= 126) {
                charbuf[cursor_pos++] = c;
                skip_next_lf = false;
                if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
                    memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS],
                            (CHAR_ROWS - 2) * CHAR_COLS);
                    memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
                    cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
                }
                continue;
            }

            // Newline / Carriage return
            if (c == '\r' || c == '\n') {
                if (c == '\n' && skip_next_lf) {
                    skip_next_lf = false;
                    continue;
                }
                skip_next_lf = (c == '\r');
                cursor_pos = ((cursor_pos / CHAR_COLS) + 1) * CHAR_COLS;
                if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
                    memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS],
                            (CHAR_ROWS - 2) * CHAR_COLS);
                    memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
                    cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
                }
            }
        }
    }
}










// Helper function to reverse bits in a byte (fixes mirrored text)
static inline uint8_t reverse_byte(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static inline void prepare_scanline(const char *chars, uint y) {
    static uint8_t scanbuf[FRAME_WIDTH / 8]; // 80 bytes
    uint row = y / FONT_CHAR_HEIGHT;
    uint char_idx_base = row * CHAR_COLS;

    // Toggle cursor blink
    if (to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(last_blink_time) > CURSOR_BLINK_MS) {
        cursor_visible = !cursor_visible;
        last_blink_time = get_absolute_time();
    }

    for (uint i = 0; i < CHAR_COLS; ++i) {
        uint buf_idx = char_idx_base + i;
        uint8_t byte;
        if (buf_idx == cursor_pos && cursor_visible) {
            byte = 0xFF; // Solid block cursor
        } else {
            uint c = chars[buf_idx];
            if (c < FONT_FIRST_ASCII || c >= FONT_FIRST_ASCII + FONT_N_CHARS) {
                c = ' ';
            }
            byte = font_8x16[(c - FONT_FIRST_ASCII) * FONT_CHAR_HEIGHT + (y % FONT_CHAR_HEIGHT)];
        }
        scanbuf[i] = reverse_byte(byte);
    }

    uint32_t *tmdsbuf;
    queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
    tmds_encode_1bpp((const uint32_t *)scanbuf, tmdsbuf, FRAME_WIDTH);
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
    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    dvi_start(&dvi0);
    while (1) __wfi();
    __builtin_unreachable();
}

int __not_in_flash("main") main() {
    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

    stdio_init_all();
    sleep_ms(1000); // Minimal delay for console

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    last_blink_time = get_absolute_time();

    printf("Configuring DVI\n");
    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = adafruit_hdmi_sock_cfg;
    dvi0.scanline_callback = core1_scanline_callback;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    printf("Prepare first scanline\n");
    memset(charbuf, ' ', CHAR_ROWS * CHAR_COLS);
    const char *custom_text = "Noneya's Terminal";
    memcpy(charbuf, custom_text, strlen(custom_text));
    prepare_scanline(charbuf, 0);

    printf("Core 1 start\n");
    sem_init(&dvi_start_sem, 0, 1);
    multicore_launch_core1(core1_main);

    sem_release(&dvi_start_sem);

    i2c_init(I2C_SLAVE, I2C_BAUD_RATE);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
    i2c_slave_init(I2C_SLAVE, I2C_SLAVE_ADDR, &i2c_slave_handler);

    while (1) {
        gpio_put(LED_PIN, !gpio_get(LED_PIN)); // Blink LED
        sleep_ms(500);
    }
    __builtin_unreachable();
}

// /*
// * Project: Noneya's Terminal
// * Description: A monochrome 640x480 HDMI terminal displaying an 80x30 character grid (8x16 font)
// *   using an Adafruit HDMI sock on a Raspberry Pi Pico (RP2040). Receives keyboard input via I2C
// *   slave (GPIO 26 SDA, 27 SCL, address 0x55) from an I2C master Pico, which accepts serial input.
// *   Displays "Noneya's Terminal" on the first row, with subsequent rows showing keyboard input in
// *   real-time, supporting carriage return (\r), line feed (\n), or \r\n for line advances, with
// *   scrolling when the screen fills.
// *
// * Font:
// * - Source: IBM VGA 8x16 font (IBM_VGA_8x16.bin, 4096 bytes, 256 chars) from
// *   https://github.com/spacerace/romfont
// *   Another interesting source for fonts is https://int10h.org/oldschool-pc-fonts/download/, though conversion of these is not handled here.
// * - Conversion: Python script /home/noneya/pico/PicoDVI/software/fonts/convert_font.py
// *   - Generates font_8x16.h (1520 bytes, ASCII 32-126, 95 chars)
// *   - Each character is 8x16 pixels, stored as 16 bytes (1 bit per pixel)
// * - Include: Uses #include "font_8x16.h" (replaces font_8x8.h from PicoDVI examples)
// *
// * Key Features:
// * - Display: 640x480@60Hz, monochrome, using PicoDVI library (commit 7af20b2…)
// * - Font Rendering: Scanline-based rendering with tmds_encode_1bpp for memory efficiency (80-byte scanbuf vs. 307,200-byte framebuffer)
// * - Bit Reversal: reverse_byte() fixes mirrored text in prepare_scanline()
// * - I2C Slave: Handles printable ASCII (32-126) and control/input sequences via ANSI-style escapes
// *   - Line Handling: \r (ASCII 13), \n (ASCII 10), or \r\n to advance lines
// *   - Backspace: ASCII 8 or 127 deletes character, restricted from title row
// *   - Arrow Keys: Supports ANSI escape codes for navigation
// *     - \x1B[A → Up  
// *     - \x1B[B → Down  
// *     - \x1B[C → Right  
// *     - \x1B[D → Left
// *   - Cursor Control:
// *     - \x1B[H → Move cursor to start of row 2
// *   - Clear Commands:
// *     - \x1B[2J → Clear screen (rows 2–30) and reset cursor to row 2
// *     - \x1B[K  → Clear from cursor to end of current line
// * - Scrolling: Shifts rows 2–29 upward when bottom of screen is reached
// * - Cursor: Blinks every 500ms using scanline-phase toggle logic
// * Memory Notes:
// * - charbuf: 80 columns x 30 rows = 2,400 bytes for screen text
// * - font_8x16.h: ~1.5 KB for ASCII 32–126 glyphs (8x16 pixels each)
// * - scanbuf (per scanline): 80 bytes reused each frame
// * - TMDS buffers, DMA queues, stack, and runtime variables consume additional memory
// * - Full framebuffer (640x480 = 307,200 bytes) intentionally avoided for SRAM efficiency
// * - Total memory usage remains well within RP2040’s 264 KB SRAM with room for expansion
// * - Hardware Setup: Uses adafruit_hdmi_sock_cfg, overclocked to 252MHz (VREG_VOLTAGE_1_20)
// * - Debugging: Serial output via stdio_init_all() (minicom -D /dev/ttyACM1)
// *   - Prints "Configuring DVI", "Prepare first scanline", "Core 1 start", and "Received: <ascii> (<char>)"
// *
// * I2C Master:
// * - Separate Pico sends keyboard input over I2C, accepting characters via serial (minicom -D /dev/ttyACM0)
// * - Wiring: GPIO 26 (SDA), 27 (SCL), 4.7k pull-ups, common GND
// *
// * Build Instructions:
// *   cd /home/noneya/pico/PicoDVI/software/build
// *   rm -rf *  # Clean build
// *   cmake -DPICO_SDK_PATH=/home/noneya/pico/pico-sdk \
// *         -DPICO_EXTRAS_PATH=/home/noneya/pico/pico-extras \
// *         -DPICO_PLATFORM=rp2040 \
// *         -DPICO_COPY_TO_RAM=1 \
// *         -DDVI_DEFAULT_SERIAL_CONFIG=adafruit_hdmi_sock_cfg \
// *         -DDVI_MONOCHROME_TMDS=1 \
// *         -DCMAKE_BUILD_TYPE=Debug ..
// *   make -j$(nproc)
// *   # Flash: Hold BOOTSEL, plug USB, release
// *   cp apps/my_terminal/my_terminal.uf2 /media/noneya/RPI-RP2/
// *
// * Dependencies:
// * - Pico SDK v2.1.0
// * - PicoDVI library (commit 7af20b2742c3dd0d7e7d3224078085ddea04a85f)
// * - font_8x16.h in /home/noneya/pico/PicoDVI/software/include
// *
// * Notes:
// * - Avoided dvi_framebuf_main_8bpp due to 307,200-byte framebuffer exceeding RP2040 SRAM
// * - Uses scanline rendering (dvi_scanbuf_main_8bpp) for memory efficiency
// * - I2C slave uses pico/i2c_slave for reliability; manual I2C handler available as fallback
// * - Tested with serial input from I2C master, including \r\n for Enter key
// */
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include "pico/stdlib.h"
// #include "pico/multicore.h"
// #include "hardware/clocks.h"
// #include "hardware/irq.h"
// #include "hardware/sync.h"
// #include "hardware/gpio.h"
// #include "hardware/vreg.h"
// #include "hardware/structs/bus_ctrl.h"
// #include "hardware/dma.h"
// #include "pico/i2c_slave.h"
// #include "dvi.h"
// #include "dvi_serialiser.h"
// #include "common_dvi_pin_configs.h"
// #include "tmds_encode.h"
// #include "font_8x16.h"
// #include "hardware/timer.h"

// #define I2C_SLAVE i2c1
// #define I2C_SDA_PIN 26
// #define I2C_SCL_PIN 27
// #define I2C_SLAVE_ADDR 0x55
// #define I2C_BAUD_RATE 100000
// #define FONT_CHAR_WIDTH 8
// #define FONT_CHAR_HEIGHT 16
// #define FONT_N_CHARS 95
// #define FONT_FIRST_ASCII 32
// #define FRAME_WIDTH 640
// #define FRAME_HEIGHT 480
// #define VREG_VSEL VREG_VOLTAGE_1_20
// #define DVI_TIMING dvi_timing_640x480p_60hz
// #define LED_PIN 16
// #define CURSOR_BLINK_MS 500

// struct dvi_inst dvi0;
// struct semaphore dvi_start_sem;
// #define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)  // 80
// #define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT) // 30
// static char charbuf[CHAR_ROWS * CHAR_COLS]; // 2400 bytes
// static volatile uint cursor_pos = CHAR_COLS; // Start on second row
// static volatile bool skip_next_lf = false; // Handle \r\n
// static volatile bool cursor_visible = true;
// static absolute_time_t last_blink_time;
// static volatile uint8_t escape_seq[3]; // Buffer for \x1B[? sequences
// static volatile uint escape_len = 0;


// // Escape sequence handling
// static volatile char esc_buffer[8];
// static volatile int esc_index = 0;
// static volatile bool in_escape = false;

// static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
//     if (event == I2C_SLAVE_RECEIVE) {
//         while (i2c_get_read_available(i2c)) {
//             uint8_t c = i2c_read_byte_raw(i2c);

//             // Start new escape sequence
//             if (!in_escape && c == 0x1B) {
//                 in_escape = true;
//                 esc_index = 0;
//                 esc_buffer[esc_index++] = c;
//                 continue;
//             } else if (in_escape) {
//                 if (esc_index < sizeof(esc_buffer) - 1) {
//                     esc_buffer[esc_index++] = c;
//                 }

//                 // Minimum expected: ESC [
//                 if (esc_index >= 2 &&
//                     esc_buffer[0] == 0x1B &&
//                     esc_buffer[1] == '[') {
                    
//                     char final = esc_buffer[esc_index - 1];
//                     uint row = cursor_pos / CHAR_COLS;
//                     uint col = cursor_pos % CHAR_COLS;

//                     // Arrow keys
//                     if (esc_index == 3 &&
//                         (final == 'A' || final == 'B' || final == 'C' || final == 'D')) {
//                         switch (final) {
//                             case 'A': if (row > 1) cursor_pos -= CHAR_COLS; break;
//                             case 'B': if (row < CHAR_ROWS - 1) cursor_pos += CHAR_COLS; break;
//                             case 'C': if (col < CHAR_COLS - 1) cursor_pos++; break;
//                             case 'D': if (col > 0) cursor_pos--; break;
//                         }
//                         in_escape = false;
//                         esc_index = 0;
//                         continue;
//                     }

//                     // Home cursor ESC[H
//                     if (esc_index == 3 && final == 'H') {
//                         cursor_pos = CHAR_COLS;
//                         in_escape = false;
//                         esc_index = 0;
//                         continue;
//                     }

//                     // Clear to end of line ESC[K
//                     if (esc_index == 3 && final == 'K') {
//                         memset(&charbuf[cursor_pos], ' ', CHAR_COLS - col);
//                         in_escape = false;
//                         esc_index = 0;
//                         continue;
//                     }

//                     // Clear screen ESC[2J
//                     if (esc_index == 4 &&
//                         esc_buffer[2] == '2' &&
//                         final == 'J') {
//                         memset(&charbuf[CHAR_COLS], ' ', (CHAR_ROWS - 1) * CHAR_COLS);
//                         cursor_pos = CHAR_COLS;
//                         in_escape = false;
//                         esc_index = 0;
//                         continue;
//                     }
//                 }

//                 // Reset on invalid or too long
//                 if (esc_index >= sizeof(esc_buffer) - 1) {
//                     in_escape = false;
//                     esc_index = 0;
//                 }
//                 continue;
//             }

//             // Handle backspace and DEL
//             if (c == '\b' || c == 127) {
//                 if (cursor_pos > CHAR_COLS) {
//                     cursor_pos--;
//                     charbuf[cursor_pos] = ' ';
//                 }
//                 continue;
//             }

//             // Printable characters
//             if (c >= 32 && c <= 126) {
//                 charbuf[cursor_pos++] = c;
//                 skip_next_lf = false;
//                 if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
//                     memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS],
//                             (CHAR_ROWS - 2) * CHAR_COLS);
//                     memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
//                     cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
//                 }
//                 continue;
//             }

//             // Newline / Carriage Return
//             if (c == '\r' || c == '\n') {
//                 if (c == '\n' && skip_next_lf) {
//                     skip_next_lf = false;
//                     continue;
//                 }
//                 skip_next_lf = (c == '\r');
//                 cursor_pos = ((cursor_pos / CHAR_COLS) + 1) * CHAR_COLS;
//                 if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
//                     memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS],
//                             (CHAR_ROWS - 2) * CHAR_COLS);
//                     memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
//                     cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
//                 }
//             }
//         }
//     }
// }









// // Helper function to reverse bits in a byte (fixes mirrored text)
// static inline uint8_t reverse_byte(uint8_t b) {
//     b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
//     b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
//     b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
//     return b;
// }

// static inline void prepare_scanline(const char *chars, uint y) {
//     static uint8_t scanbuf[FRAME_WIDTH / 8]; // 80 bytes
//     uint row = y / FONT_CHAR_HEIGHT;
//     uint char_idx_base = row * CHAR_COLS;

//     // Toggle cursor blink
//     if (to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(last_blink_time) > CURSOR_BLINK_MS) {
//         cursor_visible = !cursor_visible;
//         last_blink_time = get_absolute_time();
//     }

//     for (uint i = 0; i < CHAR_COLS; ++i) {
//         uint buf_idx = char_idx_base + i;
//         uint8_t byte;
//         if (buf_idx == cursor_pos && cursor_visible) {
//             byte = 0xFF; // Solid block cursor
//         } else {
//             uint c = chars[buf_idx];
//             if (c < FONT_FIRST_ASCII || c > FONT_FIRST_ASCII + FONT_N_CHARS - 1) c = ' ';
//             byte = font_8x16[(c - FONT_FIRST_ASCII) * FONT_CHAR_HEIGHT + (y % FONT_CHAR_HEIGHT)];
//         }
//         scanbuf[i] = reverse_byte(byte);
//     }

//     uint32_t *tmdsbuf;
//     queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
//     tmds_encode_1bpp((const uint32_t*)scanbuf, tmdsbuf, FRAME_WIDTH);
//     queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
// }

// void core1_scanline_callback() {
//     static uint y = 1;
//     prepare_scanline(charbuf, y);
//     y = (y + 1) % FRAME_HEIGHT;
// }

// void __not_in_flash("main") core1_main() {
//     dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
//     sem_acquire_blocking(&dvi_start_sem);
//     hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
//     dvi_start(&dvi0);
//     while (1) __wfi();
//     __builtin_unreachable();
// }

// int __not_in_flash("main") main() {
//     vreg_set_voltage(VREG_VSEL);
//     sleep_ms(10);
//     set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

//     stdio_init_all();
//     sleep_ms(1000); // Minimal delay for console

//     gpio_init(LED_PIN);
//     gpio_set_dir(LED_PIN, GPIO_OUT);

//     last_blink_time = get_absolute_time();

//     printf("Configuring DVI\n");
//     dvi0.timing = &DVI_TIMING;
//     dvi0.ser_cfg = adafruit_hdmi_sock_cfg;
//     dvi0.scanline_callback = core1_scanline_callback;
//     dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

//     printf("Prepare first scanline\n");
//     memset(charbuf, ' ', CHAR_ROWS * CHAR_COLS);
//     const char *custom_text = "Noneya's Terminal";
//     memcpy(charbuf, custom_text, strlen(custom_text));
//     prepare_scanline(charbuf, 0);

//     printf("Core 1 start\n");
//     sem_init(&dvi_start_sem, 0, 1);
//     multicore_launch_core1(core1_main);

//     sem_release(&dvi_start_sem);

//     i2c_init(I2C_SLAVE, I2C_BAUD_RATE);
//     gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
//     gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
//     gpio_pull_up(I2C_SDA_PIN);
//     gpio_pull_up(I2C_SCL_PIN);
//     i2c_slave_init(I2C_SLAVE, I2C_SLAVE_ADDR, &i2c_slave_handler);

//     while (1) {
//         gpio_put(LED_PIN, !gpio_get(LED_PIN)); // Blink LED
//         sleep_ms(500);
//     }
//     __builtin_unreachable();
// }




// /*
// * Project: Noneya's Terminal
// * Description: A monochrome 640x480 HDMI terminal displaying an 80x30 character grid (8x16 font)
// *   using an Adafruit HDMI sock on a Raspberry Pi Pico (RP2040). Receives keyboard input via I2C
// *   slave (GPIO 26 SDA, 27 SCL, address 0x55) from an I2C master Pico, which accepts serial input.
// *   Displays "Noneya's Terminal" on the first row, with subsequent rows showing keyboard input in
// *   real-time, supporting carriage return (\r), line feed (\n), or \r\n for line advances, with
// *   scrolling when the screen fills.
// *
// * Font:
// * - Source: IBM VGA 8x16 font (IBM_VGA_8x16.bin, 4096 bytes, 256 chars) from
// *   https://github.com/spacerace/romfont
// *   Another interesting source for fonts is https://int10h.org/oldschool-pc-fonts/download/, though conversion of these is not handled here.
// * - Conversion: Python script /home/noneya/pico/PicoDVI/software/fonts/convert_font.py
// *   - Generates font_8x16.h (1520 bytes, ASCII 32-126, 95 chars)
// *   - Each character is 8x16 pixels, stored as 16 bytes (1 bit per pixel)
// * - Include: Uses #include "font_8x16.h" (replaces font_8x8.h from PicoDVI examples)
// *
// * Key Features:
// * - Display: 640x480@60Hz, monochrome, using PicoDVI library (commit 7af20b2742c3dd0d7e7d3224078085ddea04a85f)
// * - Font Rendering: Scanline-based rendering with tmds_encode_1bpp for memory efficiency (80-byte scanbuf vs. 307,200-byte framebuffer)
// * - Bit Reversal: reverse_byte() fixes mirrored text in prepare_scanline()
// * - I2C Slave: Handles printable ASCII (32-126), \r (ASCII 13), and \n (ASCII 10)
// *   - \r, \n, or \r\n: Moves cursor to start of next row, scrolling if needed
// *   - \r\n sequence: Uses skip_next_lf flag to prevent double line advances
// *   - Backspace: Handles ASCII 8 to delete characters, preventing backspace into the title row
// *   - Arrow Keys: Supports \x1B[?A (up), \x1B[?B (down), \x1B[?C (right), \x1B[?D (left) for cursor movement
// * - Scrolling: Shifts rows 2-29 up, clears last row when cursor reaches row 30
// * - Cursor: Blinks every 500ms, toggling visibility in prepare_scanline()
// * Memory Notes:
// * - charbuf: 80 columns x 30 rows = 2,400 bytes for screen text
// * - font_8x16.h: ~1.5 KB for ASCII 32–126 glyphs (8x16 pixels each)
// * - scanbuf (per scanline): 80 bytes reused each frame
// * - TMDS buffers, DMA queues, stack, and runtime variables consume additional memory
// * - Full framebuffer (640x480 = 307,200 bytes) intentionally avoided for SRAM efficiency
// * - Total memory usage remains well within RP2040’s 264 KB SRAM with room for expansion
// * - Hardware Setup: Uses adafruit_hdmi_sock_cfg, overclocked to 252MHz (VREG_VOLTAGE_1_20)
// * - Debugging: Serial output via stdio_init_all() (minicom -D /dev/ttyACM1)
// *   - Prints "Configuring DVI", "Prepare first scanline", "Core 1 start", and "Received: <ascii> (<char>)"
// *
// * I2C Master:
// * - Separate Pico sends keyboard input over I2C, accepting characters via serial (minicom -D /dev/ttyACM0)
// * - Wiring: GPIO 26 (SDA), 27 (SCL), 4.7k pull-ups, common GND
// *
// * Build Instructions:
// *   cd /home/noneya/pico/PicoDVI/software/build
// *   rm -rf *  # Clean build
// *   cmake -DPICO_SDK_PATH=/home/noneya/pico/pico-sdk \
// *         -DPICO_EXTRAS_PATH=/home/noneya/pico/pico-extras \
// *         -DPICO_PLATFORM=rp2040 \
// *         -DPICO_COPY_TO_RAM=1 \
// *         -DDVI_DEFAULT_SERIAL_CONFIG=adafruit_hdmi_sock_cfg \
// *         -DDVI_MONOCHROME_TMDS=1 \
// *         -DCMAKE_BUILD_TYPE=Debug ..
// *   make -j$(nproc)
// *   # Flash: Hold BOOTSEL, plug USB, release
// *   cp apps/my_terminal/my_terminal.uf2 /media/noneya/RPI-RP2/
// *
// * Dependencies:
// * - Pico SDK v2.1.0
// * - PicoDVI library (commit 7af20b2742c3dd0d7e7d3224078085ddea04a85f)
// * - font_8x16.h in /home/noneya/pico/PicoDVI/software/include
// *
// * Notes:
// * - Avoided dvi_framebuf_main_8bpp due to 307,200-byte framebuffer exceeding RP2040 SRAM
// * - Uses scanline rendering (dvi_scanbuf_main_8bpp) for memory efficiency
// * - I2C slave uses pico/i2c_slave for reliability; manual I2C handler available as fallback
// * - Tested with serial input from I2C master, including \r\n for Enter key
// */
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include "pico/stdlib.h"
// #include "pico/multicore.h"
// #include "hardware/clocks.h"
// #include "hardware/irq.h"
// #include "hardware/sync.h"
// #include "hardware/gpio.h"
// #include "hardware/vreg.h"
// #include "hardware/structs/bus_ctrl.h"
// #include "hardware/dma.h"
// #include "pico/i2c_slave.h"
// #include "dvi.h"
// #include "dvi_serialiser.h"
// #include "common_dvi_pin_configs.h"
// #include "tmds_encode.h"
// #include "font_8x16.h"
// #include "hardware/timer.h"

// #define I2C_SLAVE i2c1
// #define I2C_SDA_PIN 26
// #define I2C_SCL_PIN 27
// #define I2C_SLAVE_ADDR 0x55
// #define I2C_BAUD_RATE 100000
// #define FONT_CHAR_WIDTH 8
// #define FONT_CHAR_HEIGHT 16
// #define FONT_N_CHARS 95
// #define FONT_FIRST_ASCII 32
// #define FRAME_WIDTH 640
// #define FRAME_HEIGHT 480
// #define VREG_VSEL VREG_VOLTAGE_1_20
// #define DVI_TIMING dvi_timing_640x480p_60hz
// #define LED_PIN 16
// #define CURSOR_BLINK_MS 500

// struct dvi_inst dvi0;
// struct semaphore dvi_start_sem;
// #define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)  // 80
// #define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT) // 30
// static char charbuf[CHAR_ROWS * CHAR_COLS]; // 2400 bytes
// static volatile uint cursor_pos = CHAR_COLS; // Start on second row
// static volatile bool skip_next_lf = false; // Handle \r\n
// static volatile bool cursor_visible = true;
// static absolute_time_t last_blink_time;
// static volatile uint8_t escape_seq[3]; // Buffer for \x1B[? sequences
// static volatile uint escape_len = 0;

// // I2C slave handler
// static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
//     if (event == I2C_SLAVE_RECEIVE) {
//         uint8_t c = i2c_read_byte_raw(i2c);

//         // Handle escape sequences
//         if (escape_len == 0 && c == 0x1B) { // ESC
//             escape_seq[0] = c;
//             escape_len = 1;
//             return;
//         } else if (escape_len == 1 && c == '[') {
//             escape_seq[1] = c;
//             escape_len = 2;
//             return;
//         } else if (escape_len == 2) {
//             escape_seq[2] = c;
//             escape_len = 0;
//             uint cursor_row = cursor_pos / CHAR_COLS;
//             uint cursor_col = cursor_pos % CHAR_COLS;
//             switch (c) {
//                 case 'A': // Up
//                     if (cursor_row > 1) cursor_pos -= CHAR_COLS;
//                     break;
//                 case 'B': // Down
//                     if (cursor_row < CHAR_ROWS - 1) cursor_pos += CHAR_COLS;
//                     break;
//                 case 'C': // Right
//                     if (cursor_col < CHAR_COLS - 1) cursor_pos++;
//                     break;
//                 case 'D': // Left
//                     if (cursor_col > 0) cursor_pos--;
//                     break;
//                 case '2': // Expect 'J' for clear screen (\x1B[2J)
//                     if (i2c_read_byte_raw(i2c) == 'J') {
//                          memset(&charbuf[CHAR_COLS], ' ', (CHAR_ROWS - 1) * CHAR_COLS);
//                          cursor_pos = CHAR_COLS;
//                     }
//                     break;
//             }
//             return;
//         }

//         // Reset escape sequence if invalid
//         escape_len = 0;

//         if (c == '\b' || c == 127) { // Backspace or DEL
//             if (cursor_pos > CHAR_COLS) { // Don't back into title row
//                 cursor_pos--;
//                 charbuf[cursor_pos] = ' ';
//             }
//             return;
//         }

//         if (c >= 32 && c <= 126) { // Printable ASCII
//             charbuf[cursor_pos++] = c;
//             skip_next_lf = false;
//             if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
//                 memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS], (CHAR_ROWS - 2) * CHAR_COLS);
//                 memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
//                 cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
//             }
//         } else if (c == '\r' || c == '\n') { // Handle \r, \n, or \r\n
//             if (c == '\n' && skip_next_lf) {
//                 skip_next_lf = false;
//                 return;
//             }
//             skip_next_lf = (c == '\r');
//             cursor_pos = ((cursor_pos / CHAR_COLS) + 1) * CHAR_COLS;
//             if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
//                 memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS], (CHAR_ROWS - 2) * CHAR_COLS);
//                 memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
//                 cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
//             }
//         }
//     }
// }

// // Helper function to reverse bits in a byte (fixes mirrored text)
// static inline uint8_t reverse_byte(uint8_t b) {
//     b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
//     b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
//     b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
//     return b;
// }

// static inline void prepare_scanline(const char *chars, uint y) {
//     static uint8_t scanbuf[FRAME_WIDTH / 8]; // 80 bytes
//     uint row = y / FONT_CHAR_HEIGHT;
//     uint char_idx_base = row * CHAR_COLS;

//     // Toggle cursor blink
//     if (to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(last_blink_time) > CURSOR_BLINK_MS) {
//         cursor_visible = !cursor_visible;
//         last_blink_time = get_absolute_time();
//     }

//     for (uint i = 0; i < CHAR_COLS; ++i) {
//         uint buf_idx = char_idx_base + i;
//         uint8_t byte;
//         if (buf_idx == cursor_pos && cursor_visible) {
//             byte = 0xFF; // Solid block cursor
//         } else {
//             uint c = chars[buf_idx];
//             if (c < FONT_FIRST_ASCII || c > FONT_FIRST_ASCII + FONT_N_CHARS - 1) c = ' ';
//             byte = font_8x16[(c - FONT_FIRST_ASCII) * FONT_CHAR_HEIGHT + (y % FONT_CHAR_HEIGHT)];
//         }
//         scanbuf[i] = reverse_byte(byte);
//     }

//     uint32_t *tmdsbuf;
//     queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
//     tmds_encode_1bpp((const uint32_t*)scanbuf, tmdsbuf, FRAME_WIDTH);
//     queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
// }

// void core1_scanline_callback() {
//     static uint y = 1;
//     prepare_scanline(charbuf, y);
//     y = (y + 1) % FRAME_HEIGHT;
// }

// void __not_in_flash("main") core1_main() {
//     dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
//     sem_acquire_blocking(&dvi_start_sem);
//     hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
//     dvi_start(&dvi0);
//     while (1) __wfi();
//     __builtin_unreachable();
// }

// int __not_in_flash("main") main() {
//     vreg_set_voltage(VREG_VSEL);
//     sleep_ms(10);
//     set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

//     stdio_init_all();
//     sleep_ms(1000); // Minimal delay for console

//     gpio_init(LED_PIN);
//     gpio_set_dir(LED_PIN, GPIO_OUT);

//     last_blink_time = get_absolute_time();

//     printf("Configuring DVI\n");
//     dvi0.timing = &DVI_TIMING;
//     dvi0.ser_cfg = adafruit_hdmi_sock_cfg;
//     dvi0.scanline_callback = core1_scanline_callback;
//     dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

//     printf("Prepare first scanline\n");
//     memset(charbuf, ' ', CHAR_ROWS * CHAR_COLS);
//     const char *custom_text = "Noneya's Terminal";
//     memcpy(charbuf, custom_text, strlen(custom_text));
//     prepare_scanline(charbuf, 0);

//     printf("Core 1 start\n");
//     sem_init(&dvi_start_sem, 0, 1);
//     multicore_launch_core1(core1_main);

//     sem_release(&dvi_start_sem);

//     i2c_init(I2C_SLAVE, I2C_BAUD_RATE);
//     gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
//     gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
//     gpio_pull_up(I2C_SDA_PIN);
//     gpio_pull_up(I2C_SCL_PIN);
//     i2c_slave_init(I2C_SLAVE, I2C_SLAVE_ADDR, &i2c_slave_handler);

//     while (1) {
//         gpio_put(LED_PIN, !gpio_get(LED_PIN)); // Blink LED
//         sleep_ms(500);
//     }
//     __builtin_unreachable();
// }









//  /*
//  * Project: Noneya's Terminal
//  * Description: A monochrome 640x480 HDMI terminal displaying an 80x30 character grid (8x16 font)
//  *   using an Adafruit HDMI sock on a Raspberry Pi Pico (RP2040). Receives keyboard input via I2C
//  *   slave (GPIO 26 SDA, 27 SCL, address 0x55) from an I2C master Pico, which accepts serial input.
//  *   Displays "Noneya's Terminal" on the first row, with subsequent rows showing keyboard input in
//  *   real-time, supporting carriage return (\r), line feed (\n), or \r\n for line advances, with
//  *   scrolling when the screen fills.
//  *
//  * Font:
//  * - Source: IBM VGA 8x16 font (IBM_VGA_8x16.bin, 4096 bytes, 256 chars) from
//  *   https://github.com/spacerace/romfont
//  *   Another interesting source for fonts is https://int10h.org/oldschool-pc-fonts/download/, though conversion of these is not handled here.
//  * - Conversion: Python script /home/noneya/pico/PicoDVI/software/fonts/convert_font.py
//  *   - Generates font_8x16.h (1520 bytes, ASCII 32-126, 95 chars)
//  *   - Each character is 8x16 pixels, stored as 16 bytes (1 bit per pixel)
//  * - Include: Uses #include "font_8x16.h" (replaces font_8x8.h from PicoDVI examples)
//  *
//  * Key Features:
//  * - Display: 640x480@60Hz, monochrome, using PicoDVI library (commit 7af20b2742c3dd0d7e7d3224078085ddea04a85f)
//  * - Font Rendering: Scanline-based rendering with tmds_encode_1bpp for memory efficiency (80-byte scanbuf vs. 307,200-byte framebuffer)
//  * - Bit Reversal: reverse_byte() fixes mirrored text in prepare_scanline()
//  * - I2C Slave: Handles printable ASCII (32-126), \r (ASCII 13), and \n (ASCII 10)
//  *   - \r, \n, or \r\n: Moves cursor to start of next row, scrolling if needed
//  *   - \r\n sequence: Uses skip_next_lf flag to prevent double line advances
//  *   - Backspace: Handles ASCII 8 to delete characters, preventing backspace into the title row
//  * - Scrolling: Shifts rows 2-29 up, clears last row when cursor reaches row 30
//  * - Cursor: Blinks every 500ms, toggling visibility in prepare_scanline()
//  * Memory Notes:
//  * - charbuf: 80 columns x 30 rows = 2,400 bytes for screen text
//  * - font_8x16.h: ~1.5 KB for ASCII 32–126 glyphs (8x16 pixels each)
//  * - scanbuf (per scanline): 80 bytes reused each frame
//  * - TMDS buffers, DMA queues, stack, and runtime variables consume additional memory
//  * - Full framebuffer (640x480 = 307,200 bytes) intentionally avoided for SRAM efficiency
//  * - Total memory usage remains well within RP2040’s 264 KB SRAM with room for expansion

//  * - Hardware Setup: Uses adafruit_hdmi_sock_cfg, overclocked to 252MHz (VREG_VOLTAGE_1_20)
//  * - Debugging: Serial output via stdio_init_all() (minicom -D /dev/ttyACM1)
//  *   - Prints "Configuring DVI", "Prepare first scanline", "Core 1 start", and "Received: <ascii> (<char>)"
//  *
//  * I2C Master:
//  * - Separate Pico sends keyboard input over I2C, accepting characters via serial (minicom -D /dev/ttyACM0)
//  * - Wiring: GPIO 26 (SDA), 27 (SCL), 4.7k pull-ups, common GND
//  *
//  * Build Instructions:
//  *   cd /home/noneya/pico/PicoDVI/software/build
//  *   rm -rf *  # Clean build
//  *   cmake -DPICO_SDK_PATH=/home/noneya/pico/pico-sdk \
//  *         -DPICO_EXTRAS_PATH=/home/noneya/pico/pico-extras \
//  *         -DPICO_PLATFORM=rp2040 \
//  *         -DPICO_COPY_TO_RAM=1 \
//  *         -DDVI_DEFAULT_SERIAL_CONFIG=adafruit_hdmi_sock_cfg \
//  *         -DDVI_MONOCHROME_TMDS=1 \
//  *         -DCMAKE_BUILD_TYPE=Debug ..
//  *   make -j$(nproc)
//  *   # Flash: Hold BOOTSEL, plug USB, release
//  *   cp apps/my_terminal/my_terminal.uf2 /media/noneya/RPI-RP2/
//  *
//  * Dependencies:
//  * - Pico SDK v2.1.0
//  * - PicoDVI library (commit 7af20b2742c3dd0d7e7d3224078085ddea04a85f)
//  * - font_8x16.h in /home/noneya/pico/PicoDVI/software/include
//  *
//  * Notes:
//  * - Avoided dvi_framebuf_main_8bpp due to 307,200-byte framebuffer exceeding RP2040 SRAM
//  * - Uses scanline rendering (dvi_scanbuf_main_8bpp) for memory efficiency
//  * - I2C slave uses pico/i2c_slave for reliability; manual I2C handler available as fallback
//  * - Tested with serial input from I2C master, including \r\n for Enter key
//  */
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include "pico/stdlib.h"
// #include "pico/multicore.h"
// #include "hardware/clocks.h"
// #include "hardware/irq.h"
// #include "hardware/sync.h"
// #include "hardware/gpio.h"
// #include "hardware/vreg.h"
// #include "hardware/structs/bus_ctrl.h"
// #include "hardware/dma.h"
// #include "pico/i2c_slave.h"
// #include "dvi.h"
// #include "dvi_serialiser.h"
// #include "common_dvi_pin_configs.h"
// #include "tmds_encode.h"
// #include "font_8x16.h"
// #include "hardware/timer.h"
// static bool cursor_visible = true;
// absolute_time_t last_blink_time;
// const uint CURSOR_BLINK_MS = 500;


// #define I2C_SLAVE i2c1
// #define I2C_SDA_PIN 26
// #define I2C_SCL_PIN 27
// #define I2C_SLAVE_ADDR 0x55
// #define I2C_BAUD_RATE 100000
// #define FONT_CHAR_WIDTH 8
// #define FONT_CHAR_HEIGHT 16
// #define FONT_N_CHARS 95
// #define FONT_FIRST_ASCII 32
// #define FRAME_WIDTH 640
// #define FRAME_HEIGHT 480
// #define VREG_VSEL VREG_VOLTAGE_1_20
// #define DVI_TIMING dvi_timing_640x480p_60hz
// #define LED_PIN 16

// struct dvi_inst dvi0;
// struct semaphore dvi_start_sem;
// #define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
// #define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)
// static char charbuf[CHAR_ROWS * CHAR_COLS];
// static volatile uint cursor_pos = CHAR_COLS; // Start on second row
// static volatile bool skip_next_lf = false; // Flag to handle \r\n

// // I2C slave handler
// static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
//     if (event == I2C_SLAVE_RECEIVE) {
//         uint8_t c = i2c_read_byte_raw(i2c);
//         printf("Received: %d (%c)\n", c, (c >= 32 && c <= 126) ? c : '.'); // Debug
//         if (c >= 32 && c <= 126) { // Printable ASCII
//             charbuf[cursor_pos++] = c;
//             skip_next_lf = false; // Reset flag for printable characters
//             if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
//                 // Scroll up
//                 memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS], (CHAR_ROWS - 2) * CHAR_COLS);
//                 memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
//                 cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
//             }
//         } else if (c == '\r' || c == '\n') { // Handle \r, \n, or \r\n
//             if (c == '\n' && skip_next_lf) {
//                 skip_next_lf = false; // Skip \n after \r
//                 return;
//             }
//             skip_next_lf = (c == '\r'); // Set flag if \r received
//             cursor_pos = ((cursor_pos / CHAR_COLS) + 1) * CHAR_COLS; // Move to start of next row
//             if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
//                 // Scroll up
//                 memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS], (CHAR_ROWS - 2) * CHAR_COLS);
//                 memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
//                 cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
//             }
//         } else if (c == 8) {  // Backspace (ASCII 8)
//             if (cursor_pos > CHAR_COLS) {  // Don't back into the title row
//                 cursor_pos--;
//                 charbuf[cursor_pos] = ' ';
//             }
//          }
//     }
// }

// // Helper function to reverse bits in a byte (fixes mirrored text)
// static inline uint8_t reverse_byte(uint8_t b) {
//     b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
//     b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
//     b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
//     return b;
// }

// static inline void prepare_scanline(const char *chars, uint y) {
//     static uint8_t scanbuf[FRAME_WIDTH / 8];
//     uint row = y / FONT_CHAR_HEIGHT;
//     uint char_idx_base = row * CHAR_COLS;
    
//     // Toggle cursor blink
//     if (to_ms_since_boot(get_absolute_time()) - to_ms_since_boot(last_blink_time) > CURSOR_BLINK_MS) {
//         cursor_visible = !cursor_visible;
//         last_blink_time = get_absolute_time();
//     }

//     for (uint i = 0; i < CHAR_COLS; ++i) {
//         uint buf_idx = char_idx_base + i;
//         uint8_t byte;

//         if (buf_idx == cursor_pos && cursor_visible) {
//             // Render a solid block cursor or inverted char (your choice)
//             byte = 0xFF; // Solid block
//         } else {
//             uint c = chars[buf_idx];
//             byte = font_8x16[(c - FONT_FIRST_ASCII) * FONT_CHAR_HEIGHT + (y % FONT_CHAR_HEIGHT)];
//         }

//         scanbuf[i] = reverse_byte(byte);
//     }

//     uint32_t *tmdsbuf;
//     queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
//     tmds_encode_1bpp((const uint32_t*)scanbuf, tmdsbuf, FRAME_WIDTH);
//     queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
// }

// // static inline void prepare_scanline(const char *chars, uint y) {
// //     static uint8_t scanbuf[FRAME_WIDTH / 8];
// //     for (uint i = 0; i < CHAR_COLS; ++i) {
// //         uint c = chars[i + y / FONT_CHAR_HEIGHT * CHAR_COLS];
// //         scanbuf[i] = reverse_byte(font_8x16[(c - FONT_FIRST_ASCII) * FONT_CHAR_HEIGHT + (y % FONT_CHAR_HEIGHT)]);
// //     }
// //     uint32_t *tmdsbuf;
// //     queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
// //     tmds_encode_1bpp((const uint32_t*)scanbuf, tmdsbuf, FRAME_WIDTH);
// //     queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
// // }

// void core1_scanline_callback() {
//     static uint y = 1;
//     prepare_scanline(charbuf, y);
//     y = (y + 1) % FRAME_HEIGHT;
// }

// void __not_in_flash("main") core1_main() {
//     dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
//     sem_acquire_blocking(&dvi_start_sem);
//     hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
//     dvi_start(&dvi0);
//     while (1) 
//         __wfi();
//     __builtin_unreachable();
// }

// int __not_in_flash("main") main() {
//     vreg_set_voltage(VREG_VSEL);
//     sleep_ms(10);
//     set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

//     stdio_init_all();

//     gpio_init(LED_PIN);
//     gpio_set_dir(LED_PIN, GPIO_OUT);

//     last_blink_time = get_absolute_time();

//     printf("Configuring DVI\n");

//     dvi0.timing = &DVI_TIMING;
//     dvi0.ser_cfg = adafruit_hdmi_sock_cfg;
//     dvi0.scanline_callback = core1_scanline_callback;
//     dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

//     printf("Prepare first scanline\n");
//     for (int i = 0; i < CHAR_ROWS * CHAR_COLS; ++i)
//         charbuf[i] = ' ';
//     const char *custom_text = "Noneya's Terminal";
//     for (int i = 0; i < strlen(custom_text) && i < CHAR_COLS; ++i)
//         charbuf[i] = custom_text[i];
//     prepare_scanline(charbuf, 0);

//     printf("Core 1 start\n");
//     sem_init(&dvi_start_sem, 0, 1);
//     multicore_launch_core1(core1_main);

//     sem_release(&dvi_start_sem);

//     i2c_init(I2C_SLAVE, I2C_BAUD_RATE);
//     gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
//     gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
//     gpio_pull_up(I2C_SDA_PIN);
//     gpio_pull_up(I2C_SCL_PIN);
//     i2c_slave_init(I2C_SLAVE, I2C_SLAVE_ADDR, &i2c_slave_handler);

//     while (1)
//         __wfi();
//     __builtin_unreachable();
// }





// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include "pico/stdlib.h"
// #include "pico/multicore.h"
// #include "hardware/clocks.h"
// #include "hardware/irq.h"
// #include "hardware/sync.h"
// #include "hardware/gpio.h"
// #include "hardware/vreg.h"
// #include "hardware/structs/bus_ctrl.h"
// #include "hardware/dma.h"
// #include "pico/i2c_slave.h"
// #include "dvi.h"
// #include "dvi_serialiser.h"
// #include "common_dvi_pin_configs.h"
// #include "tmds_encode.h"
// #include "font_8x16.h"

// #define I2C_SLAVE i2c1
// #define I2C_SDA_PIN 26
// #define I2C_SCL_PIN 27
// #define I2C_SLAVE_ADDR 0x55
// #define I2C_BAUD_RATE 100000
// #define FONT_CHAR_WIDTH 8
// #define FONT_CHAR_HEIGHT 16
// #define FONT_N_CHARS 95
// #define FONT_FIRST_ASCII 32
// #define FRAME_WIDTH 640
// #define FRAME_HEIGHT 480
// #define VREG_VSEL VREG_VOLTAGE_1_20
// #define DVI_TIMING dvi_timing_640x480p_60hz
// #define LED_PIN 16

// struct dvi_inst dvi0;
// struct semaphore dvi_start_sem;
// #define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
// #define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)
// static char charbuf[CHAR_ROWS * CHAR_COLS];
// static volatile uint cursor_pos = CHAR_COLS; // Start on second row

// // I2C slave handler
// static void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
//     if (event == I2C_SLAVE_RECEIVE) {
//         uint8_t c = i2c_read_byte_raw(i2c);
//         if (c >= 32 && c <= 126) { // Printable ASCII
//             charbuf[cursor_pos++] = c;
//             if (cursor_pos >= CHAR_ROWS * CHAR_COLS) {
//                 // Scroll up
//                 memmove(&charbuf[CHAR_COLS], &charbuf[2 * CHAR_COLS], (CHAR_ROWS - 2) * CHAR_COLS);
//                 memset(&charbuf[(CHAR_ROWS - 1) * CHAR_COLS], ' ', CHAR_COLS);
//                 cursor_pos = (CHAR_ROWS - 1) * CHAR_COLS;
//             }
//             printf("Received: %c\n", c); // Debug
//         }
//     }
// }

// // Helper function to reverse bits in a byte (fixes mirrored text)
// static inline uint8_t reverse_byte(uint8_t b) {
//     b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
//     b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
//     b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
//     return b;
// }

// static inline void prepare_scanline(const char *chars, uint y) {
//     static uint8_t scanbuf[FRAME_WIDTH / 8];
//     for (uint i = 0; i < CHAR_COLS; ++i) {
//         uint c = chars[i + y / FONT_CHAR_HEIGHT * CHAR_COLS];
//         scanbuf[i] = reverse_byte(font_8x16[(c - FONT_FIRST_ASCII) * FONT_CHAR_HEIGHT + (y % FONT_CHAR_HEIGHT)]);
//     }
//     uint32_t *tmdsbuf;
//     queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
//     tmds_encode_1bpp((const uint32_t*)scanbuf, tmdsbuf, FRAME_WIDTH);
//     queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
// }

// void core1_scanline_callback() {
//     static uint y = 1;
//     prepare_scanline(charbuf, y);
//     y = (y + 1) % FRAME_HEIGHT;
// }

// void __not_in_flash("main") core1_main() {
//     dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
//     sem_acquire_blocking(&dvi_start_sem);
//     hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
//     dvi_start(&dvi0);
//     while (1) 
//         __wfi();
//     __builtin_unreachable();
// }

// int __not_in_flash("main") main() {
//     vreg_set_voltage(VREG_VSEL);
//     sleep_ms(10);
//     set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

//     stdio_init_all();

//     gpio_init(LED_PIN);
//     gpio_set_dir(LED_PIN, GPIO_OUT);

//     printf("Configuring DVI\n");

//     dvi0.timing = &DVI_TIMING;
//     dvi0.ser_cfg = adafruit_hdmi_sock_cfg;
//     dvi0.scanline_callback = core1_scanline_callback;
//     dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

//     printf("Prepare first scanline\n");
//     for (int i = 0; i < CHAR_ROWS * CHAR_COLS; ++i)
//         charbuf[i] = ' ';
//     const char *custom_text = "Noneya's Terminal";
//     for (int i = 0; i < strlen(custom_text) && i < CHAR_COLS; ++i)
//         charbuf[i] = custom_text[i];
//     prepare_scanline(charbuf, 0);

//     printf("Core 1 start\n");
//     sem_init(&dvi_start_sem, 0, 1);
//     multicore_launch_core1(core1_main);

//     sem_release(&dvi_start_sem);

//     i2c_init(I2C_SLAVE, I2C_BAUD_RATE);
//     gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
//     gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
//     gpio_pull_up(I2C_SDA_PIN);
//     gpio_pull_up(I2C_SCL_PIN);
//     i2c_slave_init(I2C_SLAVE, I2C_SLAVE_ADDR, &i2c_slave_handler);

//     while (1)
//         __wfi();
//     __builtin_unreachable();
// }
