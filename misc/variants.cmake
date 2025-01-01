
# Variants...
set(VARIANT "SD2PSX" CACHE STRING "Firmware variant to build")
set_property(CACHE VARIANT PROPERTY STRINGS
                    "SD2PSX" 
                    "PMC+" 
                    "SD2PSXlite" 
                    "SD2PSXBF" 
                    "PMCZero"
                    "Other")
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
                            "MMCE_PRODUCT_ID=0x1"
                            )
    set(SD2PSX_WITH_GUI TRUE)
    set(SD2PSX_WITH_PSRAM TRUE)
    add_compile_definitions(PICO_FLASH_SIZE_BYTES=16777216)
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
                            "MMCE_PRODUCT_ID=0x3"
                            )
    set(SD2PSX_WITH_GUI FALSE)
    set(SD2PSX_WITH_PSRAM FALSE)
    add_compile_definitions(PICO_FLASH_SIZE_BYTES=2097152)
elseif( VARIANT STREQUAL "PMCZero")
    set(PIN_PSX_ACK 13)
    set(PIN_PSX_SEL 11)
    set(PIN_PSX_CLK 12)
    set(PIN_PSX_CMD 10)
    set(PIN_PSX_DAT 9)
    set(PIN_PSX_SPD_SEL 11)
    add_compile_definitions("UART_TX=7"
                            "UART_RX=8"
                            "UART_PERIPH=uart1"
                            "UART_BAUD=115200"
                            "SD_PERIPH=SPI"
                            "SD_MISO=0"
                            "SD_MOSI=3"
                            "SD_SCK=2"
                            "SD_CS=1"
                            "FLASH_OFF_CIV=0x1fb000"
                            "FLASH_OFF_EEPROM=0x1fc000"
                            "MMCE_PRODUCT_ID=0x4"
                            )
    set(SD2PSX_WITH_GUI FALSE)
    set(SD2PSX_WITH_PSRAM FALSE)
    add_compile_definitions(PICO_FLASH_SIZE_BYTES=2097152)
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
                            "MMCE_PRODUCT_ID=0x1"
                            PARENT_DIRECTORY)
    set(SD2PSX_WITH_GUI FALSE)
    set(SD2PSX_WITH_PSRAM FALSE)
    add_compile_definitions(PICO_FLASH_SIZE_BYTES=16777216)
else()
    set(PIN_PSX_ACK 16)
    set(PIN_PSX_SEL 17)
    set(PIN_PSX_CLK 18)
    set(PIN_PSX_CMD 19)
    set(PIN_PSX_DAT 20)
    set(PIN_PSX_SPD_SEL 10)
endif()

