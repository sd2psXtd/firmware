set(STAGE1_PS2_BOOT_VERSION "0.9.1")

set(STAGE1_PS2_BOOT "bootcard.bin")
set(STAGE1_PS2_BOOT_OBJ "${CMAKE_CURRENT_BINARY_DIR}/bootcard.o")


if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${STAGE1_PS2_BOOT_VERSION}/${STAGE1_PS2_BOOT})
    file(DOWNLOAD   "https://github.com/sd2psx/OSDSYS-Launcher/releases/download/${STAGE1_PS2_BOOT_VERSION}/first-stage.mcd"  
                "${CMAKE_CURRENT_BINARY_DIR}/${STAGE1_PS2_BOOT_VERSION}/${STAGE1_PS2_BOOT}" 
                SHOW_PROGRESS)
endif()

add_custom_command(OUTPUT "${STAGE1_PS2_BOOT_OBJ}"
                    COMMAND ${CMAKE_OBJCOPY} ARGS --input-target=binary --output-target=elf32-littlearm --binary-architecture arm --rename-section .data=.rodata ${STAGE1_PS2_BOOT} ${STAGE1_PS2_BOOT_OBJ}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${STAGE1_PS2_BOOT_VERSION}
                    COMMENT "Generating ps2 stage 1 boot obj")

add_custom_target(ps2bootobjs DEPENDS "${STAGE1_PS2_BOOT_OBJ}")


add_library(ps2boot INTERFACE)
add_dependencies(ps2boot ps2bootobjs)

target_link_libraries(ps2boot INTERFACE ${STAGE1_PS2_BOOT_OBJ})

