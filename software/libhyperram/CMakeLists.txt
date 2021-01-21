# Note we are using INTERFACE so that the library can be configured per-app
# with compile-time defines

add_library(libhyperram INTERFACE)

target_sources(libhyperram INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/hyperram.c
	${CMAKE_CURRENT_LIST_DIR}/hyperram.h
	)

target_include_directories(libhyperram INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(libhyperram INTERFACE
	pico_base_headers
	hardware_dma
	hardware_pio
	)

pico_generate_pio_header(libhyperram ${CMAKE_CURRENT_LIST_DIR}/hyperram.pio)
