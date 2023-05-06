#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/dma.h"
#include "pico/sem.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"

#include "gen.h"
#include "scanout.h"
#include "text.h"

#include "font_8x8.h"
#define FONT_CHAR_WIDTH 8
#define FONT_CHAR_HEIGHT 8
#define FONT_N_CHARS 95
#define FONT_FIRST_ASCII 32

#include "font.h"


// Pick one:
#define MODE_640x480_60Hz
// #define MODE_800x600_60Hz
// #define MODE_960x540p_60Hz
// #define MODE_1280x720_30Hz

#if defined(MODE_640x480_60Hz)
// DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

#elif defined(MODE_800x600_60Hz)
// DVDD 1.3V, going downhill with a tailwind
#define FRAME_WIDTH 800
#define FRAME_HEIGHT 600
#define VREG_VSEL VREG_VOLTAGE_1_30
#define DVI_TIMING dvi_timing_800x600p_60hz


#elif defined(MODE_960x540p_60Hz)
// DVDD 1.25V (slower silicon may need the full 1.3, or just not work)
#define FRAME_WIDTH 960
#define FRAME_HEIGHT 540
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_960x540p_60hz

#elif defined(MODE_1280x720_30Hz)
// 1280x720p 30 Hz (nonstandard)
// DVDD 1.25V (slower silicon may need the full 1.3, or just not work)
#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_1280x720p_30hz

#else
#error "Select a video mode!"
#endif

#define LED_PIN PICO_DEFAULT_LED_PIN

struct dvi_inst dvi0;
struct semaphore dvi_start_sem;

#define CHAR_COLS (FRAME_WIDTH / FONT_CHAR_WIDTH)
#define CHAR_ROWS (FRAME_HEIGHT / FONT_CHAR_HEIGHT)
char charbuf[CHAR_ROWS * CHAR_COLS];

#if DVI_MONOCHROME_TMDS
#define N_CHAN 1
#else
#define N_CHAN 3
#endif
#define N_PIX_BUFFERS 5

static inline void font_scanline(uint8_t *scanbuf, const char *chars, uint y) {
	memcpy(scanbuf, font_bits + (y % FONT_HEIGHT) * (FONT_STRIDE / 4), FRAME_WIDTH / 8);
}

// Info needed to create one scanline of TMDS
struct scan_display_list {
	uint32_t bwbuf[FRAME_WIDTH / 8];
	uint32_t scanlist[16];
};

struct scan_display_list display_lists[N_PIX_BUFFERS];

uint32_t global_pal[16];
uint32_t global_pal2[16];

struct render_slot {
	uint cpu_assigned;
	uint complete;
	uint y;
	struct scan_display_list *dlist;
};

struct render_state {
	struct render_slot slots[N_PIX_BUFFERS];
	uint slot_ix;
	uint y;
	queue_t q_core1_req;
};

struct render_state global_render_state;

struct scanlist sl;

// Encode one scanline from display list to TMDS
// Runs on core 1 irq, could be in scratch
void __not_in_flash("main") encode_scanline(uint32_t *tmdsbuf, struct scan_display_list *dl, uint y) {
	tmds_scan(dl->scanlist, dl->bwbuf, tmdsbuf, FRAME_WIDTH * 2);
}

// https://iamkate.com/data/12-bit-rainbow/
static uint16_t palette[12] = {
	0x817,
	0xa35,
	0xc66,
	0xe94,
	0xed0,
	0x9d5,
	0x4d8,
	0x2cb,
	0x0bc,
	0x09c,
	0x36b,
	0x639,
};

#define LEADING 2

struct text_line textlines[64];
uint n_textlines = 0;

// Render one scanline to a display list.
// Runs on either core, on irq on core 0 or in main loop on core 1
struct scan_display_list * __not_in_flash("main") render_scanline(struct render_slot *slot, uint slot_ix) {
	uint y = slot->y;
	struct scan_display_list *dlist = &display_lists[slot_ix];
	uint this_y = y % (FONT_HEIGHT + LEADING);
	uint line_num = y / (FONT_HEIGHT + LEADING);
	uint32_t *scan = dlist->scanlist;
	if (this_y < FONT_HEIGHT && line_num < n_textlines) {
		uint width = textlines[line_num].width;
		render_text_scanline(dlist->bwbuf, textlines[line_num].runs, this_y);
		uint j = 0;
		if (width > 0) {
			scan[j++] = (uint32_t)tmds_scan_1bpp_pal;
			scan[j++] = width / 2;
			scan[j++] = (uint32_t)textlines[line_num].palette;
		}
		if (width < FRAME_WIDTH) {
			scan[j++] = (uint32_t)tmds_scan_solid;
			scan[j++] = (FRAME_WIDTH - width) / 2;
			scan[j++] = 0;
		}
		scan[j] = (uint32_t)tmds_scan_stop;
	} else {
		scan[0] = (uint32_t)tmds_scan_solid_tmds;
		scan[1] = FRAME_WIDTH / 2;
		scan[2] = 0x7fd00u;
		scan[3] = 0x7fd00u;
		scan[4] = 0x7fd00u;
		scan[5] = (uint32_t)tmds_scan_stop;
	}
	return dlist;
}

