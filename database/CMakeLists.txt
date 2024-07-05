# PS1
set(GAMEDB_PS1_OBJ "${CMAKE_CURRENT_BINARY_DIR}/gamedbps1.o")

add_custom_target(gamedbobjs_ps1 ALL 
                    COMMAND ${CMAKE_COMMAND}
                        -D OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
                        -D PYTHON_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/parse_GameDB.py
                        -D CMAKE_OBJCOPY=${CMAKE_OBJCOPY}
                        -D SYSTEM=ps1
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_obj_builder.cmake
                    VERBATIM
                    BYPRODUCTS ${GAMEDB_PS1_OBJ})

add_library(gamedb INTERFACE)
add_dependencies(gamedb gamedbobjs_ps1)

target_link_libraries(gamedb INTERFACE ${GAMEDB_PS1_OBJ})

# PS2

set(GAMEDB_PS2_OBJ "${CMAKE_CURRENT_BINARY_DIR}/gamedbps2.o")

add_custom_target(gamedbobjs_ps2 ALL 
                    COMMAND ${CMAKE_COMMAND}
                        -D OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
                        -D PYTHON_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/get_and_parse_hdldb.py
                        -D CMAKE_OBJCOPY=${CMAKE_OBJCOPY}
                        -D SYSTEM=ps2
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_obj_builder.cmake
                    VERBATIM
                    BYPRODUCTS ${GAMEDB_PS2_OBJ})

add_dependencies(gamedb gamedbobjs_ps2)

target_link_libraries(gamedb INTERFACE ${GAMEDB_PS2_OBJ})
