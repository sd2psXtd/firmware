cmake_minimum_required(VERSION 3.12)
set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/ext/pico-sdk)
include(pico_sdk_import.cmake)

project(SD2PSX LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# set(PICO_COPY_TO_RAM 1)

pico_sdk_init()

add_compile_definitions(PICO_FLASH_SIZE_BYTES=16777216)

# Add all subdirectories for sub-targets

add_subdirectory(database)
add_subdirectory(ps2boot)
add_subdirectory(src/version)
add_subdirectory(src/ps2)
add_subdirectory(src/ps1)
add_subdirectory(ext/)

# SD2PSX Main Lib

add_executable(sd2psx
    src/main.c

    src/game_names/game_names.c
    src/wear_leveling/wear_leveling.c
    src/wear_leveling/wear_leveling_rp2040_flash.c

    ext/fnv/hash_64a.c
)

target_compile_definitions(
    sd2psx PUBLIC
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
)


target_include_directories(sd2psx PUBLIC
    ext/fnv
)

target_link_libraries(sd2psx
    PRIVATE
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_i2c
        hardware_flash
        gamedb
        ps2boot
        sd2psx_version
        sd2psx_common
        ps1_card
        ps2_card
        sd_fat
)

add_dependencies(sd2psx gamedb ps2boot)
set_target_properties(sd2psx PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/memmap_custom.ld)

# Common Lib

add_library(sd2psx_common STATIC     
                ${CMAKE_CURRENT_SOURCE_DIR}/src/debug.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/gui.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/input.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_menu.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_theme_mono.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/des.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/keystore.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/settings.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/bigmem.c
                ${CMAKE_CURRENT_SOURCE_DIR}/src/oled.c)

target_include_directories(sd2psx_common 
                PUBLIC 
                    ${CMAKE_CURRENT_SOURCE_DIR}/src
                PRIVATE )

target_link_libraries(sd2psx_common 
                    PUBLIC
                        lvgl::lvgl
                    PRIVATE 
                        pico_platform_headers
                        ssd1306
                        hardware_flash
                        pico_multicore)

target_compile_options(sd2psx_common
                        PUBLIC
                            -Wall -Wextra
                            -fno-jump-tables)

target_compile_definitions(sd2psx_common PUBLIC 
                            USE_SPI_ARRAY_TRANSFER=1)

set_target_properties(sd2psx_common PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/memmap_custom.ld)

pico_add_extra_outputs(sd2psx)

set(DEBUG_USB_UART OFF CACHE BOOL "Activate UART over USB for debugging")

if(DEBUG_USB_UART)
    target_compile_definitions(sd2psx_common PUBLIC -DDEBUG_USB_UART)
    pico_enable_stdio_usb(sd2psx ON)
endif()
