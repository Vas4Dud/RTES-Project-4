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
set(CMAKE_PROJECT_NAME P4_PhonoScope)

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
    # CMSIS-DSP prebuilt library (hard-float M4F variant) — spec R16
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/DSP/Lib/GCC
    # ST PDM2PCM decimation library (pre-vendored, from STM32CubeWB) — spec R16
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_Audio/PDM2PCM/Lib
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/tasks.c
    App/audio.c
    App/dsp.c
    App/perf_mon.c
    App/oled.c
    App/console.c
    App/rgb_led.c
    App/cmd_table.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
    # CMSIS-DSP headers (arm_math.h) — spec R16
    Drivers/CMSIS/DSP/Include
    # PDM2PCM headers (pdm2pcm_glo.h) — spec R16
    Middlewares/ST/STM32_Audio/PDM2PCM/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # CMSIS-DSP for Cortex-M4F — spec R16
    ARM_MATH_CM4
    __FPU_PRESENT=1U
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)


target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    arm_cortexM4lf_math
    PDMFilter_CM4_GCC_wc32
    m
)
