# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

include(pimoroni_pico_import.cmake)
include(drivers/fatfs/fatfs OPTIONAL RESULT_VARIABLE INC_FATFS)
include(drivers/sdcard/sdcard OPTIONAL RESULT_VARIABLE INC_SDCARD)

if(DEFINED INC_FATFS AND DEFINED INC_SDCARD)

	add_executable(raw_av_player
		main.cpp
	)

	target_compile_options(raw_av_player PRIVATE -Wall -Wno-narrowing)

	target_compile_definitions(raw_av_player PRIVATE
		DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
		SDCARD_PIN_SPI0_CS=22
		SDCARD_PIN_SPI0_SCK=5
		SDCARD_PIN_SPI0_MOSI=18
		SDCARD_PIN_SPI0_MISO=19
		SDCARD_PIO=pio1
		SDCARD_PIO_SM=0
		# compile time configuration of I2S
		PICO_AUDIO_I2S_MONO_INPUT=1
		#define for our example code
		USE_AUDIO_I2S=1
		PICO_AUDIO_I2S_DMA_IRQ=1
		PICO_AUDIO_I2S_PIO=1
		PICO_AUDIO_I2S_DATA=26
		PICO_AUDIO_I2S_BCLK=27
		)

	target_link_libraries(raw_av_player
		pico_stdlib
		pico_multicore
		pico_util
		libdvi
		libsprite
		sdcard
		fatfs
		pico_audio_i2s
	)

	# create map/bin/hex file etc.
	pico_add_extra_outputs(raw_av_player)
else()
    message(WARNING "Directory '${PIMORONI_PICO_PATH}' does not appear to contain the Pimoroni Pico libraries: skipping raw_av_player")
endif()