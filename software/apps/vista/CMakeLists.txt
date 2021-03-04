# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

add_executable(vista
	main.c
)

target_compile_definitions(vista PRIVATE
	DVI_VERTICAL_REPEAT=1
	DVI_N_TMDS_BUFFERS=5
	DVI_SYMBOLS_PER_WORD=1
	)

target_compile_definitions(vista PRIVATE PICO_STACK_SIZE=0x200)


target_link_libraries(vista
	pico_stdlib
	pico_multicore
	libdvi
)

# create map/bin/hex file etc.
pico_add_extra_outputs(vista)
