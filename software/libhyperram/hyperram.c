#include "hardware/pio.h"

#include "hyperram.h"
#include "hyperram.pio.h"

#define CTRL_PIN_CS 0
#define CTRL_PIN_RWDS 1
#define CTRL_PIN_CK 2

void hyperram_pio_init(const hyperram_inst_t *inst) {
	// Make sure HyperRAM is in reset before we futz with the GPIOs
	gpio_init(inst->rst_n_pin);
	gpio_put(inst->rst_n_pin, 0);
	gpio_set_dir(inst->rst_n_pin, GPIO_OUT);

	for (uint i = inst->dq_base_pin; i < inst->dq_base_pin + 8; ++i) {
		// Setting both pull bits enables bus-keep function
		gpio_set_pulls(i, true, true);
		pio_gpio_init(inst->pio, i);
	}
	hw_clear_bits(&inst->pio->input_sync_bypass, 0xffu << inst->dq_base_pin);

	for (uint i = inst->ctrl_base_pin; i < inst->ctrl_base_pin + 3; ++i)
		pio_gpio_init(inst->pio, i);
	gpio_pull_down(inst->ctrl_base_pin + CTRL_PIN_RWDS);

	// All controls low except CSn
	pio_sm_set_pins_with_mask(inst->pio, inst->sm,
		(1u << CTRL_PIN_CS) << inst->ctrl_base_pin,
		0x7u << inst->ctrl_base_pin
	);
	// All controls output except RWDS (DQs will sort themselves out later)
	pio_sm_set_pindirs_with_mask(inst->pio, inst->sm,
		(1u << CTRL_PIN_CS | 1u << CTRL_PIN_CK) << inst->ctrl_base_pin,
		0x7u << inst->ctrl_base_pin
	);

	pio_sm_config c = hyperram_program_get_default_config(inst->prog_offset);
	sm_config_set_out_pins(&c, inst->dq_base_pin, 8);
	sm_config_set_in_pins(&c, inst->dq_base_pin);
	sm_config_set_set_pins(&c, inst->ctrl_base_pin + CTRL_PIN_RWDS, 1);
	sm_config_set_sideset_pins(&c, inst->ctrl_base_pin);
	// Use shift-to-left (this means we write to memory in the wrong endianness,
	// but we hide this by requiring word-aligned addresses)
	sm_config_set_in_shift(&c, false, true, 32);
	sm_config_set_out_shift(&c, false, true, 32);
	// Should be removed: slow everything down to an easy speed to probe
	sm_config_set_clkdiv(&c, 10.f);
	pio_sm_init(inst->pio, inst->sm, inst->prog_offset, &c);
	pio_sm_set_enabled(inst->pio, inst->sm, true);

	gpio_put(inst->rst_n_pin, 1);
}

// 12-byte command packet to push to the PIO SM. After pushing the packet, can
// either push write data, or wait for read data.
typedef struct {
	uint32_t len;
	uint32_t cmd0;
	uint32_t cmd1_dir_jmp;
} hyperram_cmd_t;

typedef enum {
	HRAM_CMD_READ = 0x5,
	HRAM_CMD_WRITE = 0x1,
	HRAM_CMD_REGWRITE = 0x6
} hyperram_cmd_flags;

// HyperRAM command format from S27KL0641 datasheet:
// ------+------------------------------+------------------------------------+
// Bits  | Name                         | Description                        |
// ------+------------------------------+------------------------------------+
// 47    | R/W#                         | 1 for read, 0 for write            |
//       |                              |                                    |
// 46    | AS                           | 0 for memory address space, 1 for  |
//       |                              | register space (write only)        |
//       |                              |                                    |
// 45    | Burst                        | 0 for wrapped, 1 for linear        |
//       |                              |                                    |
// 44:16 | Row and upper column address | Address bits 31:3, irrelevant bits |
//       |                              | should be 0s                       |
//       |                              |                                    |
// 15:3  | Reserved                     | Set to 0                           |
//       |                              |                                    |
// 2:0   | Lower column address         | Address bits 2:0                   |
// ------+------------------------------+------------------------------------+

