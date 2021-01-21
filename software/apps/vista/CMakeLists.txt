# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

add_executable(vista
	main.c
)

# Define a new second stage with flash CLKDIV=2 => SCK frequency of 126 MHz.
# We need a sustained bandwidth of just over 40 MB/s during vertical active period
pico_define_boot_stage2(vista_boot2 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(vista_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=2)
pico_set_boot_stage2(vista vista_boot2)


target_compile_definitions(vista PRIVATE DVI_VERTICAL_REPEAT=1 DVI_N_TMDS_BUFFERS=5)
target_compile_definitions(vista PRIVATE PICO_STACK_SIZE=0x200)

target_link_libraries(vista
	pico_stdlib
	pico_multicore
	libdvi
)

# create map/bin/hex file etc.
pico_add_extra_outputs(vista)
