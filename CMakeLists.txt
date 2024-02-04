cmake_minimum_required(VERSION 3.12)

set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/ext/pico-sdk)
include(pico_sdk_import.cmake)

project(pico_examples C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# set(PICO_COPY_TO_RAM 1)

pico_sdk_init()

set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/lv_conf.h CACHE STRING "" FORCE)
add_subdirectory(ext/lvgl EXCLUDE_FROM_ALL)
add_subdirectory(ext/mcfat)

target_compile_definitions(mcfat PRIVATE DMAX_CACHEENTRY=6 MAX_FDHANDLES=1)

add_subdirectory(database)
add_subdirectory(ps2boot)
add_subdirectory(src/version)

add_executable(sd2psx
    src/main.c
    src/debug.c
    src/gui.c
    src/input.c
    src/ui_menu.c
    src/ui_theme_mono.c
    src/des.c
    src/keystore.c
    src/settings.c
    src/bigmem.c
    src/oled.c

    src/ps1/ps1_cardman.c
    src/ps1/ps1_dirty.c
    src/ps1/ps1_memory_card.c
    src/ps1/ps1_empty_card.c
    src/ps1/ps1_odeman.c

    src/ps2/ps2_memory_card.c
    src/ps2/ps2_dirty.c
    src/ps2/ps2_cardman.c
    src/ps2/ps2_pio_qspi.c
    src/ps2/ps2_psram.c
    src/ps2/ps2_exploit.c
    src/ps2/ps2_history_tracker.c
    src/ps2/ps2_sd2psxman.c

    src/game_names/game_names.c

    src/wear_leveling/wear_leveling.c
    src/wear_leveling/wear_leveling_rp2040_flash.c

    src/arduino_wrapper/sd.cpp
    src/arduino_wrapper/SPI.cpp

    ext/ESP8266SdFat/src/common/FmtNumber.cpp
    ext/ESP8266SdFat/src/common/FsCache.cpp
    ext/ESP8266SdFat/src/common/FsDateTime.cpp
    ext/ESP8266SdFat/src/common/FsName.cpp
    ext/ESP8266SdFat/src/common/FsStructs.cpp
    ext/ESP8266SdFat/src/common/FsUtf.cpp
    ext/ESP8266SdFat/src/common/upcase.cpp
    ext/ESP8266SdFat/src/ExFatLib/ExFatDbg.cpp
    ext/ESP8266SdFat/src/ExFatLib/ExFatFile.cpp
    ext/ESP8266SdFat/src/ExFatLib/ExFatFilePrint.cpp
    ext/ESP8266SdFat/src/ExFatLib/ExFatFileWrite.cpp
    ext/ESP8266SdFat/src/ExFatLib/ExFatFormatter.cpp
    ext/ESP8266SdFat/src/ExFatLib/ExFatName.cpp
    ext/ESP8266SdFat/src/ExFatLib/ExFatPartition.cpp
    ext/ESP8266SdFat/src/ExFatLib/ExFatVolume.cpp
    ext/ESP8266SdFat/src/FatLib/FatDbg.cpp
    ext/ESP8266SdFat/src/FatLib/FatFile.cpp
    ext/ESP8266SdFat/src/FatLib/FatFileLFN.cpp
    ext/ESP8266SdFat/src/FatLib/FatFilePrint.cpp
    ext/ESP8266SdFat/src/FatLib/FatFileSFN.cpp
    ext/ESP8266SdFat/src/FatLib/FatFormatter.cpp
    ext/ESP8266SdFat/src/FatLib/FatName.cpp
    ext/ESP8266SdFat/src/FatLib/FatPartition.cpp
    ext/ESP8266SdFat/src/FatLib/FatVolume.cpp
    ext/ESP8266SdFat/src/FreeStack.cpp
    ext/ESP8266SdFat/src/FsLib/FsFile.cpp
    ext/ESP8266SdFat/src/FsLib/FsNew.cpp
    ext/ESP8266SdFat/src/FsLib/FsVolume.cpp
    ext/ESP8266SdFat/src/iostream/istream.cpp
    ext/ESP8266SdFat/src/iostream/ostream.cpp
    ext/ESP8266SdFat/src/iostream/StdioStream.cpp
    ext/ESP8266SdFat/src/iostream/StreamBaseClass.cpp
    ext/ESP8266SdFat/src/MinimumSerial.cpp
    ext/ESP8266SdFat/src/SdCard/SdCardInfo.cpp
    ext/ESP8266SdFat/src/SdCard/SdSpiCard.cpp

    ext/fnv/hash_64a.c
)

add_library(ssd1306 STATIC ext/pico-ssd1306/ssd1306.c)
target_link_libraries(ssd1306 pico_stdlib hardware_i2c)

target_compile_definitions(
    sd2psx PUBLIC
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    PICO_FLASH_SIZE_BYTES=16777216
    USE_SPI_ARRAY_TRANSFER=1
)

target_compile_options(
    sd2psx PRIVATE
    -Wall -Wextra
    -fno-jump-tables
)

target_include_directories(sd2psx PUBLIC
    src/arduino_wrapper

    ext/pico-ssd1306
    ext/ESP8266SdFat/src
    ext/ESP8266SdFat/extras/attic
    ext/fnv
)

pico_generate_pio_header(sd2psx ${CMAKE_CURRENT_LIST_DIR}/src/ps1/ps1_mc_spi.pio)
pico_generate_pio_header(sd2psx ${CMAKE_CURRENT_LIST_DIR}/src/ps2/ps2_mc_spi.pio)
pico_generate_pio_header(sd2psx ${CMAKE_CURRENT_LIST_DIR}/src/ps2/ps2_qspi.pio)

target_link_libraries(sd2psx
    ssd1306
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_spi
    hardware_i2c
    hardware_flash
    hardware_dma
    lvgl::lvgl
    gamedb
    ps2boot
    sd2psx_version
    mcfat
)

add_dependencies(sd2psx gamedb ps2boot)
set_target_properties(sd2psx PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/memmap_custom.ld)

pico_add_extra_outputs(sd2psx)

set(DEBUG_USB_UART OFF CACHE BOOL "Activate UART over USB for debugging")

if(DEBUG_USB_UART)
    add_definitions(-DDEBUG_USB_UART)
    pico_enable_stdio_usb(sd2psx ENABLED)
endif()
