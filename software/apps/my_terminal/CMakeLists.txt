add_executable(my_terminal
    main.c
)

pico_enable_stdio_usb(my_terminal 1)

target_compile_definitions(my_terminal PRIVATE
    DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
    DVI_VERTICAL_REPEAT=1
    DVI_N_TMDS_BUFFERS=4
    DVI_MONOCHROME_TMDS=1
)

target_link_libraries(my_terminal
    pico_stdlib
    hardware_i2c
    pico_i2c_slave
    hardware_pio
    hardware_interp
    pico_multicore
    pico_util
    hardware_dma
    libdvi
)

target_include_directories(my_terminal PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include
    ${CMAKE_CURRENT_LIST_DIR}/../../libdvi
    ${PICO_SDK_PATH}/src/rp2_common/pico_i2c_slave/include
)

pico_add_extra_outputs(my_terminal)
