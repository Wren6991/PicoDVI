add_executable(colortext
	main.c
	font.h
	gen.h
	gen.c
	scanout.h
	scanout.S
	text.h
	text.c
)

# TODO this should work ok with DVI_N_TMDS_BUFFERS=2 (perhaps need to
# rearrange some pushes/pops) and also as we are monochrome the buffers are 3x
# as big as they need to be 
target_compile_definitions(colortext PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	DVI_VERTICAL_REPEAT=1
	DVI_N_TMDS_BUFFERS=3
	DVI_MONOCHROME_TMDS=0
	)

target_link_libraries(colortext
	pico_stdlib
	pico_multicore
	pico_util
	libdvi
)

# create map/bin/hex file etc.
pico_add_extra_outputs(colortext)
