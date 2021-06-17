add_executable(vista
	main.c
)

# All code and data must be RAM-resident so we can stream continuously from
# flash using direct SSI DMA, which would trample on XIP.
pico_set_binary_type(vista copy_to_ram)

target_compile_definitions(vista PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
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
