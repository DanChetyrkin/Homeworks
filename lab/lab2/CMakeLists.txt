cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME lab2)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# ---------------------------------------------------------------------------
# Lab 2: build one task instead of main.c.
#   LAB_TASK = A1, A2, A3a, A3b, A3c, B1, B2, B3 (empty: use main.c)
# The presets in CMakePresets.json set it; build/<task>/lab2.elf is the result.
# ---------------------------------------------------------------------------
set(LAB_TASK "" CACHE STRING "Lab 2 task to build instead of main.c")
set_property(CACHE LAB_TASK PROPERTY STRINGS "" A1 A2 A3a A3b A3c B1 B2 B3)

if(LAB_TASK)
    set(LAB_DEFINES "")
    if(LAB_TASK STREQUAL "A1")
        set(LAB_FILE lab2partAtask1.c)
    elseif(LAB_TASK STREQUAL "A2")
        set(LAB_FILE lab2partAtask2.c)
    elseif(LAB_TASK STREQUAL "A3a")     # echo by polling, no delay
        set(LAB_FILE lab2partAtask3.c)
        set(LAB_DEFINES A3_ENABLE_DELAY=0 ECHO_BY_INTERRUPT=0)
    elseif(LAB_TASK STREQUAL "A3b")     # polling + busy-wait delay
        set(LAB_FILE lab2partAtask3.c)
        set(LAB_DEFINES A3_ENABLE_DELAY=1 ECHO_BY_INTERRUPT=0)
    elseif(LAB_TASK STREQUAL "A3c")     # echo by interrupt + delay
        set(LAB_FILE lab2partAtask3.c)
        set(LAB_DEFINES A3_ENABLE_DELAY=1 ECHO_BY_INTERRUPT=1)
    elseif(LAB_TASK MATCHES "^B[123]$")
        string(SUBSTRING "${LAB_TASK}" 1 1 LAB_N)
        set(LAB_FILE lab2partBtask${LAB_N}.c)
    else()
        message(FATAL_ERROR "Unknown LAB_TASK '${LAB_TASK}'")
    endif()

    # Replace main.c with the task file
    get_target_property(LAB_SOURCES ${CMAKE_PROJECT_NAME} SOURCES)
    list(FILTER LAB_SOURCES EXCLUDE REGEX "/Core/Src/main\\.c$")
    list(APPEND LAB_SOURCES ${CMAKE_SOURCE_DIR}/Core/Src/${LAB_FILE})
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SOURCES ${LAB_SOURCES})
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ${LAB_DEFINES})

    # Part B uses the HAL UART driver, its interrupt handler, and its MSP.
    # Add our copies only if the CubeMX-generated files do not have them
    # (once LPUART1 is enabled in lab2.ioc, stm32g4xx_it.c already does).
    file(READ ${CMAKE_SOURCE_DIR}/Core/Src/stm32g4xx_it.c LAB_IT_C)
    string(FIND "${LAB_IT_C}" "LPUART1_IRQHandler" LAB_HAS_UART_IRQ)
    # Part A drives LPUART1 through its registers (no hlpuart1; A3c has its
    # own handler), so rename the generated HAL handler out of the vector
    # table; --gc-sections then drops it.
    if(LAB_TASK MATCHES "^A" AND NOT LAB_HAS_UART_IRQ EQUAL -1)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/Core/Src/stm32g4xx_it.c
            TARGET_DIRECTORY ${CMAKE_PROJECT_NAME}
            PROPERTIES COMPILE_DEFINITIONS "LPUART1_IRQHandler=CubeMX_LPUART1_IRQHandler_unused")
    endif()
    if(LAB_TASK MATCHES "^B" AND LAB_HAS_UART_IRQ EQUAL -1)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/partB/partB_support.c
            ${CMAKE_SOURCE_DIR}/partB/hal/stm32g4xx_hal_uart.c
            ${CMAKE_SOURCE_DIR}/partB/hal/stm32g4xx_hal_uart_ex.c
        )
        target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/partB/hal)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HAL_UART_MODULE_ENABLED)
    endif()

    message(STATUS "Lab 2 task ${LAB_TASK}: Core/Src/${LAB_FILE} ${LAB_DEFINES}")
endif()

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
