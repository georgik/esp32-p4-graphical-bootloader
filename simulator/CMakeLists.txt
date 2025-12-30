cmake_minimum_required(VERSION 3.16)
project(esp32_p4_bootloader_simulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g")

# Find required packages
find_package(PkgConfig REQUIRED)

# SDL2
pkg_check_modules(SDL2 REQUIRED sdl2)
message(STATUS "SDL2 found: ${SDL2_INCLUDE_DIRS}")

# JSON-C for NVS emulation
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)
message(STATUS "JSON-C found: ${JSONC_INCLUDE_DIRS}")

# LVGL - Assume it's installed or fetch it
# For now, we'll assume it's in a subdirectory or system path
if(EXISTS "${CMAKE_SOURCE_DIR}/lvgl")
    set(LVGL_DIR "${CMAKE_SOURCE_DIR}/lvgl")
    message(STATUS "Using local LVGL: ${LVGL_DIR}")
else()
    # Try to find system LVGL
    if(EXISTS "/opt/homebrew/include/lvgl")
        set(LVGL_DIR "/opt/homebrew")
    elseif(EXISTS "/usr/local/include/lvgl")
        set(LVGL_DIR "/usr/local")
    else()
        message(FATAL_ERROR "LVGL not found. Please install or clone to ${CMAKE_SOURCE_DIR}/lvgl")
    endif()
endif()

# Include directories
include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${JSONC_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/lvgl  # LVGL root
    ${CMAKE_SOURCE_DIR}/..     # For lv_conf.h in parent
    ${CMAKE_SOURCE_DIR}/../main
    ${CMAKE_SOURCE_DIR}/mocks
    ${CMAKE_SOURCE_DIR}/platform
)

# Add simulator build flag
add_definitions(-D__SIMULATOR_BUILD__=1)

# Source files
set(SIMULATOR_SOURCES
    main.c
    platform/lvgl_sdl_init.c
    platform/flash_emulator.c
    platform/flash_builder.c
)

# Mock sources
set(MOCK_SOURCES
    mocks/freertos_mock.c
    mocks/esp_log_mock.c
    mocks/esp_system_mock.c
    mocks/esp_partition_mock.c
    mocks/nvs_mock.c
    mocks/bsp_mock.c
    mocks/esp_ota_ops_mock.c
    mocks/mbedtls_md5_mock.c
    mocks/sdmmc_mock.c
    mocks/firmware_flasher_mock.c
    mocks/vfs_mock.c
)

# Bootloader sources (reuse existing code)
set(BOOTLOADER_SOURCES
    ../main/main.c
    ../main/lvgl_bootloader.c
    ../main/firmware_selector.c
    ../main/firmware_validator.c
    ../main/partition_manager.c
    ../main/sd_ota.c
    # Skip firmware_flasher.c for initial UI testing
)

# Collect all LVGL source files
file(GLOB_RECURSE LVGL_ALL_SOURCES
    ${LVGL_DIR}/src/*.c
)

# Exclude specific directories we don't need
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*thorvg.*")
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*freetype.*")
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*lottie.*")
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*qrcode.*")
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*png.*")
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*jpeg.*")
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*gif.*")
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*bmp.*")
list(FILTER LVGL_ALL_SOURCES EXCLUDE REGEX ".*fsdrv.*")

# Create executable
add_executable(simulator
    ${SIMULATOR_SOURCES}
    ${MOCK_SOURCES}
    ${BOOTLOADER_SOURCES}
    ${LVGL_ALL_SOURCES}
)

# Link libraries
target_link_libraries(simulator
    ${SDL2_LIBRARIES}
    ${JSONC_LIBRARIES}
    pthread
    m
    z
)

# Add SDL2 library directory
target_link_directories(simulator PUBLIC /opt/homebrew/lib)

# Install LVGL if not present
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/lvgl")
    message(STATUS "LVGL not found, will download during build")
    add_custom_command(TARGET simulator PRE_BUILD
        COMMAND git clone --depth 1 --branch v9.2.0 https://github.com/lvgl/lvgl.git ${CMAKE_SOURCE_DIR}/lvgl
        COMMENT "Cloning LVGL v9.2.0"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "=== ESP32-P4 Bootloader Simulator Configuration ===")
message(STATUS "Build type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler:           ${CMAKE_C_COMPILER}")
message(STATUS "LVGL directory:       ${LVGL_DIR}")
message(STATUS "SDL2:                 ${SDL2_LIBRARIES}")
message(STATUS "JSON-C:               ${JSONC_LIBRARIES}")
message(STATUS "=================================================")
message(STATUS "")
