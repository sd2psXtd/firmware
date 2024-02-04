set(GAMEDB_PS1_OBJ "${CMAKE_CURRENT_BINARY_DIR}/gamedbps1.o")

add_custom_target(gamedbobjs ALL 
                    COMMAND ${CMAKE_COMMAND}
                        -D OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
                        -D PYTHON_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/parse_db.py
                        -D CMAKE_OBJCOPY=${CMAKE_OBJCOPY}
                        -D SYSTEM=ps1
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_obj_builder.cmake
                    VERBATIM
                    BYPRODUCTS ${GAMEDB_PS1_OBJ})

add_library(gamedb INTERFACE)
add_dependencies(gamedb gamedbobjs)

target_link_libraries(gamedb INTERFACE ${GAMEDB_PS1_OBJ})
