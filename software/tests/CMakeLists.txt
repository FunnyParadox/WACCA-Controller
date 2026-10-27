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

add_wacca_test(wacca_update_test test_update.c "${PROJECT_SOURCE_DIR}/host/update_client.c")
target_include_directories(wacca_update_test PRIVATE "${PROJECT_SOURCE_DIR}/host")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME wacca_package_test COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_package.py")

add_wacca_test(wacca_diagnostics_test test_diagnostics.c
	"${PROJECT_SOURCE_DIR}/firmware/common/diagnostics.c")
target_include_directories(wacca_diagnostics_test PRIVATE "${PROJECT_SOURCE_DIR}/firmware/ch32v203")

if(WIN32)
	find_program(WACCA_POWERSHELL NAMES powershell.exe REQUIRED)
	add_test(NAME wacca_flash_script_test COMMAND "${WACCA_POWERSHELL}" -NoProfile -File
		"${CMAKE_CURRENT_SOURCE_DIR}/test_flash.ps1" -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}")
endif()