static uint32_t pixline[N_CHAN * FRAME_WIDTH / 8];

void prepare_pixline(void) {
	for (uint chan = 0; chan < N_CHAN; chan++) {
		uint32_t *out = pixline + chan * (FRAME_WIDTH / 8);
		for (uint i = 0; i < FRAME_WIDTH / 8; i++) {
			uint32_t g = (i & 15) * 0x11111111;
			if (i >= 4 && i < 16) {
				g = ((palette[i - 4] >> (4 * chan)) & 15) * 0x11111111;
			}
			out[i] = g;
		}
		out[0] = 0x76543210;
		out[1] = 0xfedcba98;
		out[2] = 0x89abcdef;
		out[3] = 0x01234567;
	}
}

static uint late_scanline_count = 0;

static inline void tmds_encode_scanline(void) {
	uint32_t *pixbuf;
	uint32_t *tmdsbuf;
	while (late_scanline_count > 0 && queue_try_remove(&dvi0.q_colour_valid, &pixbuf)) {
		queue_add_blocking(&dvi0.q_colour_free, &pixbuf);
		late_scanline_count--;
	}
	if (!queue_try_remove(&dvi0.q_colour_valid, &pixbuf)) {
		pixbuf = NULL;
		late_scanline_count++;
	}
	queue_remove_blocking(&dvi0.q_tmds_free, &tmdsbuf);
	for (uint chan = 0; chan < N_CHAN; chan++) {
		uint32_t *out = tmdsbuf + chan * (FRAME_WIDTH / 2);
		if (pixbuf != NULL) {
			tmds_encode_4bpp(pixbuf + chan * (FRAME_WIDTH / 8), out, FRAME_WIDTH);
		} else {
			uint32_t word = 0x7fd00;
			for (uint j = 0; j < FRAME_WIDTH / 2; j++) {
				out[j] = word;
			}
		}
	}
	queue_add_blocking(&dvi0.q_tmds_valid, &tmdsbuf);
	if (pixbuf) {
		queue_add_blocking(&dvi0.q_colour_free, &pixbuf);
	}
}

void dummy_tmds() {
	uint32_t *tmdsbuf;
	queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
	for (uint chan = 0; chan < N_TMDS_LANES; chan++) {
		uint32_t tmds = chan == 1 ? 0xbfe00 : 0x7fd00;
		uint32_t *out = tmdsbuf + chan * FRAME_WIDTH / 2;
		for (uint i = 0; i < FRAME_WIDTH / 2; i++) {
			out[i] = tmds;
		}
	}
	queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
}

void __not_in_flash("main") core1_scanline_callback() {
	uint32_t *tmdsbuf;
	queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
	uint slot_ix = global_render_state.slot_ix;
	struct render_slot *slot = &global_render_state.slots[slot_ix];
	// Access to slot->complete is conceptually acquire load
	if (slot->cpu_assigned < 0x80000000 && slot->complete) {
		encode_scanline(tmdsbuf, slot->dlist, slot->y);
	} else {
		// underrun
		slot->cpu_assigned = ~0;
		for (uint chan = 0; chan < N_TMDS_LANES; chan++) {
			uint32_t tmds = chan == 1 ? 0x7fd00 : 0xbfe00;
			uint32_t *out = tmdsbuf + chan * FRAME_WIDTH / 2;
			for (uint i = 0; i < FRAME_WIDTH / 2; i++) {
				out[i] = tmds;
			}
		}
	}
	queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);

	// Choose a CPU
	uint n_in_flight = 0;
	for (uint i = 1; i < N_PIX_BUFFERS; i++) {
		struct render_slot *slot = &global_render_state.slots[(slot_ix + i) % N_PIX_BUFFERS];
		if (!slot->complete && slot->cpu_assigned == 1) {
			n_in_flight++;
		}
	}

	// This choice tries to keep core 1 busy, but may require excessive
	// buffering if close to maximum capacity. Maybe tune.
	uint cpu = n_in_flight >= 2 ? 0 : 1;
	slot->complete = 0;
	slot->cpu_assigned = cpu;
	slot->y = global_render_state.y;
	if (cpu == 0) {
		if (multicore_fifo_wready()) {
			// could just write to hw register directly
			multicore_fifo_push_blocking(slot_ix);
		} else {
			slot->cpu_assigned = ~0;
		}
	}
	if (cpu == 1) {
		if (!queue_try_add_u32(&global_render_state.q_core1_req, &slot_ix)) {
			slot->cpu_assigned = ~0;
		}
	}
	global_render_state.slot_ix = (slot_ix + 1) % N_PIX_BUFFERS;
	global_render_state.y++;
	if (global_render_state.y == FRAME_HEIGHT) {
		global_render_state.y = 0;
		gpio_xor_mask(1u << LED_PIN);
	}
}

