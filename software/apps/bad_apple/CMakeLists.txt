# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

add_executable(bad_apple
	main.c
	rle_decompress.h
	rle_decompress.S
)

# Increase CLKDIV to 4, because we're going to run the system at 372 MHz, and
# the flash gets rather unhappy if you try to clock it at 186 MHz
pico_define_boot_stage2(bad_apple_boot2 ${PICO_SDK_PATH}/src/rp2_common/boot_stage2/boot2_w25q080.S)
target_compile_definitions(bad_apple_boot2 PRIVATE PICO_FLASH_SPI_CLKDIV=4)
pico_set_boot_stage2(bad_apple bad_apple_boot2)

target_include_directories(bad_apple PRIVATE
	${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(bad_apple PRIVATE
	DVI_VERTICAL_REPEAT=1
	DVI_N_TMDS_BUFFERS=3
	DVI_MONOCHROME_TMDS
)

target_link_libraries(bad_apple
	pico_stdlib
	hardware_dma
	pico_multicore
	pico_util
	libdvi
	libsprite
)

# create map/bin/hex file etc.
pico_add_extra_outputs(bad_apple)
