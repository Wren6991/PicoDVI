cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)

project(picodvi C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_compile_options(-Wall)

## todo board files
add_definitions(-DDEBUG_PIN0=22 -DDEBUG_N_PINS=4)
add_definitions(-DPICO_DEFAULT_UART_TX_PIN=16 -DPICO_DEFAULT_UART_RX_PIN=17)

# add_definitions(-DDEFAULT_DVI_SERIAL_CONFIG=picodvi_pmod0_cfg)
# add_definitions(-DRUN_FROM_CRYSTAL=1)
# add_definitions(-DDVI_SERIAL_DEBUG=1)

include_directories(
	assets
	include
	)

add_subdirectory(libdvi)
add_subdirectory(libsprite)

add_subdirectory(apps)