static inline void _hyperram_cmd_init(hyperram_cmd_t *cmd, hyperram_cmd_flags flags, uint32_t addr, uint len) {
	// HyperBus uses halfword addresses, not byte addresses. Additionally we
	// require addresses to be word-aligned, to hide the fact we screw up
	// endianness.
	addr = (addr >> 1) & ~0x1u;
	uint32_t addr_l = addr & 0x7u;
	uint32_t addr_h = addr >> 3;
	// First byte is always 0xff (to set DQs to output), then 24-bit length in same FIFO word.
	// Length is number of halfwords, minus one.
	cmd->len = (0xffu << 24) | ((len * 2 - 1) & ((1u << 24) - 1));
	cmd->cmd0 = (flags << 29) | addr_h;
	cmd->cmd1_dir_jmp = addr_l << 16;
}

void __not_in_flash_func(hyperram_read_blocking)(const hyperram_inst_t *inst, uint32_t addr, uint32_t *dst, uint len) {
	hyperram_cmd_t cmd;
	_hyperram_cmd_init(&cmd, HRAM_CMD_READ, addr, len);
	cmd.cmd1_dir_jmp |= inst->prog_offset + hyperram_offset_read;
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.len);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd0);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd1_dir_jmp);
	for (uint i = 0; i < len; ++i)
		dst[i] = pio_sm_get_blocking(inst->pio, inst->sm);
}

void __not_in_flash_func(hyperram_write_blocking)(const hyperram_inst_t *inst, uint32_t addr, const uint32_t *src, uint len) {
	hyperram_cmd_t cmd;
	_hyperram_cmd_init(&cmd, HRAM_CMD_WRITE, addr, len);
	cmd.cmd1_dir_jmp |= (0xffu << 8) | (inst->prog_offset + hyperram_offset_write);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.len);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd0);
	pio_sm_put_blocking(inst->pio, inst->sm, cmd.cmd1_dir_jmp);
	for (uint i = 0; i < len; ++i)
		pio_sm_put_blocking(inst->pio, inst->sm, src[i]);
}

// Note these are *byte* addresses, so are off by a factor of 2 from those given in datasheet
enum {
	HRAM_REG_ID0  = 0u << 12 | 0u << 1,
	HRAM_REG_ID1  = 0u << 12 | 1u << 1,
	HRAM_REG_CFG0 = 1u << 12 | 0u << 1,
	HRAM_REG_CFG1 = 1u << 12 | 1u << 1
};

// We are using an awful hack here to reuse the data write loop for sending a
// command packet followed immediately by a halfword of data. No worries about
// efficiency, because generally you only write to the config register once.
void hyperram_cfgreg_write(const hyperram_inst_t *inst, uint16_t wdata) {
	hyperram_cmd_t cmd;
	_hyperram_cmd_init(&cmd, HRAM_CMD_REGWRITE, HRAM_REG_CFG0, 0);
	// Make sure SM has bottomed out on TX empty, because we're about to mess
	// with its control flow
	uint32_t txstall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + inst->sm);
	inst->pio->fdebug = txstall_mask;
	while (!(inst->pio->fdebug & txstall_mask))
		;
	// Set DQs to output (note that this uses exec(), so asserts CSn as a result,
	// as it doesn't set delay/sideset bits)
	pio_sm_set_consecutive_pindirs(inst->pio, inst->sm, inst->dq_base_pin, 8, true);
	// Preload Y register for the data loop (number of halfwords - 1)
	pio_sm_exec(inst->pio, inst->sm, pio_encode_set(pio_y, 3));
	// Note the difference between offset_write and offset_write_loop is whether
	// RWDS is asserted first (only true for write)
	pio_sm_exec(inst->pio, inst->sm, pio_encode_jmp(
		inst->prog_offset + hyperram_offset_write_loop
	));
	pio_sm_put(inst->pio, inst->sm, cmd.cmd0);
	pio_sm_put(inst->pio, inst->sm, cmd.cmd1_dir_jmp | wdata);
}
