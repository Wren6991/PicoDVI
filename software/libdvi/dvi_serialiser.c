#include "pico.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/structs/padsbank0.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "dvi_serialiser.pio.h"

static void dvi_init_gpio(uint gpio, bool invert) {
	// 2 mA drive, enable slew rate limiting (this seems fine even at 720p30, and
	// the 3V3 LDO doesn't get warm like when turning all the GPIOs up to 11).
	// Also disable digital reciever.
	hw_write_masked(
		&padsbank0_hw->io[gpio],
		(0 << PADS_BANK0_GPIO0_DRIVE_LSB),
		PADS_BANK0_GPIO0_DRIVE_BITS | PADS_BANK0_GPIO0_SLEWFAST_BITS | PADS_BANK0_GPIO0_IE_BITS
	);
	gpio_set_outover(gpio, invert ? GPIO_OVERRIDE_INVERT : GPIO_OVERRIDE_NORMAL);
}

void dvi_serialiser_init(struct dvi_serialiser_cfg *cfg) {
#if DVI_SERIAL_DEBUG
	uint offset = pio_add_program(cfg->pio, &dvi_serialiser_debug_program);
	uint offset_clk = offset;
#else
	uint offset = pio_add_program(cfg->pio, &dvi_serialiser_program);
	uint offset_clk = pio_add_program(cfg->pio, &dvi_serialiser_clk_program);
#endif
	cfg->prog_offs = offset;
	cfg->prog_offs_clk = offset_clk;

	for (int i = 0; i < N_TMDS_LANES; ++i) {
		pio_sm_claim(cfg->pio, cfg->sm_tmds[i]);
		dvi_serialiser_program_init(
			cfg->pio,
			cfg->sm_tmds[i],
			i == TMDS_SYNC_LANE ? offset_clk : offset,
			cfg->pins_tmds[i],
			cfg->pins_clk,
			i == TMDS_SYNC_LANE,
			DVI_SERIAL_DEBUG
		);
		dvi_init_gpio(cfg->pins_tmds[i], cfg->invert_diffpairs);
		dvi_init_gpio(cfg->pins_tmds[i] + 1, cfg->invert_diffpairs);
	}
	dvi_init_gpio(cfg->pins_clk, cfg->invert_diffpairs);
	dvi_init_gpio(cfg->pins_clk + 1, cfg->invert_diffpairs);
}

void dvi_serialiser_enable(struct dvi_serialiser_cfg *cfg, bool enable) {
	uint mask = 0;
	for (int i = 0; i < N_TMDS_LANES; ++i)
		mask |= 1u << (cfg->sm_tmds[i] + PIO_CTRL_SM_ENABLE_LSB);
	if (enable)
		hw_set_bits(&cfg->pio->ctrl, mask);
	else
		hw_clear_bits(&cfg->pio->ctrl, mask);
}

uint32_t dvi_single_to_diff(uint32_t in) {
	uint32_t accum = 0;
	const uint TMDS_SIZE = 10;
	for (int i = 0; i < TMDS_SIZE; ++i) {
		accum <<= 2;
		if (in & 1 << (TMDS_SIZE - 1))
			accum |= 0x1;
		else
			accum |= 0x2;
		in <<= 1;
	}
	return accum;
}
