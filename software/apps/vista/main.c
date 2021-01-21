#include <stdio.h>
#include <stdlib.h>
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pll.h"
#include "hardware/sync.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/ssi.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "pico/stdlib.h"

#include "tmds_encode.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"

// TMDS bit clock 252 MHz
// DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define BIT_CLOCK_MHZ 252
#define VREG_VSEL VREG_VOLTAGE_1_15
#define DVI_TIMING dvi_timing_640x480p_60hz

#define LED_PIN 21

#define IMAGE_SIZE (640 * 480 * 2)
#define IMAGE_BASE 0x1003c000
#define IMAGE_SCANLINE_SIZE (640 * 2)

#define N_IMAGES 3
#define FRAMES_PER_IMAGE 300

struct dvi_inst dvi0;

void __scratch_x("dvi") dvi0_irq() {
	dma_hw->ints0 = 1u << dvi0.dma_cfg[TMDS_SYNC_LANE].chan_ctrl;

	static int count = 0;
	if (++count == 4) {
		count = 0;
		dvi_dma_irq_handler(&dvi0);
	}
}

struct semaphore dvi_start_sem;

// Just dumped out register values from running the code in tmds_encode.c:
// channel_msb: 4, lshift_lower: 3, i0 ctrl: 02801c60, 00002117, i1 ctrl: 02801c6d, 00002117
// channel_msb: 10, lshift_lower: 0, i0 ctrl: 02801c43, 00002117, i1 ctrl: 02801c53, 00002117
// channel_msb: 15, lshift_lower: 0, i0 ctrl: 02801c68, 00002117, i1 ctrl: 00001c78, 00002117
//
// This saves a few cycles vs doing the setup dynamically (just enough to tip
// us over the edge as it happens, when proper DC balancing is enabled.)

extern uint32_t tmds_table_fullres_x;
extern uint32_t tmds_table_fullres_y;

static inline void init_interp_for_encode() {
	interp0_hw->ctrl[1] = 0x00002117u;
	interp1_hw->ctrl[1] = 0x00002117u;
	uint32_t lutbase = (uint32_t)(get_core_num() ? &tmds_table_fullres_x : &tmds_table_fullres_y);
	interp0_hw->base[2] = (uint32_t)lutbase;
	interp1_hw->base[2] = (uint32_t)lutbase;
}

static inline void prepare_scanline_core1(const uint32_t *colourbuf, uint32_t *tmdsbuf) {
	const uint pixwidth = 640;
	// Blue
	interp0_hw->ctrl[0] = 0x02801c60u;
	interp1_hw->ctrl[0] = 0x02801c6du;
	tmds_fullres_encode_loop_16bpp_leftshift_x(colourbuf, tmdsbuf, pixwidth, 3);
	// Green
	interp0_hw->ctrl[0] = 0x02801c43u;
	interp1_hw->ctrl[0] = 0x02801c53u;
	tmds_fullres_encode_loop_16bpp_x(colourbuf, tmdsbuf + pixwidth, pixwidth);
	// Red
	interp0_hw->ctrl[0] = 0x02801c68u;
	interp1_hw->ctrl[0] = 0x00001c78u;
	tmds_fullres_encode_loop_16bpp_x(colourbuf, tmdsbuf + 2 * pixwidth, pixwidth);
}

static inline void prepare_scanline_core0(const uint32_t *colourbuf, uint32_t *tmdsbuf) {
	const uint pixwidth = 640;
	// Blue
	interp0_hw->ctrl[0] = 0x02801c60u;
	interp1_hw->ctrl[0] = 0x02801c6du;
	tmds_fullres_encode_loop_16bpp_leftshift_y(colourbuf, tmdsbuf, pixwidth, 3);
	// Green
	interp0_hw->ctrl[0] = 0x02801c43u;
	interp1_hw->ctrl[0] = 0x02801c53u;
	tmds_fullres_encode_loop_16bpp_y(colourbuf, tmdsbuf + pixwidth, pixwidth);
	// Red
	interp0_hw->ctrl[0] = 0x02801c68u;
	interp1_hw->ctrl[0] = 0x00001c78u;
	tmds_fullres_encode_loop_16bpp_y(colourbuf, tmdsbuf + 2 * pixwidth, pixwidth);
}

