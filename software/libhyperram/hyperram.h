#ifndef _HYPERRAM_H
#define _HYPERRAM_H

#include "hardware/pio.h"

typedef struct {
	PIO pio;
	uint sm;
	uint prog_offset;
	// DQ0->DQ7 starting here:
	uint dq_base_pin;
	// CSn, RWDS, CK, starting here:
	uint ctrl_base_pin;
	uint rst_n_pin;
} hyperram_inst_t;

void hyperram_pio_init(const hyperram_inst_t *inst);

void hyperram_read_blocking(const hyperram_inst_t *inst, uint32_t addr, uint32_t *dst, uint len);
void hyperram_write_blocking(const hyperram_inst_t *inst, uint32_t addr, const uint32_t *src, uint len);
void hyperram_cfgreg_write(const hyperram_inst_t *inst, uint16_t wdata);

#endif
