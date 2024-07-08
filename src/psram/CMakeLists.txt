add_library(psram STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/psram.c
            ${CMAKE_CURRENT_SOURCE_DIR}/pio_qspi.c)

target_include_directories(psram PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(psram PRIVATE 
                        hardware_pio 
                        hardware_dma 
                        sd2psx_common 
                        )

pico_generate_pio_header(psram ${CMAKE_CURRENT_LIST_DIR}/qspi.pio)
