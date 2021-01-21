add_executable(hyperram_test
	main.c
)

target_link_libraries(hyperram_test
	pico_stdlib
	libhyperram
)

pico_add_extra_outputs(hyperram_test)
