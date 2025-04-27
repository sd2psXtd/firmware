add_library(ps1_card STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/ps1_cardman.c
            ${CMAKE_CURRENT_SOURCE_DIR}/ps1_dirty.c
            ${CMAKE_CURRENT_SOURCE_DIR}/ps1_memory_card.c
            ${CMAKE_CURRENT_SOURCE_DIR}/ps1_empty_card.c
            ${CMAKE_CURRENT_SOURCE_DIR}/ps1_mc_data_interface.c
            ${CMAKE_CURRENT_SOURCE_DIR}/ps1_mmce.c
)

target_include_directories(ps1_card PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(ps1_card
                        PRIVATE
                            sd2psx_common
                            pico_platform_headers
                            pico_stdlib
                            pico_multicore
                            hardware_pio
                            sd_fat
                            )

if (SD2PSX_WITH_PSRAM)
    target_link_libraries(ps1_card PRIVATE
            psram)
endif()

configure_file(${CMAKE_CURRENT_LIST_DIR}/ps1_mc_spi.pio ${CMAKE_CURRENT_BINARY_DIR}/ps1_mc_spi.pio)

pico_generate_pio_header(ps1_card ${CMAKE_CURRENT_BINARY_DIR}/ps1_mc_spi.pio)