void __no_inline_not_in_flash_func(flash_bulk_dma_start)(uint32_t *rxbuf, uint32_t flash_offs, size_t len, uint dma_chan)
{
	ssi_hw->ssienr = 0;
	ssi_hw->ctrlr1 = len - 1; // NDF, number of data frames
	ssi_hw->dmacr = SSI_DMACR_TDMAE_BITS | SSI_DMACR_RDMAE_BITS;
	ssi_hw->ssienr = 1;
	// Other than NDF, the SSI configuration used for XIP is suitable for a bulk read too.

	dma_hw->ch[dma_chan].read_addr = (uint32_t)&ssi_hw->dr0;
	dma_hw->ch[dma_chan].write_addr = (uint32_t)rxbuf;
	dma_hw->ch[dma_chan].transfer_count = len;
	dma_hw->ch[dma_chan].ctrl_trig =
		DMA_CH0_CTRL_TRIG_BSWAP_BITS |
		DREQ_XIP_SSIRX << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB |
		dma_chan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB |
		DMA_CH0_CTRL_TRIG_INCR_WRITE_BITS |
		DMA_CH0_CTRL_TRIG_DATA_SIZE_VALUE_SIZE_WORD << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB |
		DMA_CH0_CTRL_TRIG_EN_BITS;

	// Now DMA is waiting, kick off the SSI transfer (mode continuation bits in LSBs)
	ssi_hw->dr0 = (flash_offs << 8) | 0xa0;
}

// Core 1 handles DMA IRQs and runs TMDS encode on scanline buffers it
// receives through the mailbox FIFO
void __not_in_flash("main") core1_main() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	sem_acquire_blocking(&dvi_start_sem);
	dvi_start(&dvi0);

	init_interp_for_encode();

	while (1) {
		const uint32_t *colourbuf = (const uint32_t*)multicore_fifo_pop_blocking();
		uint32_t *tmdsbuf = (uint32_t*)multicore_fifo_pop_blocking();
		prepare_scanline_core1(colourbuf, tmdsbuf);
		multicore_fifo_push_blocking(0);
	}
	__builtin_unreachable();
}

uint8_t img_buf[2][2 * IMAGE_SCANLINE_SIZE];

int __not_in_flash("main") main() {
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
	// Run system at TMDS bit clock
	set_sys_clock_khz(BIT_CLOCK_MHZ * 1000, true);

	setup_default_uart();

	for (int i = DEBUG_PIN0; i < DEBUG_PIN0 + DEBUG_N_PINS; ++i) {
		gpio_init(i);
		gpio_set_dir(i, GPIO_OUT);
	}

	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	// gpio_put(LED_PIN, 1);

	printf("Configuring DVI\n");

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DEFAULT_DVI_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

	printf("DMA first image line\n");
	const uint img_dma_chan = dma_claim_unused_channel(true);
	flash_bulk_dma_start((uint32_t*)img_buf[0], IMAGE_BASE, IMAGE_SCANLINE_SIZE * 2 / sizeof(uint32_t), img_dma_chan);
	dma_channel_wait_for_finish_blocking(img_dma_chan);
	int img_buf_front = 0;
	int img_buf_back = 1;

	printf("Core 1 start\n");
	sem_init(&dvi_start_sem, 0, 1);
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
	multicore_launch_core1(core1_main);

	uint heartbeat = 0;
	uint slideshow_ctr = 0;
	uint32_t current_image_base = IMAGE_BASE;

	init_interp_for_encode();

	sem_release(&dvi_start_sem);
	while (1) {
		if (++heartbeat >= 30) {
			heartbeat = 0;
			gpio_xor_mask(1u << LED_PIN);
		}
		if (++slideshow_ctr >= FRAMES_PER_IMAGE) {
			slideshow_ctr = 0;
			current_image_base = IMAGE_BASE + (current_image_base - IMAGE_BASE + IMAGE_SIZE) % (N_IMAGES * IMAGE_SIZE);
		}
		for (int y = 0; y < 2 * FRAME_HEIGHT; y += 2) {
			// Start DMA to back buffer before starting to encode the front buffer (each buffer is two scanlines)
			flash_bulk_dma_start(
				(uint32_t*)img_buf[img_buf_back],
				current_image_base + ((y + 2) % (2 * FRAME_HEIGHT)) * IMAGE_SCANLINE_SIZE,
				IMAGE_SCANLINE_SIZE * 2 / sizeof(uint32_t),
				img_dma_chan
			);
			const uint16_t *img = (const uint16_t*)img_buf[img_buf_front];			
			uint32_t *our_tmds_buf, *their_tmds_buf;
			queue_remove_blocking_u32(&dvi0.q_tmds_free, &their_tmds_buf);
			multicore_fifo_push_blocking((uint32_t)(img));
			multicore_fifo_push_blocking((uint32_t)their_tmds_buf);
	
			queue_remove_blocking_u32(&dvi0.q_tmds_free, &our_tmds_buf);
			prepare_scanline_core0((const uint32_t*)(img + FRAME_WIDTH * 2), our_tmds_buf);
			
			multicore_fifo_pop_blocking();
			queue_add_blocking_u32(&dvi0.q_tmds_valid, &their_tmds_buf);
			queue_add_blocking_u32(&dvi0.q_tmds_valid, &our_tmds_buf);
			// Swap the buffers after each scanline pair completion
			img_buf_front = !img_buf_front;
			img_buf_back = !img_buf_back;
		}
	}
	__builtin_unreachable();
}
	
