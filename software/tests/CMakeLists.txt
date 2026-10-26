# These programs run on the PC and simulate hardware; they are never flashed.
function(add_wacca_test name source)
	add_executable(${name} ${source} ${ARGN})
	target_link_libraries(${name} PRIVATE wacca_core)
	if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(${name} PRIVATE -UNDEBUG -Wall -Wextra -Werror)
		if(NOT name STREQUAL "wacca_usb_device_test")
			target_compile_options(${name} PRIVATE
				-Wconversion -Wshadow -Wstrict-prototypes -Wpedantic)
		endif()
	elseif(MSVC)
		target_compile_options(${name} PRIVATE /UNDEBUG)
	endif()
	add_test(NAME ${name} COMMAND ${name})
endfunction()

add_wacca_test(wacca_core_test test_core.c)
add_wacca_test(wacca_usb_test test_usb.c)
add_wacca_test(wacca_protocol_test test_protocol.c)

add_wacca_test(wacca_i2c_test test_hardware_i2c.c
	"${PROJECT_SOURCE_DIR}/firmware/ch32v203/i2c.c")
target_compile_definitions(wacca_i2c_test PRIVATE WACCA_I2C_SIMULATION=1)
target_include_directories(wacca_i2c_test PRIVATE
	i2c_sim "${PROJECT_SOURCE_DIR}/firmware/ch32v203")

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	add_wacca_test(wacca_usb_device_test test_usb_device.c
		"${PROJECT_SOURCE_DIR}/firmware/ch32v203/usb_device.c")
	target_compile_definitions(wacca_usb_device_test PRIVATE WACCA_USB_SIMULATION=1)
	target_include_directories(wacca_usb_device_test PRIVATE
		usb_sim "${PROJECT_SOURCE_DIR}/firmware/ch32v203"
		"${PROJECT_SOURCE_DIR}/vendor/wch/USB")
endif()
