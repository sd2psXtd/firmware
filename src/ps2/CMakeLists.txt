
add_library(ps2_card STATIC
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_mmceman_debug.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_sd2psxman.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_sd2psxman_commands.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_memory_card.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_mc_commands.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_mc_auth.c
                ${CMAKE_CURRENT_SOURCE_DIR}/card_emu/ps2_mc_data_interface.c
                ${CMAKE_CURRENT_SOURCE_DIR}/history_tracker/ps2_history_tracker.c
                ${CMAKE_CURRENT_SOURCE_DIR}/mmce_fs/ps2_mmce_fs.c
                ${CMAKE_CURRENT_SOURCE_DIR}/ps2_cardman.c
            )

target_include_directories(ps2_card
                            PUBLIC
                                ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ps2_card PRIVATE
            sd2psx_common
            pico_stdlib
            pico_multicore
            hardware_sync
            hardware_pio
            hardware_flash
            hardware_dma
            mcfat
            sd_fat
            )

if (SD2PSX_WITH_PSRAM)
    target_link_libraries(ps2_card PRIVATE
            psram)
    target_sources(ps2_card PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ps2_dirty.c)
    pico_generate_pio_header(ps2_card ${CMAKE_CURRENT_LIST_DIR}/../psram/qspi.pio)
endif()


configure_file(${CMAKE_CURRENT_LIST_DIR}/card_emu/ps2_mc_spi.pio ${CMAKE_CURRENT_BINARY_DIR}/ps2_mc_spi.pio)

pico_generate_pio_header(ps2_card ${CMAKE_CURRENT_BINARY_DIR}/ps2_mc_spi.pio)
