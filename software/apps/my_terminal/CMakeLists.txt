add_executable(my_terminal
    main.c
)

target_compile_definitions(my_terminal PRIVATE
    DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
    DVI_VERTICAL_REPEAT=1
    DVI_N_TMDS_BUFFERS=3
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



# add_executable(my_terminal
#     main.c
# )

# target_compile_definitions(my_terminal PRIVATE
#     DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
#     DVI_VERTICAL_REPEAT=1
#     DVI_N_TMDS_BUFFERS=3
#     DVI_MONOCHROME_TMDS=1
# )

# target_link_libraries(my_terminal
#     pico_stdlib
#     hardware_pio
#     hardware_interp
#     libdvi
#     pico_multicore
#     pico_util
#     hardware_dma
# )

# target_include_directories(my_terminal PRIVATE
#     ${CMAKE_CURRENT_LIST_DIR}/../../include
#     ${CMAKE_CURRENT_LIST_DIR}/../../libdvi
# )

# pico_add_extra_outputs(my_terminal)

# ## Replace TMDS with 10 bit UART (same baud rate):
# ## add_definitions(-DDVI_SERIAL_DEBUG=1)
# ## add_definitions(-DRUN_FROM_CRYSTAL)
# #
# #add_executable(terminal
# #	main.c
# #)
# #
# ## TODO this should work ok with DVI_N_TMDS_BUFFERS=2 (perhaps need to
# ## rearrange some pushes/pops) and also as we are monochrome the buffers are 3x
# ## as big as they need to be 
# #target_compile_definitions(terminal PRIVATE
# #	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
# #	DVI_VERTICAL_REPEAT=1
# #	DVI_N_TMDS_BUFFERS=3
# #	DVI_MONOCHROME_TMDS=1
# #	)
# #
# #target_link_libraries(terminal
# #	pico_stdlib
# #	pico_multicore
# #	pico_util
# #	libdvi
# #)
# #
# ## create map/bin/hex file etc.
# #pico_add_extra_outputs(terminal)
