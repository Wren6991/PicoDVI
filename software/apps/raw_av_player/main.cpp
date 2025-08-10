#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"
#include "audio.hpp"

extern "C" {
#include "dvi.h"
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"
#include "sprite.h"
}

#include "ff.h"

// DVDD 1.2V (1.1V seems ok too)
#define FRAME_WIDTH 400
#define FRAME_HEIGHT 240
#define VREG_VSEL VREG_VOLTAGE_1_25
#define DVI_TIMING dvi_timing_800x480p_60hz

#define AUDIO_SAMPLE_RATE 22050


#define SW_A 14
#define SW_B 15
#define SW_C 16


FATFS fs;
FIL fil;
FRESULT fr;
FIL afil;

struct dvi_inst dvi0;

uint16_t framebuf[FRAME_WIDTH * FRAME_HEIGHT] = {0};

// Note first two scanlines are pushed before DVI start
volatile uint scanline = 2;

int mount_sd() {
	fr = f_mount(&fs, "", 1);
	if (fr != FR_OK) {
		printf("Failed to mount SD card, error: %d\n", fr);
		return 1;
	}

	FILINFO file;
	auto dir = new DIR();
	printf("Listing /\n");
	f_opendir(dir, "/");
	while(f_readdir(dir, &file) == FR_OK && file.fname[0]) {
		printf("%s %lld\n", file.fname, file.fsize);
	}
	f_closedir(dir);

	return 0;
}

void core1_main() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
	dvi_start(&dvi0);
	dvi_scanbuf_main_16bpp(&dvi0);
	__builtin_unreachable();
}

void core1_scanline_callback() {
	// Discard any scanline pointers passed back
	uint16_t *bufptr;
	while (queue_try_remove_u32(&dvi0.q_colour_free, &bufptr))
		;
	bufptr = &framebuf[FRAME_WIDTH * scanline];
	queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
	scanline = (scanline + 1) % FRAME_HEIGHT;
}

void vsync() {
	while(scanline != 0) {};
}

void get_audio_frame(int16_t *buffer, uint32_t sample_count) {
	size_t bytes_read = 0;
	f_read(&afil, (uint8_t *)buffer, sample_count * 2, &bytes_read);
	if(bytes_read == 0) {
		f_lseek(&afil, 0);
	}
}

int main() {
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

	setup_default_uart();

	gpio_init(SW_A); gpio_set_dir(SW_A, GPIO_IN); gpio_pull_down(SW_A);
	gpio_init(SW_B); gpio_set_dir(SW_B, GPIO_IN); gpio_pull_down(SW_B);
	gpio_init(SW_C); gpio_set_dir(SW_C, GPIO_IN); gpio_pull_down(SW_C);

	mount_sd();

	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi0.scanline_callback = core1_scanline_callback;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());


	sprite_fill16(framebuf, 0xffff, FRAME_WIDTH * FRAME_HEIGHT);
	uint16_t *bufptr = framebuf;
	queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);
	bufptr += FRAME_WIDTH;
	queue_add_blocking_u32(&dvi0.q_colour_valid, &bufptr);

	multicore_launch_core1(core1_main);

	size_t bytes_read = 0;

	printf("Starting playback...\n");
	uint dma_channel = 9;
	struct audio_buffer_pool *ap = init_audio(AUDIO_SAMPLE_RATE, PICO_AUDIO_I2S_DATA, PICO_AUDIO_I2S_BCLK, 1, dma_channel);

	printf("Opening Video/Audio files...\n");

	// Convert video to raw RGB656 litte-endian using ffmpeg, eg:
	// ffmpeg -i BigBuckBunny_640x360.m4v -vf scale=400:240,fps=12 -c:v rawvideo -pix_fmt rgb565le BigBuckBunny2.rgb
	fr = f_open(&fil, "BigBuckBunny2.rgb", FA_READ);

	// Convert audio to raw, signed, 16-bit, little-endian mono using ffmpeg, eg:
	// ffmpeg -i BigBuckBunny_640x360.m4v -f s16le -acodec pcm_s16le -ar 22050 -ac 1 BigBuckBunny2.pcm
	fr = f_open(&afil, "BigBuckBunny2.pcm", FA_READ);

	printf("Playing...\n");

	while(true) {
		vsync();
    	update_buffer(ap, get_audio_frame);
		f_read(&fil, (uint8_t *)&framebuf, FRAME_WIDTH * FRAME_HEIGHT * 2, &bytes_read);
		if(bytes_read == 0) {
			f_lseek(&fil, 0);
		}
	}
}
