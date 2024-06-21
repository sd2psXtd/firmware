
add_library(ps2_card STATIC
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_sd2psxman.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_sd2psxman_commands.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_memory_card.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_mc_commands.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_mc_auth.c
                ${CMAKE_CURRENT_SOURCE_DIR}/history_tracker/ps2_history_tracker.c
                ${CMAKE_CURRENT_SOURCE_DIR}/ps2_dirty.c
                ${CMAKE_CURRENT_SOURCE_DIR}/ps2_cardman.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../psram/pio_qspi.c
                ${CMAKE_CURRENT_SOURCE_DIR}/../psram/psram.c
            )

target_include_directories(ps2_card
                            PUBLIC
                                ${CMAKE_CURRENT_SOURCE_DIR}
                                ${CMAKE_CURRENT_SOURCE_DIR}../psram
)

target_link_libraries(ps2_card PRIVATE 
            sd2psx_common
            pico_stdlib
            pico_multicore
            hardware_sync
            hardware_pio
            hardware_flash
            hardware_dma
            mcfat)


pico_generate_pio_header(ps2_card ${CMAKE_CURRENT_LIST_DIR}/card_emu/ps2_mc_spi.pio)
pico_generate_pio_header(ps2_card ${CMAKE_CURRENT_LIST_DIR}/../psram/qspi.pio)
