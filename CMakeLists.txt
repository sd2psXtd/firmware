cmake_minimum_required(VERSION 3.12)
# Pico SDK
set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/ext/pico-sdk)
include(pico_sdk_import.cmake)

project(SD2PSX LANGUAGES C CXX ASM)

pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(SD2PSX_WITH_PSRAM "Build SD2PSX with PSRAM support" ON)
option(SD2PSX_WITH_GUI   "Build SD2PSX with GUI support" ON)
option(DEBUG_USB_UART "Activate UART over USB for debugging" OFF)

# variants
include(misc/variants.cmake)

# set proper target name
set(TARGET_NAME)
if (DEBUG_USB_UART)
    string(TOLOWER "${VARIANT}-debug" TARGET_NAME PARENT_SCOPE)
else()
    string(TOLOWER "${VARIANT}" TARGET_NAME PARENT_SCOPE)
endif()

message(STATUS "Building ${TARGET_NAME}")
#add_compile_definitions(PICO_FLASH_SIZE_BYTES=16777216)
add_compile_definitions(PICO_FLASH_SIZE_BYTES=2097152)


# Add all subdirectories for sub-targets

add_subdirectory(database)
add_subdirectory(src/version)
add_subdirectory(src/ps2)
add_subdirectory(src/ps1)
if (SD2PSX_WITH_PSRAM)
    add_subdirectory(src/psram)
endif()
add_subdirectory(ext/)

# SD2PSX Main Lib

add_executable(${TARGET_NAME}
    src/main.c
    src/ps2.c
    src/ps1.c

    src/game_db/game_db.c
    src/wear_leveling/wear_leveling.c
    src/wear_leveling/wear_leveling_rp2040_flash.c

    ext/fnv/hash_64a.c
)

target_compile_definitions(
    ${TARGET_NAME} PUBLIC
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)

target_include_directories(${TARGET_NAME} PUBLIC
    ext/fnv
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_i2c
        hardware_flash
        gamedb
        sd2psx_version
        sd2psx_common
        ps1_card
        ps2_card
        sd_fat
)

add_dependencies(${TARGET_NAME} gamedb)
set_target_properties(${TARGET_NAME} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/memmap_custom.ld)

# Common Lib

add_library(sd2psx_common STATIC     
                ${CMAKE_CURRENT_SOURCE_DIR}/src/debug.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/input.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/des.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/keystore.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/settings.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/bigmem.c)

target_include_directories(sd2psx_common 
                PUBLIC 
                    ${CMAKE_CURRENT_SOURCE_DIR}/src
                PRIVATE )

target_link_libraries(sd2psx_common 
                    PRIVATE 
                        pico_platform_headers
                        hardware_flash
                        hardware_gpio
                        pico_multicore)

target_compile_options(sd2psx_common
                        PUBLIC
                            -Wall -Wextra
                            -fno-jump-tables)

target_compile_definitions(sd2psx_common PUBLIC 
                            USE_SPI_ARRAY_TRANSFER=1)
if (SD2PSX_WITH_PSRAM)
    target_compile_definitions(sd2psx_common PUBLIC "WITH_PSRAM=1")
endif()

if (SD2PSX_WITH_GUI)    
    target_compile_definitions(sd2psx_common PUBLIC "WITH_GUI=1")
    target_sources(sd2psx_common PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src/gui.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/oled.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_menu.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_theme_mono.c)
    target_link_libraries(sd2psx_common 
                PUBLIC
                    lvgl::lvgl
                PRIVATE 
                    ssd1306)
endif()

set_target_properties(sd2psx_common PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/memmap_custom.ld)

pico_add_extra_outputs(${TARGET_NAME})



if(DEBUG_USB_UART)
    set(DEBUG_STARTUP_DELAY 0 CACHE STRING "Startup Delay for Debug Output")
    if (NOT ${DEBUG_STARTUP_DELAY} MATCHES "[0-9]+")
        message(FATAL_ERROR "Misconfigured Debug Startup Delay: ${DEBUG_STARTUP_DELAY}")
    endif()
    target_compile_definitions(sd2psx_common PUBLIC -DDEBUG_USB_UART -DMMCE_DEBUG -DPICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=${DEBUG_STARTUP_DELAY}*1000)
    pico_enable_stdio_usb(${TARGET_NAME} 1)
else()
    pico_enable_stdio_usb(${TARGET_NAME} 0)
endif()
