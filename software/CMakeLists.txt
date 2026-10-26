cmake_minimum_required(VERSION 3.20)
project(wacca_controller LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# One source entry point, separate build directories for the MCU and the PC.
if(CMAKE_CROSSCOMPILING)
	enable_language(ASM)
	set(CMAKE_C_STANDARD 11)
	set(CMAKE_C_EXTENSIONS ON) # WCH headers use GNU inline assembly.
	add_subdirectory(firmware)
else()
	set(CMAKE_C_STANDARD 23)
	set(CMAKE_C_EXTENSIONS OFF)
	enable_testing()

	add_library(wacca_core STATIC
		firmware/common/board_pins.c
		firmware/common/usb_control.c
		firmware/common/protocol.c
		firmware/touch/usb2422.c)
	target_include_directories(wacca_core PUBLIC
		firmware/common firmware/touch)
	if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(wacca_core PRIVATE
			-Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wpedantic)
	endif()

	add_subdirectory(tests)
	if(WIN32)
		add_executable(wacca_probe host/wacca_probe.c)
		target_link_libraries(wacca_probe PRIVATE wacca_core)
		if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
			target_compile_options(wacca_probe PRIVATE
				-Wall -Wextra -Werror -Wconversion -Wshadow -Wstrict-prototypes -Wpedantic)
		endif()
	endif()
endif()
