set(WCH "${PROJECT_SOURCE_DIR}/vendor/wch")
set(CH32 "${CMAKE_CURRENT_SOURCE_DIR}/ch32v203")
set(WACCA_USB_VID "0x1209" CACHE STRING "Authorized USB vendor ID")
set(WACCA_USB_PID "0x0001" CACHE STRING "Authorized USB product ID")

function(add_mcu_program name)
	add_executable(${name}
		${name}/main.c
		common/board_pins.c common/usb_control.c common/protocol.c common/diagnostics.c
		"${CH32}/clock.c" "${CH32}/time.c" "${CH32}/usb_device.c"
		"${WCH}/Startup/startup_ch32v20x_D6.S"
		"${WCH}/Peripheral/src/ch32v20x_gpio.c"
		"${WCH}/Peripheral/src/ch32v20x_rcc.c")
	set_target_properties(${name} PROPERTIES SUFFIX ".elf")
	target_include_directories(${name} PRIVATE
		common "${CH32}" "${WCH}/Core" "${WCH}/Peripheral/inc"
		"${WCH}/System" "${WCH}/USB")
	target_compile_definitions(${name} PRIVATE CH32V20x_D6 HSE_VALUE=8000000
		WACCA_USB_VID=${WACCA_USB_VID} WACCA_USB_PID=${WACCA_USB_PID})
	target_compile_options(${name} PRIVATE
		-Os -g3 -ffunction-sections -fdata-sections -fno-common
		$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror>)
	target_link_options(${name} PRIVATE
		-march=rv32imac -mabi=ilp32 -msmall-data-limit=8 -mno-save-restore
		"-T${WCH}/Ld/Link.ld" -nostartfiles --specs=nano.specs --specs=nosys.specs
		-Wl,--gc-sections "-Wl,-Map,${CMAKE_BINARY_DIR}/${name}.map")
	# Re-link after edits to the memory layout, even if no C source changed.
	set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "${WCH}/Ld/Link.ld")
	add_custom_command(TARGET ${name} POST_BUILD
		COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${name}>" "${CMAKE_BINARY_DIR}/${name}.hex"
		COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${name}>" "${CMAKE_BINARY_DIR}/${name}.bin"
		COMMAND "${WCH_SIZE}" "$<TARGET_FILE:${name}>"
		VERBATIM)
endfunction()

add_mcu_program(touch)
target_sources(touch PRIVATE touch/hardware.c touch/usb2422.c "${CH32}/i2c.c")
target_include_directories(touch PRIVATE touch)
target_compile_definitions(touch PRIVATE WACCA_TOUCH_MCU=1)

add_mcu_program(led)
