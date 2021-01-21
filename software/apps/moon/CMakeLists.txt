# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

add_executable(moon
	main.c
)

target_compile_options(moon PRIVATE -Wall)

target_compile_definitions(moon PRIVATE DVI_VERTICAL_REPEAT=1 DVI_N_TMDS_BUFFERS=3 DVI_MONOCHROME_TMDS)


target_link_libraries(moon
	pico_stdlib
	pico_multicore
	libdvi
)

# create map/bin/hex file etc.
pico_add_extra_outputs(moon)
