add_library(libsprite INTERFACE)

target_sources(libsprite INTERFACE
	${CMAKE_CURRENT_LIST_DIR}/affine_transform.h
	${CMAKE_CURRENT_LIST_DIR}/sprite_asm_const.h
	${CMAKE_CURRENT_LIST_DIR}/sprite.S
	${CMAKE_CURRENT_LIST_DIR}/sprite.c
	${CMAKE_CURRENT_LIST_DIR}/sprite.h
	${CMAKE_CURRENT_LIST_DIR}/tile.S
	${CMAKE_CURRENT_LIST_DIR}/tile.c
	${CMAKE_CURRENT_LIST_DIR}/tile.h
	)

target_include_directories(libsprite INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(libsprite INTERFACE pico_base_headers hardware_interp)
