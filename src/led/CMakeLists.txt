
add_library(sd2psx_led STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/led.c
)

target_include_directories(sd2psx_led
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE
)

target_link_libraries(sd2psx_led
  PRIVATE
    sd2psx_common
    hardware_gpio
    hardware_pio
    ps1_card
    ps2_card
)

pico_generate_pio_header(sd2psx_led ${CMAKE_CURRENT_SOURCE_DIR}/ws2812.pio)


target_compile_definitions(sd2psx_led PUBLIC "WITH_LED=1")