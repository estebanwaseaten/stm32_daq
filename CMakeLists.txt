# --- NOTES ---
#PROJECT_SOURCE_DIR	--> dir of most recent project() call
#CMAKE_SOURCE_DIR --> top level CMakeLists.txt
#CMAKE_CURRENT_SOURCE_DIR --> current CMakeLists.txt (add_subdirectory() call) should be similar to PROJECT_SOURCE_DIR
#Forget the commands add_compile_options, include_directories, link_directories, link_libraries.
#https://dev.to/pgradot/cmake-on-stm32-the-beginning-3766


cmake_minimum_required(VERSION 3.25.0)

project(STM32)

enable_language(C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR})

set(SOURCE_LIST
	"main.c"
	"stm32_lib/STM32.c"
)

message("*** using toolchain: ${CMAKE_TOOLCHAIN_FILE}")

#defines the target:
if( MCU MATCHES "L053" )
	message("*** BUILD L053")

	#could also refer to various linker scripts here...

	#add "library"
	set(SOURCE_LIST
		${SOURCE_LIST}
		"${CMAKE_SOURCE_DIR}/stm32_lib/STM32L053/STM32L053.c"
	)

	set(TARGET L053)
	set(LINKERSCRIPT /stm32_lib/STM32L053/STM32L053)
	set(DEFINITIONS STM32L053)		# uses target_compile_definitions so set #define STM32L053
	set(ELF ${CMAKE_SOURCE_DIR}/${TARGET}.elf)
	set(BINARY ${CMAKE_SOURCE_DIR}/${TARGET}.bin)
	set(PROCESSOR cortex-m0plus)
endif()

if( MCU MATCHES "F303" )
	message("*** BUILD F303")

	#could also refer to various linker scripts here...

	#add "library"

	file(GLOB F303_SOURCE_LIST CONFIGURE_DEPENDS
    	"${CMAKE_SOURCE_DIR}/stm32_lib/STM32F303/*.c"	#needed to  expand wildcard
	)

	set(SOURCE_LIST
		${SOURCE_LIST}
		${F303_SOURCE_LIST}
	)

	set(TARGET F303)
	set(LINKERSCRIPT /stm32_lib/STM32F303/STM32F303)
	set(DEFINITIONS STM32F303)		# uses target_compile_definitions so set #define STM32F303
	set(ELF ${CMAKE_SOURCE_DIR}/${TARGET}.elf)
	set(BINARY ${CMAKE_SOURCE_DIR}/${TARGET}.bin)
	set(PROCESSOR cortex-m4)
endif()

add_executable(${TARGET} ${SOURCE_LIST})

set_target_properties( ${TARGET} PROPERTIES OUTPUT_NAME ${TARGET}.elf )
target_compile_definitions(${TARGET} PRIVATE ${DEFINITIONS})
target_compile_options(${TARGET} PRIVATE -mcpu=${PROCESSOR} -mthumb -Wall -Werror -O0 -nostdlib -ffunction-sections -fdata-sections)
target_link_options(${TARGET} PRIVATE -mcpu=${PROCESSOR} -mthumb -Wall -Werror -O0 -nostdlib -T${CMAKE_SOURCE_DIR}${LINKERSCRIPT}.ld )
target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

#show size of
add_custom_command( TARGET ${TARGET} POST_BUILD COMMAND arm-none-eabi-size ${ELF})

#extract binary data from elf file
add_custom_command( TARGET ${TARGET} POST_BUILD COMMAND arm-none-eabi-objcopy -O binary ${ELF} ${BINARY})
