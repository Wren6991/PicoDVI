#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "rle_decompress.h"

#include "test_frame.h"

#define MOVIE_BASE (XIP_BASE + 0x10000)
#define MOVIE_FRAMES 1799

// DVDD 1.25V (slower silicon may need the full 1.3, or just not work)
#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720
#define BIT_CLOCK_MHZ 372
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_1280x720p_30hz

struct dvi_inst dvi0;

int main() {
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
	// Run system at TMDS bit clock
	set_sys_clock_khz(BIT_CLOCK_MHZ * 1000, true);

	setup_default_uart();

	for (int i = DEBUG_PIN0; i < DEBUG_PIN0 + DEBUG_N_PINS; ++i) {
		gpio_init(i);
		gpio_set_dir(i, GPIO_OUT);
	}

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DEFAULT_DVI_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);

	// Fill all of DVI's TMDS buffers with valid symbols (fine vertical stripes),
	// as we'll only be rendering into the middle part of each span
	uint32_t *buf;
	while (queue_try_remove_u32(&dvi0.q_tmds_free, &buf)) {
		for (int i = 0; i < FRAME_WIDTH; ++i)
			buf[i] = i & 1 ? 0x99555 : 0x96aaa;
		queue_add_blocking_u32(&dvi0.q_tmds_valid, &buf);
	}
	// Shuffle them back to the free queue so we don't have to deal with the timing later
	while (queue_try_remove_u32(&dvi0.q_tmds_valid, &buf))
		queue_add_blocking_u32(&dvi0.q_tmds_free, &buf);

	dvi_start(&dvi0);

	const uint8_t *line = (const uint8_t *)MOVIE_BASE;
	int frame = 0;
	while (true) {
		uint32_t *render_target;
		for (int y = 0; y < FRAME_HEIGHT; ++y) {
			uint8_t line_len = *line++;
			queue_remove_blocking_u32(&dvi0.q_tmds_free, &render_target);
			rle_to_tmds(line, render_target + (1280 - 960) / 2, line_len);
			queue_add_blocking_u32(&dvi0.q_tmds_valid, &render_target);
			line += line_len;
		}
		if (++frame == MOVIE_FRAMES) {
			frame = 0;
			line = (const uint8_t *)MOVIE_BASE;
		}
	}
}
	
