#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "hyperram.h"
#include "hyperram.pio.h"

void printdata(const uint8_t *data, uint len) {
	for (int i = 0; i < len; ++i)
		printf("%02x%c", data[i], i % 16 == 15 ? '\n' : ' ');
	if (len % 16 != 15)
		printf("\n"); 
}

#define TESTLEN 256

int main() {
	vreg_set_voltage(VREG_VOLTAGE_1_20);
	sleep_ms(1);
	set_sys_clock_khz(252 * 1000, true);
	setup_default_uart();
	hyperram_inst_t hram = {
		.pio = pio0,
		.sm = 3,
		.dq_base_pin = 22,
		.ctrl_base_pin = 0,
		.rst_n_pin = 4
	};
	printf("Loading program\n");
	hram.prog_offset = pio_add_program(hram.pio, &hyperram_program);
	printf("Initialising state machine\n");
	hyperram_pio_init(&hram);
	pio_sm_set_clkdiv(hram.pio, hram.sm, 50.f);

	const uint16_t cfgreg_init =
		(0x1u << 15) | // Power on (no DPDn)
		(0x0u << 12) | // Default drive strength (34R)
		(0xeu << 4)  | // 3 latency cycles (in bias -5 format)
		(0x1u << 3);   // Fixed 2x latency mode
	                   // Don't care about wrap modes, we're always linear

	printf("Writing %04x to CR0 register\n", cfgreg_init);
	hyperram_cfgreg_write(&hram, cfgreg_init);

	for (int clkdiv = 10; clkdiv > 0; --clkdiv) {
		printf("\nSetting clock divisor to %d\n", clkdiv);
		pio_sm_set_clkdiv(hram.pio, hram.sm, clkdiv);
		printf("Writing:\n");
		uint8_t testdata[TESTLEN];
		// Clear, then write test pattern
		for (int i = 0; i < TESTLEN; ++i)
			testdata[i] = 0;
		hyperram_write_blocking(&hram, 0x1234, (const uint32_t*)testdata, TESTLEN / sizeof(uint32_t));
		for (int i = 0; i < TESTLEN; ++i)
			testdata[i] = i & 0xff;
		// printdata(testdata, TESTLEN);
		hyperram_write_blocking(&hram, 0x1234, (const uint32_t*)testdata, TESTLEN / sizeof(uint32_t));

		printf("Reading back:\n");
		hyperram_read_blocking(&hram, 0x1234, (uint32_t*)testdata, TESTLEN / sizeof(uint32_t));
		printdata(testdata, TESTLEN);
	}
}

