# USB device stack of the firmware: session lifecycle, descriptors, the
# Serial/MSC profiles and the PS3 Memory Card Adaptor (MCA) emulation.
#
# The library provides strong definitions for several weak TinyUSB hooks
# (usbd_app_driver_get_cb, tud_event_hook_cb, tud_cdc_line_coding_cb). Each one
# lives in an object that the firmware already references for other symbols
# (usb_session.c, mca_device.c), so the archive member is always linked.

add_library(sd2psx_usb STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_session.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_serial.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_reset_itf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mca_device.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mca_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mca_bridge.c
)

target_include_directories(sd2psx_usb
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sd2psx_usb
    PRIVATE
        sd2psx_common
        sd_fat
        pico_stdlib
        pico_unique_id
        pico_stdio_usb_headers
        hardware_sync
        hardware_structs
        hardware_watchdog
    PUBLIC
        tinyusb_device
        tinyusb_board
)

set_target_properties(sd2psx_usb PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/memmap_custom.ld)
