cmake_minimum_required(VERSION 3.12)
set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/ext/pico-sdk)
include(pico_sdk_import.cmake)

project(SD2PSX LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(SD2PSX_WITH_PSRAM ON CACHE BOOL "Build SD2PSX with PSRAM support")
set(SD2PSX_WITH_GUI   ON CACHE BOOL "Build SD2PSX with GUI support")

# Variants...
set(VARIANT "SD2PSX" CACHE STRING "Firmware variant to build")
set_property(CACHE VARIANT PROPERTY STRINGS
                    "SD2PSX" 
                    "PMC+" 
                    "SD2PSXlite" 
                    "SD2PSXBF" 
                    "Other")
string(TOLOWER "${VARIANT}" TARGET_NAME)
message(STATUS "Building for ${VARIANT}")
if (VARIANT STREQUAL "SD2PSX")
    set(PIN_PSX_ACK 16)
    set(PIN_PSX_SEL 17)
    set(PIN_PSX_CLK 18)
    set(PIN_PSX_CMD 19)
    set(PIN_PSX_DAT 20)
    set(PIN_PSX_SPD_SEL 10)
    add_compile_definitions("UART_TX=8"
                            "UART_RX=9"
                            "UART_PERIPH=uart1"
                            "UART_BAUD=3000000"
                            "SD_PERIPH=SPI1"
                            "SD_MISO=24"
                            "SD_MOSI=27"
                            "SD_SCK=26"
                            "SD_CS=29"
                            "FLASH_OFF_CIV=0x7fb000"
                            "FLASH_OFF_EEPROM=0x7fc000"
                            )
    set(SD2PSX_WITH_GUI TRUE)
    set(SD2PSX_WITH_PSRAM TRUE)
elseif (VARIANT STREQUAL "SD2PSXBF")
    set(PIN_PSX_ACK 16)
    set(PIN_PSX_SEL 17)
    set(PIN_PSX_CLK 18)
    set(PIN_PSX_CMD 19)
    set(PIN_PSX_DAT 20)
    set(PIN_PSX_SPD_SEL 10)
    add_compile_definitions("UART_TX=8"
                            "UART_RX=9"
                            "UART_PERIPH=uart1"
                            "UART_BAUD=3000000"
                            "SD_PERIPH=SPI1"
                            "SD_MISO=24"
                            "SD_MOSI=27"
                            "SD_SCK=26"
                            "SD_CS=29"
                            "FLASH_OFF_CIV=0x1fb000"
                            "FLASH_OFF_EEPROM=0x1fc000"
                            )
    set(SD2PSX_WITH_GUI TRUE)
    set(SD2PSX_WITH_PSRAM TRUE)
elseif( VARIANT STREQUAL "PMC+")
    set(PIN_PSX_ACK 9)
    set(PIN_PSX_SEL 7)
    set(PIN_PSX_CLK 8)
    set(PIN_PSX_CMD 6)
    set(PIN_PSX_DAT 5)
    set(PIN_PSX_SPD_SEL 10)
    add_compile_definitions("UART_TX=0"
                            "UART_RX=1"
                            "UART_PERIPH=uart1"
                            "UART_BAUD=115200"
                            "SD_PERIPH=SPI"
                            "SD_MISO=16"
                            "SD_MOSI=19"
                            "SD_SCK=18"
                            "SD_CS=17"
                            "FLASH_OFF_CIV=0x1fb000"
                            "FLASH_OFF_EEPROM=0x1fc000"
                            )
    set(SD2PSX_WITH_GUI FALSE)
    set(SD2PSX_WITH_PSRAM FALSE)
elseif( VARIANT STREQUAL "SD2PSXlite")
    set(PIN_PSX_ACK 16)
    set(PIN_PSX_SEL 17)
    set(PIN_PSX_CLK 18)
    set(PIN_PSX_CMD 19)
    set(PIN_PSX_DAT 20)
    set(PIN_PSX_SPD_SEL 10)
    add_compile_definitions("UART_TX=8"
                            "UART_RX=9"
                            "UART_PERIPH=uart1"
                            "UART_BAUD=3000000"
                            "SD_PERIPH=SPI1"
                            "SD_MISO=24"
                            "SD_MOSI=27"
                            "SD_SCK=26"
                            "SD_CS=29"
                            "FLASH_OFF_CIV=0x1fb000"
                            "FLASH_OFF_EEPROM=0x1fc000"
                            )
    set(SD2PSX_WITH_GUI FALSE)
    set(SD2PSX_WITH_PSRAM FALSE)
else()
    set(PIN_PSX_ACK 16)
    set(PIN_PSX_SEL 17)
    set(PIN_PSX_CLK 18)
    set(PIN_PSX_CMD 19)
    set(PIN_PSX_DAT 20)
    set(PIN_PSX_SPD_SEL 10)
endif()

pico_sdk_init()

#add_compile_definitions(PICO_FLASH_SIZE_BYTES=16777216)
add_compile_definitions(PICO_FLASH_SIZE_BYTES=2097152)


# Add all subdirectories for sub-targets

add_subdirectory(database)
add_subdirectory(misc)
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
        ps2_history_icons
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

option(DEBUG_USB_UART "Activate UART over USB for debugging" OFF)


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
