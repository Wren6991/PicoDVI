#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "tmds_encode.h"


// Pick one:
#define MODE_640x480_60Hz
// #define MODE_1280x720_30Hz

#if defined(MODE_640x480_60Hz)
// DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define BIT_CLOCK_MHZ 252
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz
#include "moon_1bpp_640x480.h"
#define moon_img moon_1bpp_640x480

#elif defined(MODE_1280x720_30Hz)
#define FRAME_WIDTH 1280
#define FRAME_HEIGHT 720
#define BIT_CLOCK_MHZ 372
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_1280x720p_30hz
#include "moon_1bpp_1280x720.h"
#define moon_img moon_1bpp_1280x720

#else
#error "Select a video mode!"
#endif

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
	dvi_start(&dvi0);

	while (true) {
		for (uint y = 0; y < FRAME_HEIGHT; ++y) {
			const uint32_t *colourbuf = &((const uint32_t*)moon_img)[y * FRAME_WIDTH / 32];
			uint32_t *tmdsbuf;
			queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
			tmds_encode_1bpp(colourbuf, tmdsbuf, FRAME_WIDTH);
			queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
		}
	}
}