void __not_in_flash("main") core1_main() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	sem_acquire_blocking(&dvi_start_sem);
	dvi_start(&dvi0);

	while (1) {
		uint slot_ix;
		queue_remove_blocking_u32(&global_render_state.q_core1_req, &slot_ix);
		struct render_slot *slot = &global_render_state.slots[slot_ix];
		slot->dlist = render_scanline(slot, slot_ix);
		slot->complete = 1;
	}

	__builtin_unreachable();
}

uint8_t bwbuf[FRAME_WIDTH / 8];
uint32_t graybuf[FRAME_WIDTH / 2];

uint delay(uint n) {
	uint x = 0;
	for (uint i = 0; i < n; i++) {
		x = (1 << x);
	}
	return x >> 31;
}

char *text[] = {
	"This is a demo of text rendering on a Pico board. The current prototype is written in C, but",
	"I am hoping to redo it in Rust. All pixels are generated by racing the beam; it is not using",
	"a frame buffer. Rendering is structured as a pipeline with a number of stages. The application",
	"generates a display list with references to each character, about 16 bytes per character. The",
	"middle stage renders this display list to a bitmap buffer. The last stage encodes this bitmap",
	"buffer to a TMDS signal using a palette.",
	"",
	"Right now it's a fairly minimal setup to generate text, and it could be replicated just by",
	"having a 1bpp frame buffer (which would be 38k for VGA), but the architecture is intended to",
	"scale quite a bit. A scanline can contain multiple runs, and each run can have its own palette,",
	"or even be a different graphics type. This is currently used to optimize solid colors. I'm",
	"pretty sure that 4bpp gray or palette is possible (which should enable antialiased text), and",
	"12bpp RGB is probably viable if it's only a fraction of the scanline.",
	"",
	"There are a number of interesting things about the architecture. The rendering stages are",
	"virtual machines, and implemented in direct threaded style. Currently the middle stage is in",
	"C, but ultimately it will be assembler.",
	"",
	"The architecture based on a display list can obviously support smooth scrolling, which is a",
	"hallmark of beam-racing designs as opposed to frame buffers. I also think it could support",
	"overlapping windows.",
	"",
	"We'll see where this goes, but for the time being, I'm just having lots of fun playing with it.",
	NULL,
};

int __not_in_flash("main") main() {
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
#ifdef RUN_FROM_CRYSTAL
	set_sys_clock_khz(12000, true);
#else
	// Run system at TMDS bit clock
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
#endif

	setup_default_uart();

	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi0.scanline_callback = core1_scanline_callback;
	uint color_spinlock = next_striped_spin_lock_num();
	dvi_init(&dvi0, next_striped_spin_lock_num(), color_spinlock);
	global_render_state.slot_ix = 0;
	global_render_state.y = 0;
	for (int i = 0; i < N_PIX_BUFFERS; i++) {
		global_render_state.slots[i].cpu_assigned = 0;
		global_render_state.slots[i].complete = 0;
	}
	queue_init_with_spinlock(&global_render_state.q_core1_req, sizeof(void*), 8, color_spinlock);

#if 0
	for (int i = 0; i < CHAR_ROWS * CHAR_COLS; ++i)
		charbuf[i] = FONT_FIRST_ASCII + i % FONT_N_CHARS;
	prepare_pixline();
	init_text_runs("Hello world. I am worried about running out of bandwidth. How close am I to going over the edge?");
	init_expand_lut();
	generate_scanline(0);
	tmds_encode_scanline();
#endif

	sem_init(&dvi_start_sem, 0, 1);
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);

	for (uint i = 0;; i++) {
		if (text[i] == NULL) {
			n_textlines = i;
			break;
		}
		setup_text_line(&textlines[i], text[i], FRAME_WIDTH);
		uint32_t fg = palette[i % 12];
		make_1bpp_pal(textlines[i].palette, 0, fg);
	}
	make_1bpp_pal(global_pal, 0, 0x4ba);
	make_1bpp_pal(global_pal2, 0x321, 0x4ba);

	for (uint i = 0; i < N_PIX_BUFFERS; i++) {
		struct render_slot *slot = &global_render_state.slots[1];
		slot->cpu_assigned = 0;
		slot->y = i;
		slot->dlist = render_scanline(slot, i);
		slot->complete = 1;
	}
	global_render_state.y = N_PIX_BUFFERS;
	core1_scanline_callback();
	multicore_launch_core1(core1_main);

	sem_release(&dvi_start_sem);
	while (1) {
		// TODO: set up interrupt so we can have a main task. This is
		// why we use multicore rather than queue.
		uint slot_ix = multicore_fifo_pop_blocking();
		struct render_slot *slot = &global_render_state.slots[slot_ix];
		slot->dlist = render_scanline(slot, slot_ix);
		slot->complete = 1;
	}
	__builtin_unreachable();
}
