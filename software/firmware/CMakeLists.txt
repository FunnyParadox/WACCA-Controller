set(WCH "${PROJECT_SOURCE_DIR}/vendor/wch")
set(CH32 "${CMAKE_CURRENT_SOURCE_DIR}/ch32v203")
set(WACCA_USB_VID "0x1209" CACHE STRING "Authorized USB vendor ID")
set(WACCA_USB_PID "0x0001" CACHE STRING "Authorized USB product ID")
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# One preprocessed linker layout keeps C, image packages and flash partitions aligned.
foreach(kind IN ITEMS application bootloader)
	set(layout "${CMAKE_CURRENT_BINARY_DIR}/${kind}.ld")
	set(definitions)
	if(kind STREQUAL "bootloader")
		list(APPEND definitions -DWACCA_BOOTLOADER=1)
	endif()
	add_custom_command(OUTPUT "${layout}"
		COMMAND "${CMAKE_C_COMPILER}" -E -P -x c ${definitions}
			-I "${CMAKE_CURRENT_SOURCE_DIR}/common"
			"${CMAKE_CURRENT_SOURCE_DIR}/bootloader/link.ld.S" -o "${layout}"
		DEPENDS bootloader/link.ld.S common/image_layout.h VERBATIM)
	add_custom_target(${kind}_layout DEPENDS "${layout}")
endforeach()

function(add_mcu_image target source kind)
	add_executable(${target} ${source}
		common/board_pins.c common/usb_control.c common/protocol.c common/update.c
		"${CH32}/clock.c" "${CH32}/time.c" "${CH32}/usb_device.c" "${CH32}/reboot.c"
		"${WCH}/Startup/startup_ch32v20x_D6.S"
		"${WCH}/Peripheral/src/ch32v20x_gpio.c"
		"${WCH}/Peripheral/src/ch32v20x_rcc.c")
	set(artifact ${target})
	if(kind STREQUAL "application")
		string(REGEX REPLACE "_application$" "" artifact "${target}")
	endif()
	set_target_properties(${target} PROPERTIES SUFFIX ".elf" OUTPUT_NAME ${artifact})
	target_include_directories(${target} PRIVATE
		common bootloader "${CH32}" "${WCH}/Core" "${WCH}/Peripheral/inc"
		"${WCH}/System" "${WCH}/USB")
	target_compile_definitions(${target} PRIVATE CH32V20x_D6 HSE_VALUE=8000000
		WACCA_USB_VID=${WACCA_USB_VID} WACCA_USB_PID=${WACCA_USB_PID})
	target_compile_options(${target} PRIVATE
		-Os -g3 -ffunction-sections -fdata-sections -fno-common
		$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror>)
	set(layout "${CMAKE_CURRENT_BINARY_DIR}/${kind}.ld")
	target_link_options(${target} PRIVATE
		-march=rv32imac -mabi=ilp32 -msmall-data-limit=8 -mno-save-restore
		"-T${layout}" -nostartfiles --specs=nano.specs --specs=nosys.specs
		-Wl,--gc-sections "-Wl,-Map,${CMAKE_BINARY_DIR}/${target}.map")
	add_dependencies(${target} ${kind}_layout)
	set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${layout}")
	add_custom_command(TARGET ${target} POST_BUILD
		BYPRODUCTS "${CMAKE_BINARY_DIR}/${artifact}.bin" "${CMAKE_BINARY_DIR}/${artifact}.hex"
		COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${target}>" "${CMAKE_BINARY_DIR}/${artifact}.hex"
		COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${target}>" "${CMAKE_BINARY_DIR}/${artifact}.bin"
		COMMAND "${WCH_SIZE}" "$<TARGET_FILE:${target}>" VERBATIM)
endfunction()

foreach(name IN ITEMS touch led)
	add_mcu_image(${name}_bootloader bootloader/main.c bootloader)
	target_sources(${name}_bootloader PRIVATE bootloader/flash.c)
	target_compile_definitions(${name}_bootloader PRIVATE WACCA_BOOTLOADER=1)
	add_mcu_image(${name}_application ${name}/main.c application)
	target_sources(${name}_application PRIVATE common/diagnostics.c)
	if(name STREQUAL "touch")
		set(role 1)
		target_compile_definitions(touch_bootloader PRIVATE WACCA_TOUCH_MCU=1)
		target_compile_definitions(touch_application PRIVATE WACCA_TOUCH_MCU=1)
		target_sources(touch_application PRIVATE touch/hardware.c "${CH32}/i2c.c")
		target_include_directories(touch_application PRIVATE touch)
	else()
		set(role 2)
	endif()
	add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/${name}.wcfu" "${CMAKE_BINARY_DIR}/${name}.factory.hex"
		COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/pack_firmware.py"
			--boot "${CMAKE_BINARY_DIR}/${name}_bootloader.bin"
			--app "${CMAKE_BINARY_DIR}/${name}.bin"
			--role ${role} --output "${CMAKE_BINARY_DIR}/${name}"
			--layout "${CMAKE_CURRENT_SOURCE_DIR}/common/image_layout.h"
		DEPENDS ${name}_bootloader ${name}_application
			"${PROJECT_SOURCE_DIR}/tools/pack_firmware.py" common/image_layout.h VERBATIM)
	add_custom_target(${name} ALL DEPENDS
		"${CMAKE_BINARY_DIR}/${name}.wcfu" "${CMAKE_BINARY_DIR}/${name}.factory.hex")
endforeach()
