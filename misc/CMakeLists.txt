set(history_icon_bin_names 
        ${CMAKE_CURRENT_BINARY_DIR}/icon_A.obj 
        ${CMAKE_CURRENT_BINARY_DIR}/icon_C.obj 
        ${CMAKE_CURRENT_BINARY_DIR}/icon_J.obj)

add_custom_command(OUTPUT ${history_icon_bin_names}
                COMMAND ${CMAKE_OBJCOPY} --input-target=binary --output-target=elf32-littlearm --binary-architecture arm --rename-section .data=.rodata "icon_A.sys" "${CMAKE_CURRENT_BINARY_DIR}/icon_A.obj"
                COMMAND ${CMAKE_OBJCOPY} --input-target=binary --output-target=elf32-littlearm --binary-architecture arm --rename-section .data=.rodata "icon_C.sys" "${CMAKE_CURRENT_BINARY_DIR}/icon_C.obj"
                COMMAND ${CMAKE_OBJCOPY} --input-target=binary --output-target=elf32-littlearm --binary-architecture arm --rename-section .data=.rodata "icon_J.sys" "${CMAKE_CURRENT_BINARY_DIR}/icon_J.obj"
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                COMMENT "Generating history icons")

add_custom_target(history_icon_objs DEPENDS ${history_icon_bin_names})
    
add_library(ps2_history_icons INTERFACE) 
add_dependencies(ps2_history_icons history_icon_objs)

target_link_libraries(ps2_history_icons INTERFACE
                        ${history_icon_bin_names})

